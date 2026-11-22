#include "scheduler/kites_hazard_model.h"

#include "processor/pipeline_registers.h"
#include "processor/rv5s/rv5s_hdu.h"

namespace Kites
{
namespace
{
const IssueSlot &slotAt(const SlotHistory &history, std::size_t distance)
{
    static const IssueSlot bubble{};
    return distance <= history.size() ? history[history.size() - distance] : bubble;
}

/** Which raw field of `consumer` the HDU matched against `producer`, and whether it is real. */
void attributeMatch(StallVerdict &verdict, const InstructionFacts &consumer,
                    const InstructionFacts &producer)
{
    const uint8_t rd = producer.rdField;
    const bool gprField =
        producer.kitesRegWrite && rd != 0 && (consumer.rs1Field == rd || consumer.rs2Field == rd);
    const bool fprField =
        producer.kitesFregWrite &&
        (consumer.rs1Field == rd || consumer.rs2Field == rd || consumer.rs3Field == rd);
    const RegId gpr{RegFile::Gpr, rd};
    const RegId fpr{RegFile::Fpr, rd};
    const bool gprReal = gprField && consumer.readsReg(gpr) && producer.writesReg(gpr);
    const bool fprReal = fprField && consumer.readsReg(fpr) && producer.writesReg(fpr);
    if (gprReal || (!fprReal && gprField))
        verdict.reg = gpr;
    else
        verdict.reg = fpr;
    verdict.spurious = !(gprReal || fprReal);
}

ID_EX_Register idExFrom(const InstructionFacts *facts)
{
    ID_EX_Register reg;
    reg.reset();
    if (!facts)
        return reg;
    reg.mem_read = facts->kitesMemRead;
    reg.reg_write = facts->kitesRegWrite;
    reg.freg_write = facts->kitesFregWrite;
    reg.rd = facts->rdField;
    reg.frd = facts->rdField;
    return reg;
}

bool rawRelated(const InstructionFacts &consumer, const InstructionFacts &producer, RegId &reg)
{
    for (const RegId &read : consumer.reads)
    {
        if (producer.writesReg(read))
        {
            reg = read;
            return true;
        }
    }
    return false;
}

unsigned customLatency(const InstructionFacts &producer, const SchedulingOptions &options)
{
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
} // namespace

SlotHistory bubbleHistory(std::size_t count)
{
    return SlotHistory(count);
}

StallVerdict kitesStallsBefore(const InstructionFacts &consumer, const SlotHistory &history,
                               bool forwarding)
{
    // Kites runs the HDU after pipeline_execute(): ID/EX and EX/MEM both describe the
    // instruction executed this cycle (p1); EX/MEM.prev_* describes the one before (p2).
    const IssueSlot &p1 = slotAt(history, 1);
    const IssueSlot &p2 = slotAt(history, 2);

    IF_ID_Register ifId;
    ifId.instruction = consumer.word;
    const ID_EX_Register idEx = idExFrom(p1.facts);
    const ID_EX_Register prev = idExFrom(p2.facts);

    EX_MEM_Register exMem;
    exMem.reset();
    exMem.reg_write = idEx.reg_write;
    exMem.rd = idEx.rd;
    exMem.freg_write = idEx.freg_write;
    exMem.frd = idEx.frd;
    exMem.prev_reg_write = prev.reg_write;
    exMem.prev_rd = prev.rd;
    exMem.prev_freg_write = prev.freg_write;
    exMem.prev_frd = prev.frd;

    StallVerdict verdict;
    const int result = check_data_hazard(ifId, idEx, exMem, forwarding);
    if (result <= 0)
        return verdict;

    verdict.cycles = static_cast<unsigned>(result);
    // With forwarding only a load in EX can stall; without it 2 => p1, 1 => p2.
    const bool fromP1 = forwarding || result == STALL_TWO_CYCLES;
    const IssueSlot &producer = fromP1 ? p1 : p2;
    verdict.producerDistance = fromP1 ? 1 : 2;
    verdict.producer = producer.facts;
    verdict.producerUnit = producer.unit;
    if (producer.facts)
        attributeMatch(verdict, consumer, *producer.facts);
    return verdict;
}

StallVerdict customStallsBefore(const InstructionFacts &consumer, const SlotHistory &history,
                                const SchedulingOptions &options)
{
    StallVerdict verdict;
    for (std::size_t k = 1; k <= kHistoryDepth && k <= history.size(); ++k)
    {
        const IssueSlot &slot = slotAt(history, k);
        RegId reg;
        if (!slot.facts || !rawRelated(consumer, *slot.facts, reg))
            continue;
        const unsigned latency = customLatency(*slot.facts, options);
        if (latency > k && latency - k > verdict.cycles)
        {
            verdict.cycles = static_cast<unsigned>(latency - k);
            verdict.producerDistance = static_cast<int>(k);
            verdict.producer = slot.facts;
            verdict.producerUnit = slot.unit;
            verdict.reg = reg;
        }
    }
    return verdict;
}

StallVerdict stallsBefore(const InstructionFacts &consumer, const SlotHistory &history,
                          const SchedulingOptions &options)
{
    if (!options.hazardDetectionEnabled)
        return {};
    if (options.stallModel == StallModelKind::KitesExact && consumer.hasWord)
        return kitesStallsBefore(consumer, history, options.forwardingEnabled);
    return customStallsBefore(consumer, history, options);
}

namespace
{
/** Producers in `history` that form a Q1/Q2 quirk pair with `consumer` (see the header). */
std::vector<const InstructionFacts *> quirkProducers(const InstructionFacts &consumer,
                                                     const SlotHistory &history)
{
    std::vector<const InstructionFacts *> producers;
    if (!consumer.floatingPoint)
    {
        for (std::size_t k = 1; k <= 2; ++k)
        {
            const InstructionFacts *producer = slotAt(history, k).facts;
            if (producer && producer->floatingPoint && producer->kitesRegWrite &&
                producer->rdField != 0 && consumer.readsReg({RegFile::Gpr, producer->rdField}))
                producers.push_back(producer);
        }
    }
    else if (consumer.fpReadsGprRs1 && consumer.rs1Field != 0)
    {
        const InstructionFacts *producer = slotAt(history, 1).facts;
        if (producer && producer->writesReg({RegFile::Gpr, consumer.rs1Field}))
            producers.push_back(producer);
    }
    return producers;
}
/** Number of Kites quirk pairs `consumer` forms with the last two issued slots. */
unsigned quirkPairsBefore(const InstructionFacts &consumer, const SlotHistory &history)
{
    return static_cast<unsigned>(quirkProducers(consumer, history).size());
}
} // namespace

std::set<std::pair<std::size_t, std::size_t>> quirkPairSet(const std::vector<IssueSlot> &words,
                                                           const SlotHistory &entryHistory,
                                                           const SchedulingOptions &options)
{
    std::set<std::pair<std::size_t, std::size_t>> pairs;
    SlotHistory history = entryHistory;
    unsigned ignored = 0;
    for (const IssueSlot &word : words)
    {
        // Same stall insertion as issueWord(), then the pairs seen just before the word issues.
        SlotHistory stalled = history;
        while (const unsigned cycles = stallsBefore(*word.facts, stalled, options).cycles)
            stalled.insert(stalled.end(), cycles, IssueSlot{});
        for (const InstructionFacts *producer : quirkProducers(*word.facts, stalled))
            pairs.emplace(producer->programIndex, word.facts->programIndex);
        issueWord(word, history, options, ignored);
    }
    return pairs;
}

StallVerdict issueWord(const IssueSlot &word, SlotHistory &history,
                       const SchedulingOptions &options, unsigned &quirkPairs)
{
    StallVerdict total;
    // Kites re-runs the HDU once the stall it started has drained, so loop until clear.
    // The first verdict names the producer; later ones only add cycles.
    for (;;)
    {
        const StallVerdict verdict = stallsBefore(*word.facts, history, options);
        if (verdict.cycles == 0)
            break;
        if (total.cycles == 0)
            total = StallVerdict{0,
                                 verdict.producerDistance,
                                 verdict.reg,
                                 verdict.spurious,
                                 verdict.producer,
                                 verdict.producerUnit};
        total.cycles += verdict.cycles;
        history.insert(history.end(), verdict.cycles, IssueSlot{});
    }
    quirkPairs += quirkPairsBefore(*word.facts, history);
    history.push_back(word);
    if (history.size() > kHistoryDepth)
        history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(kHistoryDepth));
    return total;
}

SequenceCost evaluateSequence(const std::vector<IssueSlot> &words, const SlotHistory &entryHistory,
                              const SchedulingOptions &options)
{
    SequenceCost cost;
    cost.exitHistory = entryHistory;
    cost.perWord.reserve(words.size());
    for (const IssueSlot &word : words)
    {
        const StallVerdict verdict = issueWord(word, cost.exitHistory, options, cost.quirkPairs);
        cost.stalls += verdict.cycles;
        cost.perWord.push_back(verdict);
    }
    return cost;
}
} // namespace Kites
