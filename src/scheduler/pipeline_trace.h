/**
 * @file pipeline_trace.h
 * @brief Per-cycle stage occupancy recorded from a real Kites run, and the Qt-free
 *        builder that turns it into an instruction x cycle pipeline diagram.
 */
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Kites
{
enum class PipeStage : uint8_t
{
    IF = 0,
    ID,
    EX,
    MEM,
    WB
};

inline constexpr std::size_t kPipeStageCount = 5;

const char *pipeStageName(PipeStage stage);

/** PCs occupying IF/ID/EX/MEM/WB during one cycle; UINT64_MAX (INVALID_PC) = empty/bubble. */
struct CycleSnapshot
{
    std::array<uint64_t, kPipeStageCount> pc;
};

/** @brief Stage occupancy of a real Kites run, one snapshot per cycle. */
struct PipelineTrace
{
    std::vector<CycleSnapshot> cycles; ///< cycles[c] = occupancy during cycle c+1.
    uint64_t programSizeBytes{};
    unsigned kitesCycleCount{}; ///< Processor cycle counter at the end of the run.
    unsigned instructionsRetired{};
    bool hitCycleLimit{};
    bool stoppedByEcall{};
};

enum class CellKind : uint8_t
{
    Empty,
    Stage,
    Stall,
    Bubble,
    Flushed
};

/** @brief One cell of the pipeline diagram. */
struct DiagramCell
{
    CellKind kind{CellKind::Empty};
    PipeStage stage{};
};

/** @brief One diagram row: a dynamic instruction instance or an inserted bubble. */
struct DiagramRow
{
    enum class Kind
    {
        Instruction,
        Bubble
    } kind{Kind::Instruction};
    uint64_t pc{UINT64_MAX};
    std::string label;      ///< "L8  add x6, x3, x1" or "(bubble)".
    std::string stallNote;  ///< Tooltip for this row's Stall cells.
    unsigned stallCycles{}; ///< Repeated-ID cycles.
    bool flushed{};
    std::vector<DiagramCell> cells; ///< size == cycleCount.
};

/** @brief Instruction x cycle diagram built from a PipelineTrace. */
struct PipelineDiagram
{
    unsigned cycleCount{};
    unsigned totalStallCycles{};
    unsigned flushedInstructionCount{};
    unsigned instructionRowCount{};
    std::vector<DiagramRow> rows;
    bool truncatedRows{};
};

/** @brief Row limit, bubble rows and label / tooltip callbacks for buildPipelineDiagram(). */
struct DiagramBuildOptions
{
    std::size_t maxInstructionRows{80};
    bool includeBubbleRows{true};
    std::function<std::string(uint64_t pc)> labelForPc;
    std::function<std::string(uint64_t pc)> stallNoteForPc;
};

/** Tracks dynamic instruction instances through the snapshots (see PHASE2 design §4.1). */
PipelineDiagram buildPipelineDiagram(const PipelineTrace &trace,
                                     const DiagramBuildOptions &options);
} // namespace Kites
