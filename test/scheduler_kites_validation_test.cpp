/**
 * Validates the Phase-2 static model against real Kites runs (HF and HNF):
 * predicted cycles == Kites cycles for straight-line code, suggested programs are
 * architecturally equivalent, and the apply round-trip is verified and stable.
 */
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "scheduler/pipeline_trace.h"
#include "scheduler/runtime/kites_trace_runner.h"
#include "scheduler/source_rewriter.h"
#include "scheduler/suggestion_engine.h"
#include "scheduler_test_support.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <random>

namespace Kites
{
namespace
{
using test_support::assembleSource;

struct NamedProgram
{
    std::string name;
    std::string source;
};

std::string readExample(const std::string &name)
{
    std::ifstream in("../examples/" + name);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

SchedulingOptions optionsFor(bool forwarding)
{
    return {.forwardingEnabled = forwarding};
}

TraceRunOptions runOptionsFor(bool forwarding)
{
    return {forwarding ? ProcessorType::RV5Stage_H_F : ProcessorType::RV5Stage_H_NF};
}

KitesTraceRunner &runner()
{
    static KitesTraceRunner instance;
    return instance;
}

unsigned debugRunCycles(const AssembledProgram &program, bool forwarding)
{
    std::unique_ptr<ProcessorBase> processor;
    if (forwarding)
        processor = std::make_unique<RV5StageProcessorHF>();
    else
        processor = std::make_unique<RV5StageProcessorHNF>();
    processor->LoadProgram(program);
    std::streambuf *saved = std::cout.rdbuf(nullptr); // DebugRun prints every cycle
    processor->DebugRun();
    std::cout.rdbuf(saved);
    return processor->cycle_s_;
}

std::string randomProgram(std::mt19937 &rng)
{
    auto pick = [&](unsigned lo, unsigned hi)
    { return std::uniform_int_distribution<unsigned>(lo, hi)(rng); };
    auto reg = [&] { return "x" + std::to_string(pick(1, 9)); };
    std::string source =
        ".data\nscratch: .word 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16\n"
        ".text\n    lui x10, 0x10000\n";
    const unsigned length = pick(10, 30);
    for (unsigned i = 0; i < length; ++i)
    {
        const std::string offset = std::to_string(4 * pick(0, 15));
        switch (pick(0, 6))
        {
        case 0:
            source += "    add " + reg() + ", " + reg() + ", " + reg() + "\n";
            break;
        case 1:
            source += "    sub " + reg() + ", " + reg() + ", " + reg() + "\n";
            break;
        case 2:
            source +=
                "    addi " + reg() + ", " + reg() + ", " + std::to_string(pick(0, 40)) + "\n";
            break;
        case 3:
            source += "    lw " + reg() + ", " + offset + "(x10)\n";
            break;
        case 4:
            source += "    sw " + reg() + ", " + offset + "(x10)\n";
            break;
        case 5:
            source += "    mul " + reg() + ", " + reg() + ", " + reg() + "\n";
            break;
        default:
            source +=
                "    xori " + reg() + ", " + reg() + ", " + std::to_string(pick(0, 31)) + "\n";
            break;
        }
    }
    return source;
}

std::vector<NamedProgram> straightLinePrograms()
{
    std::vector<NamedProgram> programs;
    for (const char *name :
         {"scheduler_phase1/load_use_original.s", "scheduler_phase1/load_use_reordered.s",
          "scheduler_phase1/anti_dependency_safe.s", "scheduler_phase1/memory_order_safe.s",
          "scheduler_phase2/01_two_load_use.s", "scheduler_phase2/02_alu_chain_noforward.s",
          "scheduler_phase2/04_must_not_move.s", "scheduler_phase2/05_pseudo_and_layout.s",
          "scheduler_phase2/06_kites_hazard_quirk.s", "scheduler_phase2/07_matrix_2x2.s",
          "scheduler_phase2/08_dot_product.s"})
        programs.push_back({name, readExample(name)});
    std::mt19937 rng(2323);
    for (int i = 0; i < 40; ++i)
        programs.push_back({"random #" + std::to_string(i), randomProgram(rng)});
    return programs;
}

bool sameState(const TraceRunResult &a, const TraceRunResult &b)
{
    return a.gpr == b.gpr && a.fpr == b.fpr && a.dataMemory == b.dataMemory;
}

class SchedulerKitesValidation : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        setupVmStateDirectory();
    }
};
} // namespace

