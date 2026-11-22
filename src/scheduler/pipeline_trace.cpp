#include "scheduler/pipeline_trace.h"

#include "scheduler/pipeline_instance_tracker.h"

#include <map>
#include <sstream>
#include <utility>

namespace Kites
{
namespace
{
std::string defaultLabel(uint64_t pc)
{
    std::ostringstream os;
    os << "0x" << std::hex << pc;
    return os.str();
}
} // namespace

const char *pipeStageName(PipeStage stage)
{
    static constexpr const char *names[] = {"IF", "ID", "EX", "MEM", "WB"};
    return names[static_cast<std::size_t>(stage)];
}

PipelineDiagram buildPipelineDiagram(const PipelineTrace &trace, const DiagramBuildOptions &options)
{
    PipelineDiagram diagram;
    const std::size_t cycleCount = trace.cycles.size();
    diagram.cycleCount = static_cast<unsigned>(cycleCount);

    // Cells are kept sparsely and only for instances that will get a row, so memory stays
    // bounded for long traces (a dense row per dynamic instance costs instances x cycles).
    std::map<uint64_t, std::vector<std::pair<unsigned, DiagramCell>>> cells;
    std::map<uint64_t, InstanceEvent> events;
    PipelineInstanceTracker tracker(
        [&](const InstanceEvent &event)
        {
            const unsigned stalls = event.idCycles > 0 ? event.idCycles - 1 : 0;
            diagram.totalStallCycles += stalls;
            const bool flushed =
                event.kind == InstanceEvent::Kind::Squashed && !event.inFlightAtEnd;
            if (flushed)
                ++diagram.flushedInstructionCount;
            if (event.instanceId < options.maxInstructionRows)
                events.emplace(event.instanceId, event);
            else
                diagram.truncatedRows = true;
        },
        [&](uint64_t id, uint64_t, unsigned cycle, PipeStage stage, bool held)
        {
            if (id < options.maxInstructionRows)
                cells[id].emplace_back(
                    cycle, DiagramCell{held ? CellKind::Stall : CellKind::Stage, stage});
        });
    for (const CycleSnapshot &snapshot : trace.cycles)
        tracker.onCycle(snapshot);
    tracker.finish();

    for (auto &[id, event] : events)
    {
        const unsigned stalls = event.idCycles > 0 ? event.idCycles - 1 : 0;
        const bool flushed = event.kind == InstanceEvent::Kind::Squashed && !event.inFlightAtEnd;
        if (options.includeBubbleRows)
        {
            for (unsigned k = 1; k <= stalls; ++k)
            {
                const std::size_t stallCycle = event.firstIdCycle + k;
                DiagramRow bubble;
                bubble.kind = DiagramRow::Kind::Bubble;
                bubble.label = "(bubble)";
                bubble.cells.assign(cycleCount, {});
                for (std::size_t j = 0; j < 3 && stallCycle + j < cycleCount; ++j)
                    bubble.cells[stallCycle + j] = {
                        CellKind::Bubble,
                        static_cast<PipeStage>(static_cast<std::size_t>(PipeStage::EX) + j)};
                diagram.rows.push_back(std::move(bubble));
            }
        }

        DiagramRow row;
        row.pc = event.pc;
        row.label = options.labelForPc ? options.labelForPc(event.pc) : defaultLabel(event.pc);
        row.stallCycles = stalls;
        if (stalls > 0 && options.stallNoteForPc)
            row.stallNote = options.stallNoteForPc(event.pc);
        row.flushed = flushed;
        row.cells.assign(cycleCount, {});
        for (const auto &[cycle, cell] : cells[id])
            row.cells[cycle] = cell;
        if (flushed && event.lastCycle + 1 < cycleCount)
            row.cells[event.lastCycle + 1] = {CellKind::Flushed, event.lastStage};
        diagram.rows.push_back(std::move(row));
        ++diagram.instructionRowCount;
    }
    return diagram;
}
} // namespace Kites
