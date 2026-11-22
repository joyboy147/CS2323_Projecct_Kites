#include "scheduler/basic_block_scheduler.h"

#include "scheduler/instruction_facts.h"
#include "scheduler/kites_hazard_model.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace Kites
{
namespace
{
constexpr unsigned kPipelineFillCycles = 4;
constexpr std::size_t kExitLookaheadUnits = 2;

struct Unit
{
    ScheduleUnit info;
    std::vector<const InstructionFacts *> words;
    std::vector<RegId> reads; ///< Read before being written inside the unit.
    std::vector<RegId> writes;
    bool memory{};
    bool unconditionalJump{};
    bool hasAllWords{true};
};

struct Block
{
    std::vector<std::size_t> programIndices;
    std::vector<Unit> units;
    std::vector<DependencyEdge> edges;
    std::vector<std::vector<std::size_t>> predecessors;
    std::vector<unsigned> criticalPath;
    SchedulingOptions options;
    bool fallsThrough{true};
    std::size_t movableCount{};
};

using Order = std::vector<std::size_t>;

void addUnique(std::vector<RegId> &regs, const RegId &reg)
{
    if (std::find(regs.begin(), regs.end(), reg) == regs.end())
        regs.push_back(reg);
}

std::vector<bool> findLeaders(const AssembledProgram &program,
                              const std::vector<InstructionFacts> &facts)
{
    const std::size_t count = facts.size();
    std::vector<bool> leader(count, false);
    if (count == 0)
        return leader;
    leader[0] = true;

    std::map<std::size_t, std::size_t> programIndexOfInstruction;
    for (std::size_t i = 0; i < count; ++i)
        programIndexOfInstruction[program.intermediate_code[i].first.getInstructionIndex()] = i;
    auto markInstruction = [&](int64_t instructionIndex)
    {
        if (instructionIndex < 0)
            return;
        const auto it = programIndexOfInstruction.find(static_cast<std::size_t>(instructionIndex));
        if (it != programIndexOfInstruction.end())
            leader[it->second] = true;
    };

    for (const auto &[_, symbol] : program.symbol_table)
        if (!symbol.isData)
            markInstruction(static_cast<int64_t>(symbol.address / 4));

    for (std::size_t i = 0; i < count; ++i)
    {
        const InstructionFacts &f = facts[i];
        if ((f.controlTransfer || f.barrier) && i + 1 < count)
            leader[i + 1] = true;
        if (f.branchOffsetBytes)
        {
            const auto instructionIndex =
                static_cast<int64_t>(program.intermediate_code[i].first.getInstructionIndex());
            markInstruction(instructionIndex + *f.branchOffsetBytes / 4);
        }
    }
    return leader;
}

unsigned rawEdgeLatency(const InstructionFacts &producer, const SchedulingOptions &options)
{
    if (options.stallModel == StallModelKind::KitesExact)
    {
        if (!options.forwardingEnabled)
            return 3;
        return producer.load ? 2 : 1;
    }
    const LatencySettings &lat = options.latencies;
    if (!options.forwardingEnabled)
        return lat.noForwarding;
    if (producer.load)
        return lat.loadUse;
    if (producer.mul)
        return lat.mul;
    if (producer.div)
        return lat.div;
    return lat.alu;
}

Unit makeUnit(const std::vector<InstructionFacts> &facts, const std::vector<std::size_t> &members,
              const std::set<uint64_t> &labelLines)
{
    Unit unit;
    unit.info.programIndices = members;
    unit.info.sourceLine = facts[members.front()].sourceLine;
    unit.info.pinnedFirst = labelLines.contains(unit.info.sourceLine);
    std::vector<RegId> written;
    for (std::size_t index : members)
    {
        const InstructionFacts &f = facts[index];
        unit.words.push_back(&f);
        for (const RegId &reg : f.reads)
            if (std::find(written.begin(), written.end(), reg) == written.end())
                addUnique(unit.reads, reg);
        for (const RegId &reg : f.writes)
        {
            addUnique(written, reg);
            addUnique(unit.writes, reg);
        }
        unit.memory = unit.memory || f.memoryAccess();
        unit.info.pinnedLast = unit.info.pinnedLast || f.controlTransfer || f.barrier;
        unit.unconditionalJump = unit.unconditionalJump || f.unconditionalJump;
        unit.hasAllWords = unit.hasAllWords && f.hasWord;
    }
    // A lone auipc computes PC + imm: moving its line changes its value. (A multi-word line
    // such as `la` is re-encoded for its new address on reassembly, so it may move.) Pinning
    // it at both ends keeps it at the same position, so nothing crosses it.
    if (members.size() == 1 && facts[members.front()].readsPc)
        unit.info.pinnedFirst = unit.info.pinnedLast = true;
    return unit;
}

/** Adds each (producer, consumer, kind) edge once. */
class EdgeBuilder
{
  public:
    explicit EdgeBuilder(Block &block) : m_block(block)
    {
    }

    void add(std::size_t producer, std::size_t consumer, DependencyKind kind, unsigned latency,
             RegId reg = {})
    {
        if (m_seen.emplace(producer, consumer, kind).second)
            m_block.edges.push_back({producer, consumer, kind, latency, reg});
    }

  private:
    Block &m_block;
    std::set<std::tuple<std::size_t, std::size_t, DependencyKind>> m_seen;
};

/** RAW from the last writer, WAW, and WAR from every reader since that write. */
void addRegisterEdges(Block &block, EdgeBuilder &edges)
{
    std::map<RegId, std::size_t> lastWriter;
    std::map<RegId, std::vector<std::size_t>> readersSinceWrite;
    for (std::size_t u = 0; u < block.units.size(); ++u)
    {
        const Unit &unit = block.units[u];
        for (const RegId &reg : unit.reads)
        {
            const auto writer = lastWriter.find(reg);
            if (writer == lastWriter.end())
                continue;
            const Unit &producer = block.units[writer->second];
            edges.add(writer->second, u, DependencyKind::ReadAfterWrite,
                      rawEdgeLatency(*producer.words.back(), block.options), reg);
        }
        for (const RegId &reg : unit.writes)
        {
            if (const auto writer = lastWriter.find(reg); writer != lastWriter.end())
                edges.add(writer->second, u, DependencyKind::WriteAfterWrite, 1, reg);
            for (std::size_t reader : readersSinceWrite[reg])
                if (reader != u)
                    edges.add(reader, u, DependencyKind::WriteAfterRead, 1, reg);
            lastWriter[reg] = u;
            readersSinceWrite[reg].clear();
        }
        for (const RegId &reg : unit.reads)
            if (std::find(unit.writes.begin(), unit.writes.end(), reg) == unit.writes.end())
                readersSinceWrite[reg].push_back(u);
    }
}

/** Memory operations (including load/load, for MMIO) keep their relative order. */
void addMemoryEdges(Block &block, EdgeBuilder &edges)
{
    std::optional<std::size_t> lastMemory;
    for (std::size_t u = 0; u < block.units.size(); ++u)
    {
        if (!block.units[u].memory)
            continue;
        if (lastMemory)
            edges.add(*lastMemory, u, DependencyKind::MemoryOrder, 1);
        lastMemory = u;
    }
}

/** Control transfers and barriers stay last; label lines stay first. */
void addPinEdges(Block &block, EdgeBuilder &edges)
{
    const std::size_t count = block.units.size();
    for (std::size_t u = 0; u < count; ++u)
    {
        if (block.units[u].info.pinnedLast)
            for (std::size_t earlier = 0; earlier < u; ++earlier)
                edges.add(earlier, u, DependencyKind::Barrier, 1);
        if (block.units[u].info.pinnedFirst)
            for (std::size_t later = u + 1; later < count; ++later)
                edges.add(u, later, DependencyKind::Barrier, 1);
    }
}

void buildDependencies(Block &block)
{
    EdgeBuilder edges(block);
    addRegisterEdges(block, edges);
    addMemoryEdges(block, edges);
    addPinEdges(block, edges);

    const std::size_t count = block.units.size();
    block.predecessors.assign(count, {});
    for (const DependencyEdge &edge : block.edges)
        block.predecessors[edge.consumer].push_back(edge.producer);

    // Critical path: units in reverse original order are a topological order.
    block.criticalPath.assign(count, 1);
    for (std::size_t u = count; u-- > 0;)
        for (const DependencyEdge &edge : block.edges)
            if (edge.producer == u)
                block.criticalPath[u] = std::max(block.criticalPath[u],
                                                 edge.latency + block.criticalPath[edge.consumer]);
}

std::vector<Block> buildBlocks(const AssembledProgram &program,
                               const std::vector<InstructionFacts> &facts,
                               const SchedulingOptions &options)
{
    std::set<uint64_t> labelLines;
    for (const auto &[_, symbol] : program.symbol_table)
        if (!symbol.isData)
            labelLines.insert(symbol.line_number);

    const std::vector<bool> leader = findLeaders(program, facts);
    std::vector<Block> blocks;
    for (std::size_t i = 0; i < facts.size(); ++i)
    {
        if (leader[i] || blocks.empty())
            blocks.emplace_back();
        blocks.back().programIndices.push_back(i);
    }

    for (Block &block : blocks)
    {
        std::vector<std::size_t> members;
        for (std::size_t index : block.programIndices)
        {
            if (!members.empty() && facts[members.back()].sourceLine != facts[index].sourceLine)
            {
                block.units.push_back(makeUnit(facts, members, labelLines));
                members.clear();
            }
            members.push_back(index);
        }
        if (!members.empty())
            block.units.push_back(makeUnit(facts, members, labelLines));

        block.options = options;
        const bool allWords = std::all_of(block.units.begin(), block.units.end(),
                                          [](const Unit &u) { return u.hasAllWords; });
        if (!allWords && options.stallModel == StallModelKind::KitesExact)
        {
            block.options.stallModel = StallModelKind::CustomLatency;
            block.options.latencies = LatencySettings{};
        }
        block.fallsThrough = !block.units.back().unconditionalJump;
        block.movableCount = static_cast<std::size_t>(
            std::count_if(block.units.begin(), block.units.end(),
                          [](const Unit &u) { return !u.info.pinnedFirst && !u.info.pinnedLast; }));
        buildDependencies(block);
    }
    return blocks;
}

std::vector<IssueSlot> wordsInOrder(const Block &block, const Order &order)
{
    std::vector<IssueSlot> words;
    for (std::size_t u : order)
        for (const InstructionFacts *word : block.units[u].words)
            words.push_back({word, u});
    return words;
}

SlotHistory asEntry(SlotHistory history)
{
    for (IssueSlot &slot : history)
        slot.unit = kNoUnit;
    return history;
}

Order identityOrder(std::size_t count)
{
    Order order(count);
    for (std::size_t i = 0; i < count; ++i)
        order[i] = i;
    return order;
}

class BlockScheduler
{
  public:
    BlockScheduler(const Block &block, const Block *next) : m_block(block), m_next(next)
    {
    }

    struct Evaluation
    {
        SequenceCost sequence;
        unsigned exitStalls{};
        unsigned exitQuirks{};

        [[nodiscard]] unsigned cost() const
        {
            return sequence.stalls + exitStalls;
        }
        [[nodiscard]] unsigned quirks() const
        {
            return sequence.quirkPairs + exitQuirks;
        }
    };

    Evaluation evaluate(const Order &order, const SlotHistory &entry) const
    {
        Evaluation result;
        result.sequence = evaluateSequence(wordsInOrder(m_block, order), entry, m_block.options);
        addExit(result);
        return result;
    }

    /** Chooses the order for this block given the entry context of the chosen predecessor. */
    Order choose(const SlotHistory &entry, std::string &note, bool &accepted)
    {
        const std::size_t count = m_block.units.size();
        const Order original = identityOrder(count);
        accepted = false;
        if (count <= 1)
        {
            note = "single unit";
            return original;
        }

        const Evaluation base = evaluate(original, entry);
        m_entry = &entry;
        m_originalQuirkPairs = quirkPairSet(wordsWithExit(original), entry, m_block.options);
        m_best = Candidate{original, base.cost(), false, 0};

        consider(greedy(entry, true), entry);
        consider(greedy(entry, false), entry);
        if (m_block.movableCount <= m_block.options.exhaustiveSearchLimit)
        {
            Order partial;
            std::vector<bool> placed(count, false);
            unsigned quirks = 0;
            searchExhaustive(partial, placed, entry, 0, quirks);
        }
        else
        {
            note = "block too large for exhaustive search; greedy result used";
        }

        if (m_best.cost < base.cost() && !m_best.quirkViolation)
        {
            accepted = true;
            return m_best.order;
        }
        if (note.empty())
            note = "no improvement";
        return original;
    }

  private:
    struct Candidate
    {
        Order order;
        unsigned cost{};
        bool quirkViolation{};
        std::size_t moved{};

        [[nodiscard]] bool betterThan(const Candidate &other) const
        {
            return std::tie(cost, quirkViolation, moved, order) <
                   std::tie(other.cost, other.quirkViolation, other.moved, other.order);
        }
    };

    void addExit(Evaluation &evaluation) const
    {
        if (!m_block.fallsThrough || !m_next)
            return;
        std::vector<IssueSlot> head;
        for (std::size_t u = 0; u < m_next->units.size() && u < kExitLookaheadUnits; ++u)
            for (const InstructionFacts *word : m_next->units[u].words)
                head.push_back({word, kNoUnit});
        const SequenceCost exit =
            evaluateSequence(head, evaluation.sequence.exitHistory, m_block.options);
        evaluation.exitStalls = exit.stalls;
        evaluation.exitQuirks = exit.quirkPairs;
    }

    /** The block's words in `order`, followed by the next block's head when it falls through. */
    std::vector<IssueSlot> wordsWithExit(const Order &order) const
    {
        std::vector<IssueSlot> words = wordsInOrder(m_block, order);
        if (m_block.fallsThrough && m_next)
            for (std::size_t u = 0; u < m_next->units.size() && u < kExitLookaheadUnits; ++u)
                for (const InstructionFacts *word : m_next->units[u].words)
                    words.push_back({word, kNoUnit});
        return words;
    }

    /**
     * True if `order` forms a Kites quirk pair that the original order does not. Comparing
     * pair counts is not enough: a move can remove one pair and create another.
     */
    bool createsQuirkPair(const Order &order, unsigned quirks) const
    {
        if (quirks == 0)
            return false;
        const auto after = quirkPairSet(wordsWithExit(order), *m_entry, m_block.options);
        return !std::includes(m_originalQuirkPairs.begin(), m_originalQuirkPairs.end(),
                              after.begin(), after.end());
    }

    bool ready(std::size_t unit, const std::vector<bool> &placed) const
    {
        if (placed[unit])
            return false;
        const auto &preds = m_block.predecessors[unit];
        return std::all_of(preds.begin(), preds.end(), [&](std::size_t p) { return placed[p]; });
    }

    unsigned issueUnit(std::size_t unit, SlotHistory &history, unsigned &quirks) const
    {
        unsigned stalls = 0;
        for (const InstructionFacts *word : m_block.units[unit].words)
            stalls += issueWord({word, unit}, history, m_block.options, quirks).cycles;
        return stalls;
    }

    Order greedy(const SlotHistory &entry, bool preferCriticalPath) const
    {
        const std::size_t count = m_block.units.size();
        std::vector<bool> placed(count, false);
        SlotHistory history = entry;
        Order order;
        while (order.size() < count)
        {
            std::size_t best = kNoUnit;
            std::tuple<unsigned, unsigned, std::size_t> bestKey{};
            for (std::size_t u = 0; u < count; ++u)
            {
                if (!ready(u, placed))
                    continue;
                SlotHistory trial = history;
                unsigned quirks = 0;
                const unsigned stalls = issueUnit(u, trial, quirks);
                const unsigned pathPenalty =
                    preferCriticalPath ? ~0U - m_block.criticalPath[u] : 0U;
                const auto key = std::make_tuple(stalls, pathPenalty, u);
                if (best == kNoUnit || key < bestKey)
                {
                    best = u;
                    bestKey = key;
                }
            }
            placed[best] = true;
            unsigned quirks = 0;
            issueUnit(best, history, quirks);
            order.push_back(best);
        }
        return order;
    }

    void consider(const Order &order, const SlotHistory &entry)
    {
        const Evaluation evaluation = evaluate(order, entry);
        offer(order, evaluation.cost(), evaluation.quirks());
    }

    void offer(const Order &order, unsigned cost, unsigned quirks)
    {
        const std::vector<bool> moved = movedUnits(identityOrder(order.size()), order);
        const Candidate candidate{
            order, cost, createsQuirkPair(order, quirks),
            static_cast<std::size_t>(std::count(moved.begin(), moved.end(), true))};
        if (candidate.betterThan(m_best))
            m_best = candidate;
    }

    void searchExhaustive(Order &partial, std::vector<bool> &placed, const SlotHistory &history,
                          unsigned stalls, unsigned quirks)
    {
        if (stalls > m_best.cost)
            return; // partial cost only grows
        const std::size_t count = m_block.units.size();
        if (partial.size() == count)
        {
            Evaluation evaluation;
            evaluation.sequence.stalls = stalls;
            evaluation.sequence.quirkPairs = quirks;
            evaluation.sequence.exitHistory = history;
            addExit(evaluation);
            offer(partial, evaluation.cost(), evaluation.quirks());
            return;
        }
        for (std::size_t u = 0; u < count; ++u)
        {
            if (!ready(u, placed))
                continue;
            SlotHistory next = history;
            unsigned nextQuirks = quirks;
            const unsigned unitStalls = issueUnit(u, next, nextQuirks);
            placed[u] = true;
            partial.push_back(u);
            searchExhaustive(partial, placed, next, stalls + unitStalls, nextQuirks);
            partial.pop_back();
            placed[u] = false;
        }
    }

    const Block &m_block;
    const Block *m_next;
    const SlotHistory *m_entry{};
    std::set<std::pair<std::size_t, std::size_t>> m_originalQuirkPairs;
    Candidate m_best;
};

std::vector<StallInfo> stallInfoPerUnit(const Block &block, const Order &order,
                                        const SequenceCost &sequence)
{
    std::vector<StallInfo> info(block.units.size());
    const std::vector<IssueSlot> words = wordsInOrder(block, order);
    for (std::size_t i = 0; i < words.size(); ++i)
    {
        const StallVerdict &verdict = sequence.perWord[i];
        if (verdict.cycles == 0)
            continue;
        StallInfo &unitInfo = info[words[i].unit];
        if (unitInfo.cycles == 0)
        {
            unitInfo.producerUnit = verdict.producerUnit;
            unitInfo.producerProgramIndex =
                verdict.producer ? verdict.producer->programIndex : kNoUnit;
            unitInfo.stalledProgramIndex = words[i].facts->programIndex;
            unitInfo.reg = verdict.reg;
            unitInfo.spurious = verdict.spurious;
            unitInfo.viaForwarding = block.options.forwardingEnabled;
        }
        unitInfo.cycles += verdict.cycles;
    }
    return info;
}

std::vector<std::size_t> flatten(const Block &block, const Order &order)
{
    std::vector<std::size_t> indices;
    for (std::size_t u : order)
        indices.insert(indices.end(), block.units[u].info.programIndices.begin(),
                       block.units[u].info.programIndices.end());
    return indices;
}

unsigned sumCycles(const std::vector<StallInfo> &stalls)
{
    unsigned total = 0;
    for (const StallInfo &s : stalls)
        total += s.cycles;
    return total;
}
} // namespace

std::vector<bool> movedUnits(const std::vector<std::size_t> &original,
                             const std::vector<std::size_t> &suggested)
{
    const std::size_t n = original.size();
    const std::size_t m = suggested.size();
    std::vector<std::vector<unsigned>> lcs(n + 1, std::vector<unsigned>(m + 1, 0));
    for (std::size_t i = n; i-- > 0;)
        for (std::size_t j = m; j-- > 0;)
            lcs[i][j] = original[i] == suggested[j] ? lcs[i + 1][j + 1] + 1
                                                    : std::max(lcs[i + 1][j], lcs[i][j + 1]);

    std::map<std::size_t, std::size_t> originalPosition;
    for (std::size_t i = 0; i < n; ++i)
        originalPosition[original[i]] = i;

    std::size_t maxId = 0;
    for (std::size_t id : original)
        maxId = std::max(maxId, id);
    std::vector<bool> matched(n == 0 ? 0 : maxId + 1, false);
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < n && j < m)
    {
        if (original[i] == suggested[j])
        {
            matched[original[i]] = true;
            ++i;
            ++j;
        }
        else if (lcs[i + 1][j] > lcs[i][j + 1])
            ++i;
        else if (lcs[i][j + 1] > lcs[i + 1][j])
            ++j;
        else if (i > originalPosition[suggested[j]]) // tie: skip the later original element
            ++i;
        else
            ++j;
    }
    std::vector<bool> moved(matched.size(), false);
    for (std::size_t id : original)
        moved[id] = !matched[id];
    return moved;
}

