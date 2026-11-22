/**
 * @file stall_profiler.h
 * @brief Qt-free stall profiler: attributes every lost cycle of a real Kites 5-stage run
 *        to a source line and a hazard cause.
 *
 * Accounting identity (checked by identityHolds()):
 *   cycles = retired + stallCycles + flushCycles + fillCycles + truncatedCycles.
 * It is decomposed per retired instance from WB-to-WB gaps: repeated ID cycles are stalls
 * charged to the consumer line; the rest of a gap is a control flush charged to the
 * preceding taken branch / jump line.
 */
#pragma once

#include "common/assembled_program.h"
#include "processor/processor_types.h"
#include "scheduler/instruction_facts.h"
#include "scheduler/pipeline_instance_tracker.h"
#include "scheduler/pipeline_trace.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace Kites
{
/** @brief Why a cycle produced no retirement. */
enum class LostCycleCause : uint8_t
{
    LoadUse,            ///< Forwarding on, producer is a load in EX (GPR or FPR).
    RawNoForwardDist1,  ///< Forwarding off, producer one slot ahead (2 cycles).
    RawNoForwardDist2,  ///< Forwarding off, producer two slots ahead (1 cycle).
    SpuriousFieldAlias, ///< HDU matched a raw rs1/rs2 field the consumer does not read.
    SpuriousFpGprAlias, ///< Producer is an F/D op whose rd the HDU treats as a GPR/FPR write.
    BranchFlush,        ///< Taken B-type: 3 cycles, charged to the branch line.
    JalFlush,           ///< 2 cycles, charged to the jal line.
    JalrFlush,          ///< 2 cycles, charged to the jalr line.
    PipelineFill,       ///< Cycles before the first retirement.
    Truncated,          ///< Cycles after the last retirement when the run stopped early.
    Unclassified,       ///< Must stay 0; tests require it.
    Count
};

inline constexpr std::size_t kLostCycleCauseCount = static_cast<std::size_t>(LostCycleCause::Count);
using CauseCounts = std::array<uint64_t, kLostCycleCauseCount>;

/** @brief Short label, e.g. "load-use". */
const char *causeLabel(LostCycleCause cause);
/** @brief One plain sentence for tooltips. */
const char *causeExplanation(LostCycleCause cause);
/** @brief True for the data-hazard causes (the ones reordering can fix). */
bool isDataStallCause(LostCycleCause cause);

/** @brief Kites' own counters at the end of a run, for reconciliation. */
struct KitesCounters
{
    uint64_t cycles{};
    uint64_t retired{};
    uint64_t branchMispredictions{};
    bool exact{true}; ///< False when the processor reset itself (NH_NF ecall stop).
};

/** @brief Cycles lost by one source line, as stall consumer or flush redirector. */
struct LineProfile
{
    unsigned sourceLine{};
    std::string text;
    uint64_t executionCount{};
    uint64_t lostCycles{};
    CauseCounts byCause{};
    std::map<unsigned, uint64_t> producerLines; ///< Producer source line -> stall cycles.
    std::size_t blockIndex{};                   ///< Index into SchedulingResult::blocks.
    std::string spuriousDetail;                 ///< Subtype text when a spurious alias occurred.

    /** @brief The cause with the most cycles on this line. */
    [[nodiscard]] LostCycleCause dominantCause() const;
};

/** @brief NH modes: a dependence an HD processor would have stalled for (value may be stale). */
struct UnprotectedHazard
{
    unsigned consumerLine{};
    unsigned producerLine{};
    RegId reg{};
    int distance{};
    uint64_t occurrenceCount{};
};

/** @brief Result of profiling one run. */
struct StallProfile
{
    ProcessorType processor{ProcessorType::RV5Stage_H_F};
    bool forwarding{};
    bool hazardDetection{};
    uint64_t cycles{};
    uint64_t retired{};
    uint64_t stallCycles{}; ///< Repeated ID cycles of retired instances.
    uint64_t flushCycles{}; ///< Remaining WB-gap cycles after a redirect.
    uint64_t fillCycles{};
    uint64_t truncatedCycles{};
    uint64_t unclassifiedCycles{}; ///< Part of the stall/flush cycles no rule explained.
    uint64_t takenBranchCount{};
    uint64_t jumpCount{};
    CauseCounts byCause{};
    CauseCounts eventsByCause{};
    std::vector<LineProfile>
        lines; ///< lostCycles desc, then sourceLine asc; zero-loss lines omitted.
    std::vector<UnprotectedHazard> unprotected;
    bool hitCycleLimit{};
    bool cancelled{};
    KitesCounters kites;

    /** @brief The accounting identity plus agreement with Kites' counters and no unclassified
     * cycles. */
    [[nodiscard]] bool identityHolds() const;
    /** @brief Human-readable reason when identityHolds() is false (empty otherwise). */
    [[nodiscard]] std::string identityReport() const;
};

/** @brief Selects the pipeline model; HF, HNF, NH_F and NH_NF are supported. */
struct StallProfilerOptions
{
    ProcessorType processor{ProcessorType::RV5Stage_H_F};
};

/**
 * @brief Streaming profiler. Feed pre-step snapshots (PipelineTrace convention) with
 * onCycle(), then call finish(). Memory is O(lines), not O(cycles).
 */
class StallProfiler
{
  public:
    StallProfiler(const AssembledProgram &program, const std::string &sourceText,
                  const StallProfilerOptions &options);
    ~StallProfiler();
    StallProfiler(const StallProfiler &) = delete;
    StallProfiler &operator=(const StallProfiler &) = delete;

    /** @brief Feed one pre-step snapshot (cycle order). */
    void onCycle(const CycleSnapshot &snapshot);
    /** @brief Close the run: still-live instances are discarded, and lines are sorted. */
    StallProfile finish(const KitesCounters &counters, bool hitCycleLimit, bool cancelled);

  private:
    void onInstance(const InstanceEvent &event);
    void classifyStall(const InstanceEvent &event, unsigned stall);
    void recordUnprotected(const InstanceEvent &event);
    const InstructionFacts *factsAt(uint64_t pc) const;
    unsigned lineOf(uint64_t pc) const;
    LineProfile &line(unsigned sourceLine);
    void charge(uint64_t pc, LostCycleCause cause, uint64_t cycles);

    const AssembledProgram &m_program;
    std::vector<std::string> m_sourceLines;
    StallProfilerOptions m_options;
    bool m_forwarding{};
    bool m_hazardDetection{};
    std::vector<InstructionFacts> m_facts;
    std::unique_ptr<PipelineInstanceTracker> m_tracker;
    std::map<unsigned, LineProfile> m_lines;
    std::map<std::tuple<unsigned, unsigned, RegId, int>, uint64_t> m_unprotected;
    StallProfile m_profile;
    bool m_haveRetired{};
    unsigned m_lastWb{};
    uint64_t m_lastPc{};
};
} // namespace Kites
