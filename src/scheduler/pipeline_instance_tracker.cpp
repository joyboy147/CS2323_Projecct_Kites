#include "scheduler/pipeline_instance_tracker.h"

namespace Kites
{
namespace
{
constexpr uint64_t kEmpty = UINT64_MAX;
constexpr auto kId = static_cast<std::size_t>(PipeStage::ID);
constexpr auto kEx = static_cast<std::size_t>(PipeStage::EX);
constexpr auto kMem = static_cast<std::size_t>(PipeStage::MEM);
constexpr auto kWb = static_cast<std::size_t>(PipeStage::WB);
} // namespace

PipelineInstanceTracker::PipelineInstanceTracker(std::function<void(const InstanceEvent &)> sink,
                                                 OccupancyFn occupancy)
    : m_sink(std::move(sink)), m_occupancy(std::move(occupancy))
{
}

void PipelineInstanceTracker::complete(Live &live, InstanceEvent::Kind kind, bool atEnd)
{
    live.event.kind = kind;
    live.event.inFlightAtEnd = atEnd;
    if (m_sink)
        m_sink(live.event);
}

void PipelineInstanceTracker::onCycle(const CycleSnapshot &snapshot)
{
    const unsigned cycle = m_cycle++;
    std::vector<Live> previous = std::move(m_live);
    std::vector<bool> claimed(previous.size(), false);
    m_live.clear();

    auto find = [&](std::size_t stage, uint64_t pc) -> int
    {
        for (std::size_t i = 0; i < previous.size(); ++i)
            if (!claimed[i] && previous[i].stage == stage && previous[i].event.pc == pc)
                return static_cast<int>(i);
        return -1;
    };

    for (std::size_t s = kPipeStageCount; s-- > 0;)
    {
        const uint64_t pc = snapshot.pc[s];
        if (pc == kEmpty)
            continue;

        bool held = false;
        int index = s > 0 ? find(s - 1, pc) : -1;
        if (index < 0)
        {
            index = find(s, pc);
            held = index >= 0;
        }
        Live live;
        if (index >= 0)
        {
            claimed[static_cast<std::size_t>(index)] = true;
            live = std::move(previous[static_cast<std::size_t>(index)]);
        }
        else
        {
            live.event.instanceId = m_nextId++;
            live.event.pc = pc;
            live.event.fetchCycle = cycle;
        }

        live.stage = s;
        live.event.lastCycle = cycle;
        live.event.lastStage = static_cast<PipeStage>(s);
        if (s == kId)
        {
            if (live.event.firstIdCycle == UINT_MAX)
                live.event.firstIdCycle = cycle;
            ++live.event.idCycles;
            live.event.idContext.emplace_back(snapshot.pc[kEx], snapshot.pc[kMem]);
        }
        if (m_occupancy)
            m_occupancy(live.event.instanceId, pc, cycle, static_cast<PipeStage>(s), held);

        if (s == kWb)
        {
            live.event.wbCycle = cycle;
            complete(live, InstanceEvent::Kind::Retired, false);
        }
        else
        {
            m_live.push_back(std::move(live));
        }
    }

    for (std::size_t i = 0; i < previous.size(); ++i)
        if (!claimed[i])
            complete(previous[i], InstanceEvent::Kind::Squashed, false);
}

void PipelineInstanceTracker::finish()
{
    for (Live &live : m_live)
        complete(live, InstanceEvent::Kind::Squashed, true);
    m_live.clear();
}
} // namespace Kites
