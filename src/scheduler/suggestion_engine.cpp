#include "scheduler/suggestion_engine.h"

#include "scheduler/basic_block_scheduler.h"
#include "scheduler/instruction_facts.h"
#include "scheduler/source_rewriter.h"

#include <algorithm>
#include <set>

namespace Kites
{
std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            if (start < text.size())
                lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::string displayLine(const std::vector<std::string> &lines, unsigned line)
{
    if (line == 0 || line > lines.size())
        return {};
    std::string text = lines[line - 1];
    const auto comment = text.find_first_of("#;");
    if (comment != std::string::npos)
        text.resize(comment);
    std::string collapsed;
    for (char c : text)
    {
        const bool space = c == ' ' || c == '\t' || c == '\r';
        if (space && (collapsed.empty() || collapsed.back() == ' '))
            continue;
        collapsed += space ? ' ' : c;
    }
    if (!collapsed.empty() && collapsed.back() == ' ')
        collapsed.pop_back();
    return collapsed;
}

namespace
{
std::string plural(unsigned count, const std::string &word)
{
    return std::to_string(count) + " " + word + (count == 1 ? "" : "s");
}

std::string quoted(const std::string &text)
{
    return "`" + text + "`";
}

std::string mnemonic(const std::string &text)
{
    return text.substr(0, text.find_first_of(" \t"));
}

class Describer
{
  public:
    Describer(const AssembledProgram &program, const std::vector<std::string> &lines,
              const SchedulingOptions &options)
        : m_program(program), m_lines(lines), m_options(options)
    {
    }

    unsigned lineOf(std::size_t programIndex) const
    {
        return m_program.intermediate_code[programIndex].first.getLineNumber();
    }

    std::string textOfLine(unsigned line) const
    {
        return displayLine(m_lines, line);
    }

    std::string textOf(std::size_t programIndex) const
    {
        return textOfLine(lineOf(programIndex));
    }

    InstructionFacts facts(std::size_t programIndex) const
    {
        return describeInstruction(m_program, programIndex, m_options);
    }

    std::string stallReason(const StallInfo &info) const
    {
        if (info.cycles == 0 || info.producerProgramIndex == kNoUnit)
            return {};
        const std::string producer = quoted(textOf(info.producerProgramIndex)) + " (line " +
                                     std::to_string(lineOf(info.producerProgramIndex)) + ")";
        const std::string reg = regName(info.reg);
        if (info.spurious)
            return "hazard unit matches " + reg +
                   " in a raw register field (not a real read) "
                   "against " +
                   producer;
        if (m_options.stallModel == StallModelKind::CustomLatency)
            return reg + " from " + producer + " not ready yet (custom latency)";
        if (info.viaForwarding)
            return "load-use on " + reg + " from " + producer;
        return "waits for " + producer + " to write " + reg + " back";
    }

  private:
    const AssembledProgram &m_program;
    const std::vector<std::string> &m_lines;
    const SchedulingOptions &m_options;
};

/** Explanations for one accepted block; unit ids in the result are global. */
class BlockExplainer
{
  public:
    BlockExplainer(const BasicBlockSchedule &block, std::size_t blockIndex, std::size_t firstUnit,
                   const Describer &describer, const SchedulingOptions &options)
        : m_block(block), m_blockIndex(blockIndex), m_firstUnit(firstUnit), m_describer(describer),
          m_options(options)
    {
        const std::size_t count = block.units.size();
        m_newPos.resize(count);
        for (std::size_t k = 0; k < count; ++k)
            m_newPos[block.scheduledUnitOrder[k]] = k;
        std::vector<std::size_t> identity(count);
        for (std::size_t i = 0; i < count; ++i)
            identity[i] = i;
        m_moved = movedUnits(identity, block.scheduledUnitOrder);
    }

