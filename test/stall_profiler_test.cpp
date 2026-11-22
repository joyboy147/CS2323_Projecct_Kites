/**
 * Qt-free tests of the stall profiler on hand-written stage snapshots: the accounting
 * identity, cause classification, flush attribution, truncation, and agreement between
 * the instance tracker and the pipeline diagram.
 */
#include "processor/processor_constants.h"
#include "scheduler/pipeline_instance_tracker.h"
#include "scheduler/stall_profiler.h"
#include "scheduler_test_support.h"

#include <gtest/gtest.h>

namespace Kites
{
namespace
{
using namespace test_support;
using namespace test_support::enc;
constexpr uint64_t E = INVALID_PC;

std::size_t at(LostCycleCause cause)
{
    return static_cast<std::size_t>(cause);
}

/** lw x5; add x6,x5,x2; addi x7,x3,0 on HF: one load-use stall. */
struct LoadUseFixture
{
    ProgramBuilder builder;
    std::vector<CycleSnapshot> cycles{{{0, E, E, E, E}}, {{4, 0, E, E, E}}, {{8, 4, 0, E, E}},
                                      {{8, 4, E, 0, E}}, {{E, 8, 4, E, 0}}, {{E, E, 8, 4, E}},
                                      {{E, E, E, 8, 4}}, {{E, E, E, E, 8}}};
    LoadUseFixture()
    {
        builder.line("lw x5, 0(x1)", {lw(5, 0, 1)})
            .line("add x6, x5, x2", {add(6, 5, 2)})
            .line("addi x7, x3, 0", {addi(7, 3, 0)});
    }
};

StallProfile profile(const ProgramBuilder &builder, const std::vector<CycleSnapshot> &cycles,
                     ProcessorType type, const KitesCounters &counters, bool hitLimit = false)
{
    StallProfiler profiler(builder.program(), builder.source(), {type});
    for (const CycleSnapshot &snapshot : cycles)
        profiler.onCycle(snapshot);
    return profiler.finish(counters, hitLimit, false);
}
} // namespace

TEST(StallProfiler, LoadUseStallIsChargedToTheConsumer)
{
    const LoadUseFixture f;
    const StallProfile p = profile(f.builder, f.cycles, ProcessorType::RV5Stage_H_F, {8, 3, 0});
    EXPECT_TRUE(p.identityHolds()) << p.identityReport();
    EXPECT_EQ(p.retired, 3U);
    EXPECT_EQ(p.stallCycles, 1U);
    EXPECT_EQ(p.fillCycles, 4U);
    EXPECT_EQ(p.byCause[at(LostCycleCause::LoadUse)], 1U);
    ASSERT_EQ(p.lines.size(), 1U);
    EXPECT_EQ(p.lines[0].sourceLine, 2U);
    EXPECT_EQ(p.lines[0].text, "add x6, x5, x2");
    EXPECT_EQ(p.lines[0].dominantCause(), LostCycleCause::LoadUse);
    EXPECT_EQ(p.lines[0].producerLines.at(1), 1U);
}

TEST(StallProfiler, TakenBranchFlushIsChargedToTheBranch)
{
    ProgramBuilder b;
    b.line("beq x0, x0, target", {beq(0, 0, 16)})
        .line("addi x1, x0, 1", {addi(1, 0, 1)})
        .line("addi x2, x0, 2", {addi(2, 0, 2)})
        .line("addi x3, x0, 3", {addi(3, 0, 3)})
        .line("addi x4, x0, 4", {addi(4, 0, 4)});
    const std::vector<CycleSnapshot> cycles{
        {{0, E, E, E, E}},  {{4, 0, E, E, E}},  {{8, 4, 0, E, E}},
        {{12, 8, 4, 0, E}}, {{16, E, E, E, 0}}, {{E, 16, E, E, E}},
        {{E, E, 16, E, E}}, {{E, E, E, 16, E}}, {{E, E, E, E, 16}}};
    const StallProfile p = profile(b, cycles, ProcessorType::RV5Stage_H_F, {9, 2, 1});
    EXPECT_TRUE(p.identityHolds()) << p.identityReport();
    EXPECT_EQ(p.flushCycles, 3U);
    EXPECT_EQ(p.takenBranchCount, 1U);
    EXPECT_EQ(p.byCause[at(LostCycleCause::BranchFlush)], 3U);
    ASSERT_EQ(p.lines.size(), 1U);
    EXPECT_EQ(p.lines[0].sourceLine, 1U);
}

TEST(StallProfiler, TruncatedRunCountsTheTail)
{
    const LoadUseFixture f;
    const std::vector<CycleSnapshot> partial(f.cycles.begin(), f.cycles.begin() + 6);
    const StallProfile p =
        profile(f.builder, partial, ProcessorType::RV5Stage_H_F, {6, 1, 0}, true);
    EXPECT_TRUE(p.identityHolds()) << p.identityReport();
    EXPECT_EQ(p.retired, 1U);
    EXPECT_EQ(p.truncatedCycles, 1U);
    EXPECT_EQ(p.stallCycles, 0U); // the stalled add never retired
}

TEST(StallProfiler, BrokenIdentityIsReported)
{
    const LoadUseFixture f;
    const StallProfile p = profile(f.builder, f.cycles, ProcessorType::RV5Stage_H_F, {9, 3, 0});
    EXPECT_FALSE(p.identityHolds());
    EXPECT_NE(p.identityReport().find("Kites 9"), std::string::npos);
}

TEST(StallProfiler, TrackerAgreesWithDiagram)
{
    const LoadUseFixture f;
    std::vector<InstanceEvent> events;
    PipelineInstanceTracker tracker([&](const InstanceEvent &e) { events.push_back(e); });
    PipelineTrace trace;
    trace.cycles = f.cycles;
    for (const CycleSnapshot &snapshot : f.cycles)
        tracker.onCycle(snapshot);
    tracker.finish();
    const PipelineDiagram diagram = buildPipelineDiagram(trace, {});
    EXPECT_EQ(events.size(), diagram.instructionRowCount);
    unsigned stalls = 0;
    for (const InstanceEvent &e : events)
    {
        EXPECT_EQ(e.kind, InstanceEvent::Kind::Retired);
        stalls += e.idCycles - 1;
    }
    EXPECT_EQ(stalls, diagram.totalStallCycles);
}

TEST(StallProfiler, EveryCauseHasTextAndDataCausesAreFirst)
{
    for (std::size_t c = 0; c < kLostCycleCauseCount; ++c)
    {
        const auto cause = static_cast<LostCycleCause>(c);
        EXPECT_STRNE(causeLabel(cause), "?");
        EXPECT_STRNE(causeExplanation(cause), "");
    }
    EXPECT_TRUE(isDataStallCause(LostCycleCause::LoadUse));
    EXPECT_TRUE(isDataStallCause(LostCycleCause::SpuriousFpGprAlias));
    EXPECT_FALSE(isDataStallCause(LostCycleCause::BranchFlush));
}
} // namespace Kites