TEST_F(SchedulerKitesValidation, ModelMatchesKitesAndSuggestionsAreEquivalent)
{
    unsigned improvedRuns = 0;
    for (const NamedProgram &named : straightLinePrograms())
    {
        ASSERT_FALSE(named.source.empty()) << named.name;
        for (const bool forwarding : {true, false})
        {
            SCOPED_TRACE(named.name + (forwarding ? " (HF)" : " (HNF)"));
            const AssembledProgram program = assembleSource(named.source);
            const ProgramSuggestion s =
                SuggestionEngine{}.analyze(named.source, program, optionsFor(forwarding));

            const TraceRunResult original = runner().run(program, runOptionsFor(forwarding));
            ASSERT_TRUE(original.ok) << original.error;
            EXPECT_EQ(original.trace.cycles.size(), original.trace.kitesCycleCount);
            EXPECT_EQ(s.originalStraightLineCycles, original.trace.kitesCycleCount);
            EXPECT_EQ(debugRunCycles(program, forwarding), original.trace.kitesCycleCount);
            const PipelineDiagram diagram = buildPipelineDiagram(original.trace, {});
            EXPECT_EQ(diagram.cycleCount, original.trace.kitesCycleCount);
            EXPECT_EQ(diagram.totalStallCycles, s.originalStaticStalls);

            if (s.status != ProgramSuggestion::Status::Improved)
                continue;
            ++improvedRuns;
            ASSERT_TRUE(s.canApply) << s.applyBlockedReason;
            const AssembledProgram rewritten = assembleSource(s.rewrittenSource);
            const auto verification =
                verifyRewrittenProgram(program, rewritten, s.rewrittenSource, s);
            EXPECT_TRUE(verification.ok) << verification.reason;

            const TraceRunResult suggested = runner().run(rewritten, runOptionsFor(forwarding));
            ASSERT_TRUE(suggested.ok) << suggested.error;
            EXPECT_EQ(s.suggestedStraightLineCycles, suggested.trace.kitesCycleCount);
            EXPECT_LT(suggested.trace.kitesCycleCount, original.trace.kitesCycleCount);
            EXPECT_TRUE(sameState(original, suggested));

            // Applying is a fixed point: the rewritten program has nothing left to apply.
            const ProgramSuggestion again =
                SuggestionEngine{}.analyze(s.rewrittenSource, rewritten, optionsFor(forwarding));
            EXPECT_NE(again.status, ProgramSuggestion::Status::Improved);
            EXPECT_EQ(again.originalStaticStalls, s.suggestedStaticStalls);
        }
    }
    std::cout << "[ info     ] " << improvedRuns << " of 102 runs had an applicable suggestion\n";
    EXPECT_GE(improvedRuns, 40U);
}

TEST_F(SchedulerKitesValidation, LoopDemoSavesEveryIteration)
{
    constexpr unsigned kIterations = 8;
    const std::string source = readExample("scheduler_phase2/03_loop_sum.s");
    ASSERT_FALSE(source.empty());
    for (const bool forwarding : {true, false})
    {
        SCOPED_TRACE(forwarding ? "HF" : "HNF");
        const AssembledProgram program = assembleSource(source);
        const ProgramSuggestion s =
            SuggestionEngine{}.analyze(source, program, optionsFor(forwarding));
        ASSERT_EQ(s.status, ProgramSuggestion::Status::Improved);
        ASSERT_TRUE(s.canApply);
        const AssembledProgram rewritten = assembleSource(s.rewrittenSource);

        const TraceRunResult original = runner().run(program, runOptionsFor(forwarding));
        const TraceRunResult suggested = runner().run(rewritten, runOptionsFor(forwarding));
        ASSERT_TRUE(original.ok && suggested.ok);
        EXPECT_TRUE(sameState(original, suggested));
        EXPECT_EQ(original.gpr[6], 36U); // 1 + 2 + ... + 8
        const unsigned perIteration = s.originalStaticStalls - s.suggestedStaticStalls;
        EXPECT_EQ(original.trace.kitesCycleCount - suggested.trace.kitesCycleCount,
                  perIteration * kIterations);
        EXPECT_EQ(debugRunCycles(program, forwarding), original.trace.kitesCycleCount);
    }
}