    std::vector<Explanation> explain()
    {
        std::vector<Explanation> out;
        std::set<std::size_t> covered;
        for (std::size_t c : m_block.scheduledUnitOrder)
        {
            const StallInfo &before = m_block.originalStalls[c];
            const StallInfo &after = m_block.scheduledStalls[c];
            if (before.cycles <= after.cycles)
                continue;
            const unsigned saved = before.cycles - after.cycles;
            const std::size_t p = before.producerUnit;

            std::vector<std::size_t> fillers;
            for (std::size_t f : m_block.scheduledUnitOrder)
            {
                if (f == c || f == p || !m_moved[f] || covered.contains(f))
                    continue;
                const bool nowBetween =
                    (p == kNoUnit || m_newPos[p] < m_newPos[f]) && m_newPos[f] < m_newPos[c];
                const bool wasBetween = (p == kNoUnit || p < f) && f < c;
                if (nowBetween && !wasBetween)
                    fillers.push_back(f);
            }

            if (!fillers.empty())
            {
                for (std::size_t i = 0; i < fillers.size(); ++i)
                {
                    out.push_back(fillerExplanation(fillers[i], c, before, i == 0 ? saved : 0));
                    covered.insert(fillers[i]);
                }
            }
            else if (p != kNoUnit && m_moved[p] && m_newPos[p] < p && !covered.contains(p))
            {
                out.push_back(hoistExplanation(p, c, saved));
                covered.insert(p);
            }
            else if (m_moved[c] && m_newPos[c] > c && !covered.contains(c))
            {
                out.push_back(deferExplanation(c, before, saved));
                covered.insert(c);
            }
        }
        for (std::size_t u : m_block.scheduledUnitOrder)
        {
            if (!m_moved[u] || covered.contains(u))
                continue;
            Explanation e = base(u);
            e.headline = "Moved " + quoted(text(u)) + " " + direction(u) +
                         " as part of this block's reordering";
            e.detail = "It is independent of the instructions it crosses.";
            appendSafety(e, u);
            out.push_back(std::move(e));
        }
        return out;
    }

  private:
    std::string text(std::size_t unit) const
    {
        return m_describer.textOfLine(m_block.units[unit].sourceLine);
    }

    std::size_t global(std::size_t unit) const
    {
        return m_firstUnit + unit;
    }

    std::string direction(std::size_t unit) const
    {
        const std::size_t newPos = m_newPos[unit];
        const std::size_t distance = newPos > unit ? newPos - unit : unit - newPos;
        return std::string(newPos < unit ? "up " : "down ") +
               plural(static_cast<unsigned>(distance), "row");
    }

    Explanation base(std::size_t moved) const
    {
        Explanation e;
        e.blockIndex = m_blockIndex;
        e.movedUnitId = global(moved);
        return e;
    }

    std::vector<std::size_t> crossed(std::size_t unit) const
    {
        std::vector<std::size_t> out;
        for (std::size_t other = 0; other < m_block.units.size(); ++other)
        {
            if (other == unit)
                continue;
            if ((other < unit) != (m_newPos[other] < m_newPos[unit]))
                out.push_back(other);
        }
        return out;
    }

    void appendSafety(Explanation &e, std::size_t unit) const
    {
        const std::vector<std::size_t> others = crossed(unit);
        if (others.empty())
            return;
        std::string names;
        constexpr std::size_t kMaxNamed = 4;
        for (std::size_t i = 0; i < others.size() && i < kMaxNamed; ++i)
        {
            names += (i > 0 ? ", " : "") + quoted(text(others[i]));
            e.relatedUnitIds.push_back(global(others[i]));
        }
        if (others.size() > kMaxNamed)
            names += " and " + std::to_string(others.size() - kMaxNamed) + " more";
        e.detail += " It crosses " + names +
                    ": there is no register or memory dependence between them, so the program's "
                    "result is unchanged.";
    }

    std::string producerText(const StallInfo &stall) const
    {
        return m_describer.textOf(stall.producerProgramIndex);
    }

    std::string whyStalled(std::size_t consumer, const StallInfo &stall) const
    {
        const std::string c = quoted(text(consumer));
        const std::string cm = quoted(mnemonic(text(consumer)));
        const std::string p = quoted(producerText(stall));
        const std::string reg = regName(stall.reg);
        const std::string cycles = plural(stall.cycles, "cycle");
        if (stall.spurious)
        {
            const InstructionFacts f = m_describer.facts(stall.stalledProgramIndex);
            const char *field = f.rs1Field == stall.reg.index ? "rs1" : "rs2";
            return "Kites' hazard unit compares raw register fields, so " + c +
                   " looks like it "
                   "reads " +
                   reg + " (its immediate occupies the " + field +
                   " bits). Kites "
                   "therefore stalled " +
                   cm + " behind " + p + " for " + cycles + ".";
        }
        if (m_options.stallModel == StallModelKind::CustomLatency)
            return c + " needs " + reg + " from " + p +
                   ". With the configured latencies it "
                   "waited " +
                   cycles + ".";
        if (stall.viaForwarding)
            return c + " needs " + reg + " from " + p +
                   ". A loaded value is only ready after "
                   "the MEM stage, so Kites stalled " +
                   cm + " for " + cycles + ".";
        return "Without forwarding, " + cm + " must wait until " + p + " writes " + reg +
               " back in WB (2 stall cycles when adjacent, 1 when one instruction apart). Kites "
               "stalled it for " +
               cycles + ".";
    }

