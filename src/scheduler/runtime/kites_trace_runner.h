/**
 * @file kites_trace_runner.h
 * @brief Runs a program on a private Kites 5-stage processor, one Step() at a time,
 *        recording per-cycle stage PCs. Needs QtCore (the processors are QObjects).
 */
#pragma once

#include "common/assembled_program.h"
#include "processor/processor_types.h"
#include "scheduler/pipeline_trace.h"
#include "scheduler/stall_profiler.h"

#include <atomic>
#include <functional>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kites
{
class ProcessorBase;

/** @brief How to run a program on a private Kites processor. */
struct TraceRunOptions
{
    ProcessorType processor{ProcessorType::RV5Stage_H_F};
    unsigned maxCycles{20000};
    std::size_t dataWindowBytes{4096}; ///< Bytes compared from the data section start.
    bool recordTrace{true};            ///< False: no O(cycles) snapshot vector (profiling).
    /// Called with every pre-step snapshot (same convention as PipelineTrace::cycles).
    std::function<void(const CycleSnapshot &)> observer;
    const std::atomic<bool>
        *cancel{}; ///< Polled every 4096 cycles; sets TraceRunResult::cancelled.
    unsigned undoTrimInterval{
        4096}; ///< Drop the processor's undo history every N cycles (0 = never).
};

/** @brief Final state and trace of one run. */
struct TraceRunResult
{
    bool ok{};
    std::string error;
    PipelineTrace trace;       ///< cycles is empty when recordTrace was false.
    std::vector<uint64_t> gpr; ///< Final register values.
    std::vector<uint64_t> fpr;
    std::vector<uint8_t> dataMemory; ///< [dataStart, dataStart + dataWindowBytes).
    KitesCounters counters;          ///< Kites' own cycle / retirement / misprediction counters.
    uint64_t snapshotCount{};        ///< Cycles observed (== counters.cycles for complete runs).
    bool cancelled{};
};

/**
 * @brief Runs programs on private 5-stage Kites processors (HF, HNF, NH_F, NH_NF), one
 * Step() at a time. Keeps one processor per type (creating one is costly in GUI builds).
 *
 * Threading: prepare() constructs the processor (a GUI build creates a QGraphicsScene) and
 * loads the program; call it on the GUI thread. runPrepared() only steps the processor and
 * may run on a worker thread. run() does both. A runner is not thread-safe; it never touches
 * the application's ProcessorManager.
 */
class KitesTraceRunner
{
  public:
    KitesTraceRunner();
    ~KitesTraceRunner();
    KitesTraceRunner(const KitesTraceRunner &) = delete;
    KitesTraceRunner &operator=(const KitesTraceRunner &) = delete;

    /** @brief Construct (if needed), reset and load. Returns an error message, or "" on success. */
    std::string prepare(const AssembledProgram &program, ProcessorType type);
    /** @brief Step the processor loaded by the last prepare() with the same type. */
    TraceRunResult runPrepared(const AssembledProgram &program, const TraceRunOptions &options);
    /** @brief prepare() + runPrepared(). */
    TraceRunResult run(const AssembledProgram &program, const TraceRunOptions &options);

  private:
    struct CachedProcessor;
    CachedProcessor *processorFor(ProcessorType type);

    std::map<ProcessorType, std::unique_ptr<CachedProcessor>> m_cache;
};
/** @brief A stall profile, or why it could not be produced. */
struct ProfileRunResult
{
    bool ok{};
    std::string error;
    StallProfile profile;
};

/**
 * @brief Profiles the program loaded by `runner.prepare(program, options.processor)`.
 * Safe on a worker thread (no trace vector is kept; memory is O(lines)).
 */
ProfileRunResult profilePrepared(KitesTraceRunner &runner, const AssembledProgram &program,
                                 const std::string &sourceText, TraceRunOptions options);

/** @brief prepare() + profilePrepared(), for single-threaded callers (tests, CLI). */
ProfileRunResult profileOnKites(KitesTraceRunner &runner, const AssembledProgram &program,
                                const std::string &sourceText, const TraceRunOptions &options);
} // namespace Kites