TEST_F(SchedulerKitesValidation, TraceRunnerHandlesEcallAndCycleLimit)
{
    // The H_F/H_NF processors never call handle_syscall(), so ecall does not stop them:
    // the run drains normally and the instruction after the ecall still executes.
    const AssembledProgram withEcall =
        assembleSource("addi x5, x0, 7\naddi a7, x0, 10\necall\naddi x6, x0, 9\n");
    const TraceRunResult ecallRun = runner().run(withEcall, runOptionsFor(true));
    ASSERT_TRUE(ecallRun.ok) << ecallRun.error;
    EXPECT_FALSE(ecallRun.trace.stoppedByEcall);
    EXPECT_EQ(ecallRun.gpr[5], 7U);
    EXPECT_EQ(ecallRun.gpr[6], 9U);
    EXPECT_EQ(ecallRun.trace.kitesCycleCount, 8U);

    const AssembledProgram endless = assembleSource("addi x1, x0, 1\nloop:\nj loop\n");
    TraceRunOptions limited = runOptionsFor(true);
    limited.maxCycles = 500;
    const TraceRunResult limitRun = runner().run(endless, limited);
    ASSERT_TRUE(limitRun.ok);
    EXPECT_TRUE(limitRun.trace.hitCycleLimit);
    EXPECT_EQ(limitRun.trace.cycles.size(), 500U);

    TraceRunOptions unsupported;
    unsupported.processor = ProcessorType::RVSS; // single-cycle: no pipeline
    EXPECT_FALSE(runner().run(withEcall, unsupported).ok);
}

TEST_F(SchedulerKitesValidation, StallNotesNameTheProducer)
{
    const std::string source = readExample("scheduler_phase1/load_use_original.s");
    const AssembledProgram program = assembleSource(source);
    const auto notes = SuggestionEngine{}.stallNotesByPc(source, program, optionsFor(true));
    ASSERT_EQ(notes.size(), 1U);
    EXPECT_EQ(notes.begin()->first, 16U);
    EXPECT_NE(notes.begin()->second.find("lw x3, 0(x1)"), std::string::npos)
        << notes.begin()->second;
}

TEST_F(SchedulerKitesValidation, QuirkGuardComparesPairsNotCounts)
{
    // Review regression: with FP scheduling on, `flw f4` -> `sub x10, x5, x4` is a Q1 pair
    // (Kites forwards the FP load into x4) plus a spurious load-use stall. The old count-based
    // guard accepted an order that removed that pair but created `fadd.s f5` -> `sub` (x5),
    // which changed Kites' value of x10. The chosen order may only keep existing pairs.
    const std::string source = ".data\n"
                               "v: .word 0x40400000\n"
                               ".text\n"
                               "    li x9, 0x10000000\n"
                               "    li x5, 100\n"
                               "    li x4, 7\n"
                               "    fadd.s f5, f1, f2\n"
                               "    addi x2, x2, 0\n"
                               "    flw f4, 0(x9)\n"
                               "    sub x10, x5, x4\n";
    SchedulingOptions options = optionsFor(true);
    options.scheduleFloatingPoint = true;
    const AssembledProgram program = assembleSource(source);
    const ProgramSuggestion s = SuggestionEngine{}.analyze(source, program, options);
    ASSERT_TRUE(s.canApply) << s.applyBlockedReason;
    const AssembledProgram rewritten = assembleSource(s.rewrittenSource);
    const TraceRunResult before = runner().run(program, runOptionsFor(true));
    const TraceRunResult after = runner().run(rewritten, runOptionsFor(true));
    ASSERT_TRUE(before.ok && after.ok);
    EXPECT_EQ(before.gpr, after.gpr) << s.rewrittenSource;
    EXPECT_LT(after.trace.kitesCycleCount, before.trace.kitesCycleCount);
}
} // namespace Kites