    std::string savesSentence(unsigned saved) const
    {
        return saved > 0 ? " Saves " + plural(saved, "cycle") + "." : "";
    }

    Explanation fillerExplanation(std::size_t filler, std::size_t consumer, const StallInfo &stall,
                                  unsigned saved) const
    {
        Explanation e = base(filler);
        e.cyclesSaved = saved;
        const char *gap = stall.viaForwarding ? "load-use gap" : "stall slot";
        e.headline =
            "Moved " + quoted(text(filler)) + " " + direction(filler) + " to fill the " + gap;

        bool usesProducer = false;
        if (stall.producerProgramIndex != kNoUnit)
        {
            const InstructionFacts producer = m_describer.facts(stall.producerProgramIndex);
            for (std::size_t index : m_block.units[filler].programIndices)
                for (const RegId &reg : m_describer.facts(index).reads)
                    usesProducer = usesProducer || producer.writesReg(reg);
        }
        e.detail =
            whyStalled(consumer, stall) + " " + quoted(text(filler)) +
            (usesProducer ? " runs in that slot instead."
                          : " does not use the result of " + quoted(producerText(stall)) +
                                ", so it can run in that slot while the value is produced.") +
            savesSentence(saved);
        if (stall.producerUnit != kNoUnit)
            e.relatedUnitIds.push_back(global(stall.producerUnit));
        e.relatedUnitIds.push_back(global(consumer));
        appendSafety(e, filler);
        return e;
    }

    Explanation hoistExplanation(std::size_t producer, std::size_t consumer, unsigned saved) const
    {
        Explanation e = base(producer);
        e.cyclesSaved = saved;
        e.headline = "Moved " + quoted(text(producer)) + " " + direction(producer) +
                     " so its value is ready before " + quoted(text(consumer)) + " needs it";
        e.detail = whyStalled(consumer, m_block.originalStalls[consumer]) + savesSentence(saved);
        e.relatedUnitIds.push_back(global(consumer));
        appendSafety(e, producer);
        return e;
    }

    Explanation deferExplanation(std::size_t consumer, const StallInfo &stall, unsigned saved) const
    {
        Explanation e = base(consumer);
        e.cyclesSaved = saved;
        e.headline = "Moved " + quoted(text(consumer)) + " " + direction(consumer) +
                     ", after independent instructions, so the value from " +
                     quoted(producerText(stall)) + " is ready";
        e.detail = whyStalled(consumer, stall) + savesSentence(saved);
        if (stall.producerUnit != kNoUnit)
            e.relatedUnitIds.push_back(global(stall.producerUnit));
        appendSafety(e, consumer);
        return e;
    }