SchedulingResult BasicBlockScheduler::schedule(const AssembledProgram &program,
                                               const SchedulingOptions &options) const
{
    SchedulingResult result;
    std::vector<InstructionFacts> facts;
    facts.reserve(program.intermediate_code.size());
    for (std::size_t i = 0; i < program.intermediate_code.size(); ++i)
        facts.push_back(describeInstruction(program, i, options));

    const std::vector<Block> blocks = buildBlocks(program, facts, options);
    const auto wordCount = static_cast<unsigned>(facts.size());

    if (!options.hazardDetectionEnabled)
    {
        result.applicable = false;
        result.notApplicableReason =
            "Without hazard detection this pipeline never stalls, so reordering cannot save "
            "cycles. It can only change which (possibly stale) values instructions read. Use a "
            "processor with hazard detection.";
    }

    SlotHistory originalEntry = bubbleHistory();
    SlotHistory chosenEntry = bubbleHistory();
    for (std::size_t b = 0; b < blocks.size(); ++b)
    {
        const Block &block = blocks[b];
        const Block *next = b + 1 < blocks.size() ? &blocks[b + 1] : nullptr;
        BlockScheduler scheduler(block, next);
        const Order original = identityOrder(block.units.size());

        BasicBlockSchedule out;
        for (const Unit &unit : block.units)
            out.units.push_back(unit.info);
        out.dependencies = block.edges;
        out.originalInstructionIndices = block.programIndices;

        Order chosen = original;
        if (result.applicable)
            chosen = scheduler.choose(chosenEntry, out.note, out.accepted);
        else
            out.note = "not applicable";

        const auto originalEval = scheduler.evaluate(original, originalEntry);
        const auto chosenEval = scheduler.evaluate(chosen, chosenEntry);
        out.scheduledUnitOrder = chosen;
        out.scheduledInstructionIndices = flatten(block, chosen);
        if (result.applicable)
        {
            out.originalStalls = stallInfoPerUnit(block, original, originalEval.sequence);
            out.scheduledStalls = stallInfoPerUnit(block, chosen, chosenEval.sequence);
            out.originalExitStalls = originalEval.exitStalls;
            out.scheduledExitStalls = chosenEval.exitStalls;
        }
        else
        {
            out.originalStalls.assign(block.units.size(), {});
            out.scheduledStalls.assign(block.units.size(), {});
        }

        const auto blockWords = static_cast<unsigned>(block.programIndices.size());
        out.originalCycleCount = blockWords + sumCycles(out.originalStalls);
        out.scheduledCycleCount = blockWords + sumCycles(out.scheduledStalls);
        result.originalCycleCount += out.originalCycleCount;
        result.scheduledCycleCount += out.scheduledCycleCount;
        result.originalStalls += sumCycles(out.originalStalls);
        result.scheduledStalls += sumCycles(out.scheduledStalls);

        // Kites flushes after jal/jalr, so nothing is in flight at the next block's head.
        originalEntry =
            block.fallsThrough ? asEntry(originalEval.sequence.exitHistory) : bubbleHistory();
        chosenEntry =
            block.fallsThrough ? asEntry(chosenEval.sequence.exitHistory) : bubbleHistory();
        result.blocks.push_back(std::move(out));
    }

    result.straightLineOriginalCycles =
        wordCount == 0 ? 0 : wordCount + result.originalStalls + kPipelineFillCycles;
    result.straightLineScheduledCycles =
        wordCount == 0 ? 0 : wordCount + result.scheduledStalls + kPipelineFillCycles;
    return result;
}
} // namespace Kites
