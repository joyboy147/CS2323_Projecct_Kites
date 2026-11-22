/**
 * @file pipeline_instance_tracker.h
 * @brief Streams per-cycle stage snapshots into dynamic instruction instances.
 *
 * Shared by the pipeline diagram builder and the stall profiler so that both use the
 * same matching rule and can never disagree about what an instance is.
 */
#pragma once

#include "scheduler/pipeline_trace.h"

#include <climits>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Kites
{
/** @brief One dynamic instruction instance, reported when it leaves the pipeline. */
struct InstanceEvent
{
    enum class Kind
    {
        Retired,  ///< Reached WB.
        Squashed, ///< Left the pipeline before WB (flushed), or was still in flight at finish().
    };
    Kind kind{Kind::Retired};
    uint64_t instanceId{}; ///< Creation order, starting at 0.
    uint64_t pc{};
    unsigned fetchCycle{};           ///< 0-based trace index of the first cycle it was seen.
    unsigned firstIdCycle{UINT_MAX}; ///< UINT_MAX if it never reached ID.
    unsigned idCycles{};             ///< Cycles spent in ID (1 = no stall).
    unsigned wbCycle{};              ///< Retired only.
    unsigned lastCycle{};            ///< Last cycle it occupied a stage.
    PipeStage lastStage{PipeStage::IF};
    bool inFlightAtEnd{}; ///< Squashed only because the trace ended (finish()).
    /// Per ID cycle: {EX pc, MEM pc} of that cycle's snapshot (INVALID_PC for bubbles).
    std::vector<std::pair<uint64_t, uint64_t>> idContext;
};

/**
 * @brief Matches each cycle's stage PCs to instances: an instance advances from stage s-1,
 * else stays in stage s (a stall), else a new instance starts. O(1) memory per cycle.
 */
class PipelineInstanceTracker
{
  public:
    /** Called once per instance and cycle it occupies (for diagram cells). */
    using OccupancyFn = std::function<void(uint64_t instanceId, uint64_t pc, unsigned cycle,
                                           PipeStage stage, bool held)>;

    explicit PipelineInstanceTracker(std::function<void(const InstanceEvent &)> sink,
                                     OccupancyFn occupancy = {});

    /** Feed pre-step snapshots in order. */
    void onCycle(const CycleSnapshot &snapshot);
    /** Report still-live instances as Squashed with inFlightAtEnd (truncated run). */
    void finish();

    [[nodiscard]] unsigned cycleCount() const
    {
        return m_cycle;
    }

  private:
    struct Live
    {
        InstanceEvent event;
        std::size_t stage{};
    };

    void complete(Live &live, InstanceEvent::Kind kind, bool atEnd);

    std::function<void(const InstanceEvent &)> m_sink;
    OccupancyFn m_occupancy;
    std::vector<Live> m_live; ///< At most one per stage.
    unsigned m_cycle{};
    uint64_t m_nextId{};
};
} // namespace Kites
