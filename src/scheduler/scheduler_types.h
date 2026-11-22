/**
 * @file scheduler_types.h
 * @brief Options and result types shared by the Qt-free instruction scheduler.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Kites
{
enum class RegFile : uint8_t
{
    Gpr,
    Fpr
};

/** @brief A register: file (GPR/FPR) and index. */
struct RegId
{
    RegFile file{RegFile::Gpr};
    uint8_t index{};

    auto operator<=>(const RegId &) const = default;
};

/** "x5" / "f2" (ABI-free register name used in explanations). */
std::string regName(const RegId &reg);

/** @brief Stall model: the exact Kites HDU replica, or model-only custom latencies. */
enum class StallModelKind
{
    KitesExact,
    CustomLatency
};

/** Minimum issue distance (in issue slots) producer -> consumer for a true RAW dependency. */
struct LatencySettings
{
    unsigned loadUse{2}; ///< Forwarding: load -> consumer (Kites: 2, i.e. 1 stall when adjacent).
    unsigned alu{1};     ///< Forwarding: ALU -> consumer (Kites: 1).
    unsigned mul{1};     ///< Forwarding: MUL* -> consumer (Kites: 1).
    unsigned div{1};     ///< Forwarding: DIV/REM -> consumer (Kites: 1).
    unsigned noForwarding{3}; ///< No forwarding: any producer -> consumer (Kites: 3).
};

/** The first two members keep their Phase-1 order so designated initialisers still work. */
struct SchedulingOptions
{
    bool forwardingEnabled{true};
    bool hazardDetectionEnabled{true};
    StallModelKind stallModel{StallModelKind::KitesExact};
    LatencySettings latencies{};
    bool scheduleFloatingPoint{false}; ///< FP/D instructions are block barriers unless set.
    std::size_t exhaustiveSearchLimit{
        8}; ///< Blocks with at most this many movable units get an exact search.
};

/** @brief Why one unit must stay before another. */
enum class DependencyKind
{
    ReadAfterWrite,
    WriteAfterRead,
    WriteAfterWrite,
    MemoryOrder,
    Barrier
};

/** @brief One ordering constraint between two units of a block. */
struct DependencyEdge
{
    std::size_t
        producer{}; ///< Unit index within the block (== instruction index for 1 instr/line).
    std::size_t consumer{};
    DependencyKind kind{};
    unsigned int latency{}; ///< Priority weight only (critical path); legality ignores it.
    RegId reg{};            ///< Meaningful for RAW/WAR/WAW.
};

inline constexpr std::size_t kNoUnit = std::numeric_limits<std::size_t>::max();

/** Why a unit waits before issuing (model verdict). */
struct StallInfo
{
    unsigned cycles{}; ///< Bubbles before this unit's words (summed over its words).
    std::size_t producerUnit{kNoUnit}; ///< Unit in this block, or kNoUnit if outside the block.
    std::size_t producerProgramIndex{kNoUnit}; ///< Index into intermediate_code.
    std::size_t stalledProgramIndex{kNoUnit};  ///< The word of this unit that waited.
    RegId reg{};
    bool viaForwarding{}; ///< True: load-use with forwarding. False: no-forwarding write-back wait.
    bool spurious{};      ///< The HDU matched a raw field that is not an architectural read.
};

/** @brief The scheduling unit: all machine words of one source line. */
struct ScheduleUnit
{
    std::vector<std::size_t>
        programIndices;    ///< Into intermediate_code, contiguous, same line_number.
    unsigned sourceLine{}; ///< 1-based.
    bool pinnedFirst{};    ///< A label is defined on this line (or a lone auipc, see below).
    bool pinnedLast{};     ///< Control transfer or barrier (or a lone auipc: both flags are set,
                           ///< so it keeps its position because its value depends on the PC).
};

/** @brief Result for one basic block: original and chosen order, DAG and per-unit stalls. */
struct BasicBlockSchedule
{
    std::vector<std::size_t>
        originalInstructionIndices; ///< intermediate_code indices, original order.
    std::vector<std::size_t>
        scheduledInstructionIndices;          ///< Chosen order (== original if !accepted).
    std::vector<DependencyEdge> dependencies; ///< Unit-level edges.
    unsigned int
        originalCycleCount{}; ///< Issue slots = words + model stalls (block-local, no fill).
    unsigned int scheduledCycleCount{};

    std::vector<ScheduleUnit> units;             ///< Original order.
    std::vector<std::size_t> scheduledUnitOrder; ///< Permutation of [0, units.size()).
    std::vector<StallInfo> originalStalls;       ///< Per unit id, measured in the original program.
    std::vector<StallInfo> scheduledStalls; ///< Per unit id, measured in the scheduled program.
    unsigned
        originalExitStalls{}; ///< Stalls induced on the next block's head by the original tail.
    unsigned scheduledExitStalls{};
    bool accepted{}; ///< New order strictly cheaper and passes the quirk guard.
    std::string note;
};

/** @brief Result for a whole program: per-block schedules and whole-program model totals. */
struct SchedulingResult
{
    std::vector<BasicBlockSchedule> blocks;
    unsigned int originalCycleCount{}; ///< Sum of block issue slots (Phase-1 semantics).
    unsigned int scheduledCycleCount{};
    unsigned originalStalls{}; ///< Whole-program model stalls (fall-through context).
    unsigned scheduledStalls{};
    unsigned straightLineOriginalCycles{}; ///< words + stalls + 4 (== Kites if no taken branch).
    unsigned straightLineScheduledCycles{};
    bool applicable{true};
    std::string notApplicableReason;

    [[nodiscard]] bool changed() const
    {
        return scheduledStalls < originalStalls;
    }
};
} // namespace Kites
