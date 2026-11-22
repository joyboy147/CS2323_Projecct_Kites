/**
 * Qt-free tests of the Phase-2 scheduler core: stall model, DAG legality, block
 * selection, moved-row detection, explanations, source rewriting and diagrams.
 */
#include "processor/processor_constants.h"
#include "scheduler/basic_block_scheduler.h"
#include "scheduler/kites_hazard_model.h"
#include "scheduler/pipeline_trace.h"
#include "scheduler/source_rewriter.h"
#include "scheduler/suggestion_engine.h"
#include "scheduler_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

namespace Kites
{
namespace
{
using namespace test_support;
using namespace test_support::enc;

const SchedulingOptions kHF{.forwardingEnabled = true};
const SchedulingOptions kHNF{.forwardingEnabled = false};

std::size_t positionOf(const std::vector<std::size_t> &order, std::size_t value)
{
    return static_cast<std::size_t>(std::find(order.begin(), order.end(), value) - order.begin());
}

bool hasEdge(const BasicBlockSchedule &block, std::size_t producer, std::size_t consumer,
             DependencyKind kind)
{
    return std::any_of(
        block.dependencies.begin(), block.dependencies.end(), [&](const DependencyEdge &e)
        { return e.producer == producer && e.consumer == consumer && e.kind == kind; });
}

ProgramBuilder rawProgram()
{
    ProgramBuilder b;
    b.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .line("add x6, x5, x2", {add(6, 5, 2)})
        .line("addi x7, x3, 0", {addi(7, 3, 0)});
    return b;
}
} // namespace

TEST(SchedulerCore, RawBlocksMove)
{
    const auto b = rawProgram();
    const auto result = BasicBlockScheduler{}.schedule(b.program(), kHF);
    ASSERT_EQ(result.blocks.size(), 1U);
    EXPECT_TRUE(result.blocks[0].accepted);
    EXPECT_EQ(result.blocks[0].scheduledInstructionIndices, (std::vector<std::size_t>{0, 2, 1}));
    EXPECT_EQ(result.originalStalls, 1U);
    EXPECT_EQ(result.scheduledStalls, 0U);
    EXPECT_EQ(result.straightLineOriginalCycles, 8U);
    EXPECT_EQ(result.straightLineScheduledCycles, 7U);
    EXPECT_TRUE(hasEdge(result.blocks[0], 0, 1, DependencyKind::ReadAfterWrite));
}

TEST(SchedulerCore, WarPreserved)
{
    ProgramBuilder b;
    b.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .line("add x6, x5, x2", {add(6, 5, 2)})
        .line("addi x2, x3, 1", {addi(2, 3, 1)});
    const auto result = BasicBlockScheduler{}.schedule(b.program(), kHF);
    EXPECT_FALSE(result.blocks[0].accepted);
    EXPECT_EQ(result.blocks[0].scheduledInstructionIndices, (std::vector<std::size_t>{0, 1, 2}));
    EXPECT_TRUE(hasEdge(result.blocks[0], 1, 2, DependencyKind::WriteAfterRead));
}

TEST(SchedulerCore, WawPreserved)
{
    ProgramBuilder b;
    b.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .line("add x6, x5, x2", {add(6, 5, 2)})
        .line("addi x6, x3, 1", {addi(6, 3, 1)});
    const auto result = BasicBlockScheduler{}.schedule(b.program(), kHF);
    EXPECT_FALSE(result.blocks[0].accepted);
    EXPECT_TRUE(hasEdge(result.blocks[0], 1, 2, DependencyKind::WriteAfterWrite));
}

TEST(SchedulerCore, MemoryOrderPreserved)
{
    // Reordering the two loads would save the load-use stall, but loads keep their order.
    ProgramBuilder loads;
    loads.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .line("lw x6, 0(x2)", {lw(6, 0, 2)})
        .line("add x7, x6, x6", {add(7, 6, 6)});
    const auto loadResult = BasicBlockScheduler{}.schedule(loads.program(), kHF);
    EXPECT_FALSE(loadResult.blocks[0].accepted);
    EXPECT_TRUE(hasEdge(loadResult.blocks[0], 0, 1, DependencyKind::MemoryOrder));

    ProgramBuilder mixed;
    mixed.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .line("sw x2, 0(x3)", {sw(2, 0, 3)})
        .line("sw x4, 4(x3)", {sw(4, 4, 3)})
        .line("add x6, x5, x4", {add(6, 5, 4)});
    const auto result = BasicBlockScheduler{}.schedule(mixed.program(), kHF);
    const auto &order = result.blocks[0].scheduledUnitOrder;
    EXPECT_LT(positionOf(order, 0), positionOf(order, 1));
    EXPECT_LT(positionOf(order, 1), positionOf(order, 2));
    EXPECT_TRUE(hasEdge(result.blocks[0], 0, 1, DependencyKind::MemoryOrder));
    EXPECT_TRUE(hasEdge(result.blocks[0], 1, 2, DependencyKind::MemoryOrder));
}

TEST(SchedulerCore, BarrierAndControlPinned)
{
    for (const uint32_t terminator : {beq(1, 2, 8), ecall(), jal(0, 8)})
    {
        ProgramBuilder b;
        b.line("lw x5, 0(x1)", {lw(5, 0, 1)})
            .line("add x6, x5, x2", {add(6, 5, 2)})
            .line("addi x7, x3, 0", {addi(7, 3, 0)})
            .line("end", {terminator})
            .line("addi x8, x0, 0", {addi(8, 0, 0)})
            .line("addi x9, x0, 0", {addi(9, 0, 0)});
        const auto result = BasicBlockScheduler{}.schedule(b.program(), kHF);
        ASSERT_GE(result.blocks.size(), 2U);
        EXPECT_EQ(result.blocks[0].scheduledInstructionIndices,
                  (std::vector<std::size_t>{0, 2, 1, 3}));
        EXPECT_TRUE(result.blocks[0].units.back().pinnedLast);
    }

    // A label on the consumer's own line pins it first, so the filler cannot move above it.
    ProgramBuilder pinned;
    pinned.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .labelOnNextLine("head")
        .line("add x6, x5, x2", {add(6, 5, 2)})
        .line("addi x7, x3, 0", {addi(7, 3, 0)});
    const auto pinnedResult = BasicBlockScheduler{}.schedule(pinned.program(), kHF);
    ASSERT_EQ(pinnedResult.blocks.size(), 2U);
    EXPECT_TRUE(pinnedResult.blocks[1].units[0].pinnedFirst);
    EXPECT_FALSE(pinnedResult.blocks[1].accepted);
    EXPECT_EQ(pinnedResult.originalStalls, 1U);
}

TEST(SchedulerCore, FpIsBarrierByDefault)
{
    ProgramBuilder b;
    b.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .line("flw f1, 0(x2)", {flw(1, 0, 2)})
        .line("fadd.s f2, f1, f3", {fadd_s(2, 1, 3)})
        .line("add x6, x5, x3", {add(6, 5, 3)});
    const auto result = BasicBlockScheduler{}.schedule(b.program(), kHF);
    ASSERT_EQ(result.blocks.size(), 3U);
    EXPECT_TRUE(result.blocks[0].units.back().pinnedLast);

    SchedulingOptions fp = kHF;
    fp.scheduleFloatingPoint = true;
    const auto fpResult = BasicBlockScheduler{}.schedule(b.program(), fp);
    ASSERT_EQ(fpResult.blocks.size(), 1U);
    const auto &edges = fpResult.blocks[0].dependencies;
    EXPECT_TRUE(std::any_of(edges.begin(), edges.end(),
                            [](const DependencyEdge &e)
                            {
                                return e.producer == 1 && e.consumer == 2 &&
                                       e.kind == DependencyKind::ReadAfterWrite &&
                                       e.reg == RegId{RegFile::Fpr, 1};
                            }));
}

TEST(SchedulerCore, PseudoGroupAtomic)
{
    ProgramBuilder b;
    b.line("lw x6, 0(x1)", {lw(6, 0, 1)})
        .line("add x7, x6, x2", {add(7, 6, 2)})
        .line("li x5, 0x12345", {lui(5, 0x12), addi(5, 5, 0x345)});
    const auto result = BasicBlockScheduler{}.schedule(b.program(), kHF);
    const auto &block = result.blocks[0];
    ASSERT_EQ(block.units.size(), 3U);
    EXPECT_EQ(block.units[2].programIndices, (std::vector<std::size_t>{2, 3}));
    EXPECT_TRUE(block.accepted);
    EXPECT_EQ(block.scheduledInstructionIndices, (std::vector<std::size_t>{0, 2, 3, 1}));
}

TEST(SchedulerCore, KitesSpuriousHazardModelled)
{
    // addi x7, x0, 3 has 3 in its rs2 bit field, so Kites' HDU sees a read of x3.
    ProgramBuilder alias;
    alias.line("lw x3, 0(x1)", {lw(3, 0, 1)}).line("addi x7, x0, 3", {addi(7, 0, 3)});
    const auto aliasResult = BasicBlockScheduler{}.schedule(alias.program(), kHF);
    EXPECT_EQ(aliasResult.originalStalls, 1U);
    EXPECT_TRUE(aliasResult.blocks[0].originalStalls[1].spurious);

    ProgramBuilder choice;
    choice.line("lw x3, 0(x1)", {lw(3, 0, 1)})
        .line("add x4, x3, x3", {add(4, 3, 3)})
        .line("addi x7, x0, 3", {addi(7, 0, 3)})
        .line("addi x8, x0, 5", {addi(8, 0, 5)});
    const auto result = BasicBlockScheduler{}.schedule(choice.program(), kHF);
    EXPECT_EQ(result.scheduledStalls, 0U);
    EXPECT_EQ(result.blocks[0].scheduledUnitOrder[1], 3U); // the non-aliasing filler
}

TEST(SchedulerCore, NoForwardingDistances)
{
    auto stallsAtDistance = [](unsigned gap)
    {
        ProgramBuilder b;
        b.line("addi x1, x0, 5", {addi(1, 0, 5)});
        for (unsigned i = 0; i < gap; ++i)
            b.line("addi x9, x0, 0", {addi(9, 0, 0)});
        b.line("add x2, x1, x1", {add(2, 1, 1)});
        SchedulingOptions options = kHNF;
        options.exhaustiveSearchLimit = 0;
        return BasicBlockScheduler{}.schedule(b.program(), options).originalStalls;
    };
    EXPECT_EQ(stallsAtDistance(0), 2U);
    EXPECT_EQ(stallsAtDistance(1), 1U);
    EXPECT_EQ(stallsAtDistance(2), 0U);
}

TEST(SchedulerCore, ForwardingOnlyLoadUse)
{
    ProgramBuilder b;
    b.line("addi x1, x0, 5", {addi(1, 0, 5)})
        .line("add x2, x1, x1", {add(2, 1, 1)})
        .line("add x3, x2, x2", {add(3, 2, 2)});
    EXPECT_EQ(BasicBlockScheduler{}.schedule(b.program(), kHF).originalStalls, 0U);
    EXPECT_EQ(BasicBlockScheduler{}.schedule(b.program(), kHNF).originalStalls, 4U);
}

TEST(SchedulerCore, EntryContextAcrossLabel)
{
    ProgramBuilder fallThrough;
    fallThrough.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .label("next")
        .line("add x6, x5, x2", {add(6, 5, 2)});
    EXPECT_EQ(BasicBlockScheduler{}.schedule(fallThrough.program(), kHF).originalStalls, 1U);

    ProgramBuilder afterJump;
    afterJump.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .line("jal x0, next", {jal(0, 4)})
        .label("next")
        .line("add x6, x5, x2", {add(6, 5, 2)});
    EXPECT_EQ(BasicBlockScheduler{}.schedule(afterJump.program(), kHF).originalStalls, 0U);

    // With the label on its own line the filler may move to the top of the second block.
    ProgramBuilder movable;
    movable.line("lw x5, 0(x1)", {lw(5, 0, 1)})
        .label("next")
        .line("add x6, x5, x2", {add(6, 5, 2)})
        .line("addi x7, x3, 0", {addi(7, 3, 0)});
    const auto result = BasicBlockScheduler{}.schedule(movable.program(), kHF);
    EXPECT_TRUE(result.blocks[1].accepted);
    EXPECT_EQ(result.scheduledStalls, 0U);
}

TEST(SchedulerCore, ExitCouplingPreventsRegression)
{
    // Moving addi x7 up saves a stall inside block A but adds one at block B's head.
    ProgramBuilder b;
    b.line("addi x5, x0, 0", {addi(5, 0, 0)})
        .line("add x6, x5, x5", {add(6, 5, 5)})
        .line("addi x7, x10, 0", {addi(7, 10, 0)})
        .label("b")
        .line("add x8, x6, x6", {add(8, 6, 6)});
    const auto result = BasicBlockScheduler{}.schedule(b.program(), kHNF);
    ASSERT_EQ(result.blocks.size(), 2U);
    EXPECT_FALSE(result.blocks[0].accepted);
    EXPECT_EQ(result.blocks[0].originalExitStalls, 1U);
    EXPECT_EQ(result.originalStalls, 3U);
    EXPECT_EQ(result.scheduledStalls, 3U);
}

TEST(SchedulerCore, NeverWorse)
{
    std::mt19937 rng(20251117);
    auto pick = [&](unsigned lo, unsigned hi)
    { return std::uniform_int_distribution<unsigned>(lo, hi)(rng); };
    for (int trial = 0; trial < 500; ++trial)
    {
        ProgramBuilder b;
        const unsigned length = pick(3, 12);
        for (unsigned i = 0; i < length; ++i)
        {
            const uint32_t rd = pick(1, 8), rs1 = pick(1, 8), rs2 = pick(1, 8);
            switch (pick(0, 5))
            {
            case 0:
                b.line("add", {add(rd, rs1, rs2)});
                break;
            case 1:
                b.line("addi", {addi(rd, rs1, static_cast<int32_t>(pick(0, 40)))});
                break;
            case 2:
                b.line("lw", {lw(rd, static_cast<int32_t>(4 * pick(0, 3)), 10)});
                break;
            case 3:
                b.line("sw", {sw(rs2, static_cast<int32_t>(4 * pick(0, 3)), 10)});
                break;
            case 4:
                b.line("mul", {mul(rd, rs1, rs2)});
                break;
            default:
                b.line("lui", {lui(rd, pick(0, 31))});
                break;
            }
        }
        const SchedulingOptions options = trial % 2 ? kHF : kHNF;
        const auto result = BasicBlockScheduler{}.schedule(b.program(), options);
        ASSERT_LE(result.scheduledStalls, result.originalStalls) << "trial " << trial;
        for (const auto &block : result.blocks)
        {
            auto order = block.scheduledUnitOrder;
            std::sort(order.begin(), order.end());
            for (std::size_t i = 0; i < order.size(); ++i)
                ASSERT_EQ(order[i], i);
            for (const auto &edge : block.dependencies)
                ASSERT_LT(positionOf(block.scheduledUnitOrder, edge.producer),
                          positionOf(block.scheduledUnitOrder, edge.consumer))
                    << "trial " << trial;
        }
    }
}

TEST(SchedulerCore, Deterministic)
{
    const auto b = rawProgram();
    const auto first = BasicBlockScheduler{}.schedule(b.program(), kHNF);
    const auto second = BasicBlockScheduler{}.schedule(b.program(), kHNF);
    ASSERT_EQ(first.blocks.size(), second.blocks.size());
    for (std::size_t i = 0; i < first.blocks.size(); ++i)
        EXPECT_EQ(first.blocks[i].scheduledInstructionIndices,
                  second.blocks[i].scheduledInstructionIndices);
}

TEST(SchedulerCore, NotApplicableWithoutHazardDetection)
{
    const auto b = rawProgram();
    const auto result = BasicBlockScheduler{}.schedule(
        b.program(), {.forwardingEnabled = true, .hazardDetectionEnabled = false});
    EXPECT_FALSE(result.applicable);
    EXPECT_FALSE(result.notApplicableReason.empty());
    EXPECT_EQ(result.blocks[0].scheduledInstructionIndices, (std::vector<std::size_t>{0, 1, 2}));
    EXPECT_FALSE(result.changed());

    const auto suggestion = SuggestionEngine{}.analyze(
        b.source(), b.program(), {.forwardingEnabled = true, .hazardDetectionEnabled = false});
    EXPECT_EQ(suggestion.status, ProgramSuggestion::Status::NotApplicable);
    EXPECT_FALSE(suggestion.canApply);
}

TEST(SchedulerCore, QuirkGuard)
{
    // Any order that hides the load-use stall puts add x6 (reads x5) within forwarding
    // distance of fadd.s f5, which Kites would wrongly forward into the integer ALU.
    auto build = [](uint32_t first)
    {
        ProgramBuilder b;
        b.line("first", {first})
            .line("lw x7, 0(x2)", {lw(7, 0, 2)})
            .line("add x8, x7, x3", {add(8, 7, 3)})
            .line("add x6, x5, x1", {add(6, 5, 1)});
        return b;
    };
    const auto guarded = BasicBlockScheduler{}.schedule(build(fadd_s(5, 1, 2)).program(), kHF);
    ASSERT_EQ(guarded.blocks.size(), 2U);
    EXPECT_FALSE(guarded.blocks[1].accepted);

    const auto free = BasicBlockScheduler{}.schedule(build(addi(9, 0, 0)).program(), kHF);
    EXPECT_TRUE(free.blocks[0].accepted);
}

TEST(SchedulerCore, CustomLatencyModel)
{
    ProgramBuilder b;
    b.line("mul x5, x1, x2", {mul(5, 1, 2)}).line("add x6, x5, x3", {add(6, 5, 3)});
    SchedulingOptions options = kHF;
    options.stallModel = StallModelKind::CustomLatency;
    options.latencies.mul = 3;
    EXPECT_EQ(BasicBlockScheduler{}.schedule(b.program(), options).originalStalls, 2U);
    EXPECT_EQ(BasicBlockScheduler{}.schedule(b.program(), kHF).originalStalls, 0U);
}

TEST(SchedulerCore, LcsMovedRows)
{
    const auto simple = movedUnits({0, 1, 2}, {0, 2, 1});
    EXPECT_EQ(simple, (std::vector<bool>{false, false, true}));

    const auto mixed = movedUnits({0, 1, 2, 3, 4, 5}, {1, 2, 0, 3, 5, 4});
    EXPECT_EQ(mixed, (std::vector<bool>{true, false, false, false, false, true}));

    const auto b = rawProgram();
    const auto suggestion = SuggestionEngine{}.analyze(b.source(), b.program(), kHF);
    ASSERT_EQ(suggestion.suggestedListing.size(), 3U);
    EXPECT_EQ(suggestion.suggestedListing[1].text, "addi x7, x3, 0");
    EXPECT_EQ(suggestion.suggestedListing[1].move, MoveKind::Up);
    EXPECT_EQ(suggestion.suggestedListing[1].moveDistance, 1);
    EXPECT_EQ(suggestion.suggestedListing[2].move, MoveKind::None);
}

TEST(SchedulerCore, ExplanationText)
{
    const auto b = rawProgram();
    const auto forwarding = SuggestionEngine{}.analyze(b.source(), b.program(), kHF);
    ASSERT_EQ(forwarding.status, ProgramSuggestion::Status::Improved);
    ASSERT_FALSE(forwarding.explanations.empty());
    const Explanation &e = forwarding.explanations.front();
    const std::string text = e.headline + " " + e.detail;
    for (const char *needle :
         {"addi x7, x3, 0", "lw x5, 0(x1)", "add x6, x5, x2", "x5", "Saves 1 cycle", "MEM"})
        EXPECT_NE(text.find(needle), std::string::npos) << needle << " in: " << text;
    EXPECT_EQ(e.cyclesSaved, 1U);
    EXPECT_EQ(forwarding.originalListing[1].stallReason,
              "load-use on x5 from `lw x5, 0(x1)` (line 1)");

    const auto noForwarding = SuggestionEngine{}.analyze(b.source(), b.program(), kHNF);
    ASSERT_FALSE(noForwarding.explanations.empty());
    const std::string nf =
        noForwarding.explanations.front().headline + noForwarding.explanations.front().detail;
    EXPECT_NE(nf.find("WB"), std::string::npos) << nf;
}

TEST(SchedulerCore, RewritePermutesLinesOnly)
{
    const std::string source = "# demo\n"
                               ".data\n"
                               "value: .word 7\n"
                               ".text\n"
                               "main:\n"
                               "    lw   x5, 0(x1)    # load\n"
                               "\n"
                               "    add  x6, x5, x2   # use\n"
                               "    # a comment between instructions\n"
                               "    addi x7, x3, 0    # filler\n";
    const AssembledProgram program = assembleSource(source);
    const auto suggestion = SuggestionEngine{}.analyze(source, program, kHF);
    ASSERT_EQ(suggestion.status, ProgramSuggestion::Status::Improved);
    ASSERT_TRUE(suggestion.canApply) << suggestion.applyBlockedReason;
    EXPECT_EQ(suggestion.rewrittenSource, "# demo\n"
                                          ".data\n"
                                          "value: .word 7\n"
                                          ".text\n"
                                          "main:\n"
                                          "    lw   x5, 0(x1)    # load\n"
                                          "\n"
                                          "    addi x7, x3, 0    # filler\n"
                                          "    # a comment between instructions\n"
                                          "    add  x6, x5, x2   # use\n");

    const AssembledProgram rewritten = assembleSource(suggestion.rewrittenSource);
    const auto verification =
        verifyRewrittenProgram(program, rewritten, suggestion.rewrittenSource, suggestion);
    EXPECT_TRUE(verification.ok) << verification.reason;
}

TEST(SchedulerCore, RewriteRefusesDirectiveInsideBlock)
{
    const std::string assembled = "lw x5, 0(x1)\n\nadd x6, x5, x2\naddi x7, x3, 0\n";
    const AssembledProgram program = assembleSource(assembled);
    const auto suggestion = SuggestionEngine{}.analyze(assembled, program, kHF);
    ASSERT_TRUE(suggestion.canApply);

    const std::string withDirective = "lw x5, 0(x1)\n.align 2\nadd x6, x5, x2\naddi x7, x3, 0\n";
    const RewriteResult refused = rewriteSource(withDirective, suggestion);
    EXPECT_FALSE(refused.ok);
    EXPECT_NE(refused.reason.find("directive"), std::string::npos);
}

TEST(SchedulerCore, DiagramFromSyntheticTrace)
{
    constexpr uint64_t E = INVALID_PC;
    // lw x5; add x6,x5,x2; addi x7,x3,0 on the HF pipeline (one load-use stall).
    PipelineTrace trace;
    trace.cycles = {{{0, E, E, E, E}}, {{4, 0, E, E, E}}, {{8, 4, 0, E, E}}, {{8, 4, E, 0, E}},
                    {{E, 8, 4, E, 0}}, {{E, E, 8, 4, E}}, {{E, E, E, 8, 4}}, {{E, E, E, E, 8}}};
    const PipelineDiagram diagram = buildPipelineDiagram(trace, {});
    EXPECT_EQ(diagram.cycleCount, 8U);
    EXPECT_EQ(diagram.totalStallCycles, 1U);
    EXPECT_EQ(diagram.instructionRowCount, 3U);
    ASSERT_EQ(diagram.rows.size(), 4U);
    const DiagramRow &bubble = diagram.rows[1];
    const DiagramRow &consumer = diagram.rows[2];
    EXPECT_EQ(bubble.kind, DiagramRow::Kind::Bubble);
    EXPECT_EQ(consumer.pc, 4U);
    EXPECT_EQ(consumer.stallCycles, 1U);
    EXPECT_EQ(consumer.cells[3].kind, CellKind::Stall);
    EXPECT_EQ(consumer.cells[3].stage, PipeStage::ID);
    EXPECT_EQ(bubble.cells[3].kind, CellKind::Bubble);
    EXPECT_EQ(bubble.cells[3].stage, PipeStage::EX);
    EXPECT_EQ(bubble.cells[4].stage, PipeStage::MEM);
    EXPECT_EQ(bubble.cells[5].stage, PipeStage::WB);

    // A taken branch at 0 resolved in MEM squashes the three younger instructions.
    PipelineTrace flush;
    flush.cycles = {{{0, E, E, E, E}},   {{4, 0, E, E, E}},   {{8, 4, 0, E, E}},
                    {{12, 8, 4, 0, E}},  {{20, E, E, E, 0}},  {{24, 20, E, E, E}},
                    {{E, 24, 20, E, E}}, {{E, E, 24, 20, E}}, {{E, E, E, 24, 20}},
                    {{E, E, E, E, 24}}};
    const PipelineDiagram flushed = buildPipelineDiagram(flush, {});
    EXPECT_EQ(flushed.flushedInstructionCount, 3U);
    EXPECT_EQ(flushed.totalStallCycles, 0U);
    ASSERT_GE(flushed.rows.size(), 4U);
    EXPECT_TRUE(flushed.rows[1].flushed);
    EXPECT_EQ(flushed.rows[1].cells[4].kind, CellKind::Flushed);
}

TEST(SchedulerCore, DiagramOfLongLoopTraceStaysSmall)
{
    // Review regression: a loop that runs to the cycle limit has one instance per dynamic
    // instruction. Rows used to be allocated densely (instances x cycles cells) before the
    // row limit applied; this trace would have needed ~1.6 GB that way.
    constexpr uint64_t E = INVALID_PC;
    constexpr std::size_t kCycles = 20000;
    PipelineTrace trace;
    for (std::size_t c = 0; c < kCycles; ++c)
    {
        CycleSnapshot snapshot{};
        for (std::size_t s = 0; s < kPipeStageCount; ++s)
            snapshot.pc[s] = c >= s ? ((c - s) % 3) * 4 : E; // a 3-instruction loop body
        trace.cycles.push_back(snapshot);
    }
    DiagramBuildOptions options;
    options.maxInstructionRows = 10;
    const PipelineDiagram diagram = buildPipelineDiagram(trace, options);
    EXPECT_EQ(diagram.cycleCount, kCycles);
    EXPECT_TRUE(diagram.truncatedRows);
    ASSERT_EQ(diagram.rows.size(), 10U);
    for (std::size_t r = 0; r < diagram.rows.size(); ++r)
    {
        const DiagramRow &row = diagram.rows[r];
        ASSERT_EQ(row.cells.size(), kCycles);
        EXPECT_EQ(row.pc, (r % 3) * 4);
        for (std::size_t s = 0; s < kPipeStageCount; ++s)
        {
            EXPECT_EQ(row.cells[r + s].kind, CellKind::Stage);
            EXPECT_EQ(row.cells[r + s].stage, static_cast<PipeStage>(s));
        }
        EXPECT_EQ(row.cells[r + kPipeStageCount].kind, CellKind::Empty);
    }
}

TEST(SchedulerCore, LoneAuipcKeepsItsPosition)
{
    // Review regression: `auipc x5, 0` filled the load-use gap, moving up one row, which
    // changes x5 from 12 to 8 (Kites' 5-stage auipc returns 0, so its equivalence check
    // could not notice). A lone auipc must not move and nothing may cross it.
    const std::string source = ".data\n"
                               "v: .word 5\n"
                               ".text\n"
                               "    li x20, 0x10000000\n"
                               "    lw x6, 0(x20)\n"
                               "    add x7, x6, x6\n"
                               "    auipc x5, 0\n";
    const AssembledProgram program = assembleSource(source);
    const SchedulingResult result = BasicBlockScheduler{}.schedule(program, kHF);
    ASSERT_EQ(result.blocks.size(), 1U);
    const BasicBlockSchedule &block = result.blocks[0];
    EXPECT_FALSE(block.accepted);
    EXPECT_EQ(block.scheduledUnitOrder, (std::vector<std::size_t>{0, 1, 2, 3}));

    // `la` is re-encoded for its new position on reassembly, so it may still fill the gap.
    const std::string withLa = ".data\n"
                               "v: .word 5\n"
                               ".text\n"
                               "    li x20, 0x10000000\n"
                               "    lw x6, 0(x20)\n"
                               "    add x7, x6, x6\n"
                               "    la x5, v\n";
    EXPECT_TRUE(BasicBlockScheduler{}.schedule(assembleSource(withLa), kHF).blocks[0].accepted);
}
} // namespace Kites