    const BasicBlockSchedule &m_block;
    std::size_t m_blockIndex;
    std::size_t m_firstUnit;
    const Describer &m_describer;
    const SchedulingOptions &m_options;
    std::vector<std::size_t> m_newPos;
    std::vector<bool> m_moved;
};

ListingRow makeRow(const Describer &describer, const ScheduleUnit &unit, std::size_t unitId,
                   std::size_t blockIndex, const StallInfo &stall)
{
    ListingRow row;
    row.unitId = unitId;
    row.blockIndex = blockIndex;
    row.sourceLine = unit.sourceLine;
    row.text = describer.textOfLine(unit.sourceLine);
    row.stallCycles = stall.cycles;
    row.stallReason = describer.stallReason(stall);
    row.pinned = unit.pinnedFirst || unit.pinnedLast;
    return row;
}
} // namespace

ProgramSuggestion SuggestionEngine::analyze(const std::string &sourceText,
                                            const AssembledProgram &program,
                                            const SchedulingOptions &options) const
{
    ProgramSuggestion s;
    s.options = options;
    s.schedule = BasicBlockScheduler{}.schedule(program, options);
    const std::vector<std::string> lines = splitLines(sourceText);
    const Describer describer(program, lines, options);

    std::set<unsigned> seenLines;
    bool linesUnique = true;
    for (std::size_t b = 0; b < s.schedule.blocks.size(); ++b)
    {
        const BasicBlockSchedule &block = s.schedule.blocks[b];
        const std::size_t firstUnit = s.units.size();
        for (std::size_t u = 0; u < block.units.size(); ++u)
        {
            const ScheduleUnit &unit = block.units[u];
            linesUnique = seenLines.insert(unit.sourceLine).second && linesUnique;
            s.originalListing.push_back(
                makeRow(describer, unit, firstUnit + u, b, block.originalStalls[u]));
            s.units.push_back(unit);
            s.unitBlock.push_back(b);
        }

        std::vector<std::size_t> identity(block.units.size());
        for (std::size_t i = 0; i < identity.size(); ++i)
            identity[i] = i;
        const std::vector<bool> moved = movedUnits(identity, block.scheduledUnitOrder);
        for (std::size_t k = 0; k < block.scheduledUnitOrder.size(); ++k)
        {
            const std::size_t u = block.scheduledUnitOrder[k];
            ListingRow row =
                makeRow(describer, block.units[u], firstUnit + u, b, block.scheduledStalls[u]);
            if (moved[u] && k != u)
            {
                row.move = k < u ? MoveKind::Up : MoveKind::Down;
                row.moveDistance = static_cast<int>(k < u ? u - k : k - u);
            }
            s.suggestedListing.push_back(std::move(row));
        }

        if (block.accepted)
        {
            BlockExplainer explainer(block, b, firstUnit, describer, options);
            for (Explanation &e : explainer.explain())
                s.explanations.push_back(std::move(e));
        }
    }

    s.originalStaticStalls = s.schedule.originalStalls;
    s.suggestedStaticStalls = s.schedule.scheduledStalls;
    s.originalStraightLineCycles = s.schedule.straightLineOriginalCycles;
    s.suggestedStraightLineCycles = s.schedule.straightLineScheduledCycles;

    if (!s.schedule.applicable)
    {
        s.status = ProgramSuggestion::Status::NotApplicable;
        s.statusMessage = s.schedule.notApplicableReason;
    }
    else if (s.schedule.changed())
    {
        s.status = ProgramSuggestion::Status::Improved;
        s.statusMessage = "Reordering removes " +
                          plural(s.originalStaticStalls - s.suggestedStaticStalls, "stall cycle") +
                          " (model, straight-line).";
    }
    else
    {
        s.status = ProgramSuggestion::Status::NoImprovement;
        s.statusMessage = "No safe reordering lowers the stall count.";
    }

    if (s.status != ProgramSuggestion::Status::Improved)
    {
        s.applyBlockedReason = "nothing to apply";
    }
    else if (!linesUnique)
    {
        s.applyBlockedReason = "internal error: two units share a source line";
    }
    else
    {
        const RewriteResult rewrite = rewriteSource(sourceText, s);
        s.canApply = rewrite.ok;
        s.rewrittenSource = rewrite.ok ? rewrite.text : std::string{};
        s.applyBlockedReason = rewrite.reason;
    }
    return s;
}

std::map<uint64_t, std::string>
SuggestionEngine::stallNotesByPc(const std::string &sourceText, const AssembledProgram &program,
                                 const SchedulingOptions &options) const
{
    std::map<uint64_t, std::string> notes;
    const SchedulingResult result = BasicBlockScheduler{}.schedule(program, options);
    const std::vector<std::string> lines = splitLines(sourceText);
    const Describer describer(program, lines, options);
    for (const BasicBlockSchedule &block : result.blocks)
    {
        for (const StallInfo &stall : block.originalStalls)
        {
            if (stall.cycles == 0 || stall.stalledProgramIndex == kNoUnit)
                continue;
            const uint64_t pc =
                uint64_t{4} *
                program.intermediate_code[stall.stalledProgramIndex].first.getInstructionIndex();
            notes[pc] =
                "Stalled " + plural(stall.cycles, "cycle") + ": " + describer.stallReason(stall);
        }
    }
    return notes;
}
} // namespace Kites
