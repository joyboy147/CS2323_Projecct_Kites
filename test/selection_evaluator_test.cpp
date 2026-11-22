/**
 * Tests of interactive per-block selection: whole-program evaluation of every subset of
 * accepted blocks, and the masked rewrite round-trip checked against a real Kites run.
 */
#include "scheduler/runtime/kites_trace_runner.h"
#include "scheduler/selection_evaluator.h"
#include "scheduler/source_rewriter.h"
#include "scheduler/suggestion_engine.h"
#include "scheduler_test_support.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

namespace Kites
{
namespace
{
using test_support::assembleSource;

// Three fall-through blocks, each with a load-use stall that a filler can hide.
const std::string kThreeBlocks = "    lui  x10, 0x10000\n"
                                 "    lw   x5, 0(x10)\n"
                                 "    add  x6, x5, x5\n"
                                 "    addi x7, x0, 1\n"
                                 "second:\n"
                                 "    lw   x8, 4(x10)\n"
                                 "    add  x9, x8, x8\n"
                                 "    addi x11, x0, 2\n"
                                 "third:\n"
                                 "    lw   x12, 8(x10)\n"
                                 "    add  x13, x12, x12\n"
                                 "    addi x14, x0, 5\n";
} // namespace

TEST(SelectionEvaluator, EverySubsetMatchesReanalysisAndKites)
{
    setupVmStateDirectory();
    const AssembledProgram program = assembleSource(kThreeBlocks);
    for (const bool forwarding : {true, false})
    {
        const SchedulingOptions options{.forwardingEnabled = forwarding};
        const ProgramSuggestion suggestion =
            SuggestionEngine{}.analyze(kThreeBlocks, program, options);
        ASSERT_EQ(suggestion.status, ProgramSuggestion::Status::Improved);
        const std::size_t blockCount = suggestion.schedule.blocks.size();
        ASSERT_GE(blockCount, 3U);

        const auto all =
            evaluateSelection(program, suggestion, std::vector<bool>(blockCount, true));
        EXPECT_EQ(all.stallCount, suggestion.suggestedStaticStalls);
        const auto none = evaluateSelection(program, suggestion, {});
        EXPECT_EQ(none.stallCount, suggestion.originalStaticStalls);

        KitesTraceRunner runner;
        for (unsigned mask = 1; mask < (1U << blockCount); ++mask)
        {
            SCOPED_TRACE("mask " + std::to_string(mask) + (forwarding ? " HF" : " HNF"));
            std::vector<bool> selected(blockCount);
            for (std::size_t b = 0; b < blockCount; ++b)
                selected[b] = (mask >> b) & 1U;
            const SelectionEvaluation evaluation = evaluateSelection(program, suggestion, selected);
            EXPECT_TRUE(evaluation.safe);
            const ProgramSuggestion masked = maskSuggestion(kThreeBlocks, suggestion, evaluation);
            const bool anyAccepted = std::any_of(
                evaluation.selected.begin(), evaluation.selected.end(), [](bool b) { return b; });
            if (!anyAccepted)
            {
                EXPECT_FALSE(masked.canApply);
                continue;
            }
            ASSERT_TRUE(masked.canApply) << masked.applyBlockedReason;
            const AssembledProgram rewritten = assembleSource(masked.rewrittenSource);
            const auto verification =
                verifyRewrittenProgram(program, rewritten, masked.rewrittenSource, masked);
            EXPECT_TRUE(verification.ok) << verification.reason;
            const TraceRunResult run =
                runner.run(rewritten, {forwarding ? ProcessorType::RV5Stage_H_F
                                                  : ProcessorType::RV5Stage_H_NF});
            ASSERT_TRUE(run.ok);
            EXPECT_EQ(run.trace.kitesCycleCount, evaluation.straightLineCycles);
            int deltaSum = 0;
            for (int delta : evaluation.perBlockDelta)
                deltaSum += delta;
            EXPECT_GE(deltaSum, 0);
        }
    }
}
} // namespace Kites
