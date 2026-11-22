#include "scheduler/selection_evaluator.h"

#include "scheduler/instruction_facts.h"
#include "scheduler/kites_hazard_model.h"
#include "scheduler/source_rewriter.h"

#include <set>

namespace Kites
{
namespace
{
constexpr unsigned kPipelineFillCycles = 4;

using PairSet = std::set<std::pair<std::size_t, std::size_t>>;

struct MixedCost
{
    unsigned stallCount{};
    PairSet quirkPairs;
};

MixedCost evaluateMixed(const std::vector<InstructionFacts> &facts,
                        const SchedulingResult &schedule, const std::vector<bool> &selected,
                        const SchedulingOptions &options)
{
    MixedCost cost;
    SlotHistory entry = bubbleHistory();
    for (std::size_t b = 0; b < schedule.blocks.size(); ++b)
    {
        const BasicBlockSchedule &block = schedule.blocks[b];
        const bool useScheduled = b < selected.size() && selected[b] && block.accepted;
        const auto &indices =
            useScheduled ? block.scheduledInstructionIndices : block.originalInstructionIndices;
        std::vector<IssueSlot> words;
        for (std::size_t index : indices)
            words.push_back({&facts[index]});
        const SequenceCost sequence = evaluateSequence(words, entry, options);
        cost.stallCount += sequence.stalls;
        const PairSet pairs = quirkPairSet(words, entry, options);
        cost.quirkPairs.insert(pairs.begin(), pairs.end());

        // Same context rule as the scheduler: nothing is in flight after jal/jalr.
        const bool fallsThrough =
            indices.empty() || !facts[block.originalInstructionIndices.back()].unconditionalJump;
        entry = fallsThrough ? sequence.exitHistory : bubbleHistory();
        for (IssueSlot &slot : entry)
            slot.unit = kNoUnit;
    }
    return cost;
}
} // namespace

SelectionEvaluation evaluateSelection(const AssembledProgram &program,
                                      const ProgramSuggestion &suggestion,
                                      const std::vector<bool> &selected)
{
    const SchedulingResult &schedule = suggestion.schedule;
    const SchedulingOptions &options = suggestion.options;
    std::vector<InstructionFacts> facts;
    facts.reserve(program.intermediate_code.size());
    for (std::size_t i = 0; i < program.intermediate_code.size(); ++i)
        facts.push_back(describeInstruction(program, i, options));

    SelectionEvaluation evaluation;
    evaluation.selected.assign(schedule.blocks.size(), false);
    for (std::size_t b = 0; b < schedule.blocks.size(); ++b)
        evaluation.selected[b] = b < selected.size() && selected[b] && schedule.blocks[b].accepted;

    const MixedCost original =
        evaluateMixed(facts, schedule, std::vector<bool>(schedule.blocks.size(), false), options);
    const MixedCost mixed = evaluateMixed(facts, schedule, evaluation.selected, options);
    evaluation.stallCount = mixed.stallCount;
    evaluation.straightLineCycles = facts.empty() ? 0
                                                  : static_cast<unsigned>(facts.size()) +
                                                        mixed.stallCount + kPipelineFillCycles;

    evaluation.perBlockDelta.assign(schedule.blocks.size(), 0);
    for (std::size_t b = 0; b < schedule.blocks.size(); ++b)
    {
        if (!evaluation.selected[b])
            continue;
        std::vector<bool> without = evaluation.selected;
        without[b] = false;
        evaluation.perBlockDelta[b] =
            static_cast<int>(evaluateMixed(facts, schedule, without, options).stallCount) -
            static_cast<int>(mixed.stallCount);
    }

    for (const auto &pair : mixed.quirkPairs)
    {
        if (original.quirkPairs.contains(pair))
            continue;
        const auto line = [&](std::size_t index)
        { return program.intermediate_code[index].first.getLineNumber(); };
        evaluation.safe = false;
        evaluation.unsafeReason = "line " + std::to_string(line(pair.first)) +
                                  " would be forwarded into line " +
                                  std::to_string(line(pair.second)) +
                                  " by a Kites forwarding quirk across a block boundary";
        break;
    }
    return evaluation;
}

ProgramSuggestion maskSuggestion(const std::string &sourceText, const ProgramSuggestion &suggestion,
                                 const SelectionEvaluation &evaluation)
{
    ProgramSuggestion masked = suggestion;
    bool any = false;
    for (std::size_t b = 0; b < masked.schedule.blocks.size(); ++b)
    {
        BasicBlockSchedule &block = masked.schedule.blocks[b];
        if (b < evaluation.selected.size() && evaluation.selected[b])
        {
            any = true;
            continue;
        }
        if (!block.accepted)
            continue;
        block.accepted = false;
        block.note = "not selected";
        block.scheduledInstructionIndices = block.originalInstructionIndices;
        for (std::size_t u = 0; u < block.scheduledUnitOrder.size(); ++u)
            block.scheduledUnitOrder[u] = u;
        block.scheduledStalls = block.originalStalls;
        block.scheduledCycleCount = block.originalCycleCount;
    }
    std::erase_if(masked.explanations,
                  [&](const Explanation &e) {
                      return e.blockIndex >= evaluation.selected.size() ||
                             !evaluation.selected[e.blockIndex];
                  });
    masked.schedule.scheduledStalls = evaluation.stallCount;
    masked.schedule.straightLineScheduledCycles = evaluation.straightLineCycles;
    masked.suggestedStaticStalls = evaluation.stallCount;
    masked.suggestedStraightLineCycles = evaluation.straightLineCycles;
    masked.canApply = false;
    masked.rewrittenSource.clear();
    if (!any)
    {
        masked.applyBlockedReason = "no block selected";
        return masked;
    }
    if (!evaluation.safe)
    {
        masked.applyBlockedReason = evaluation.unsafeReason;
        return masked;
    }
    const RewriteResult rewrite = rewriteSource(sourceText, masked);
    masked.canApply = rewrite.ok;
    masked.rewrittenSource = rewrite.ok ? rewrite.text : std::string{};
    masked.applyBlockedReason = rewrite.reason;
    return masked;
}
} // namespace Kites
