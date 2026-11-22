#include "scheduler/runtime/kites_trace_runner.h"

#include "config/config.h"
#include "processor/processor_base.h"
#include "processor/processor_constants.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace Kites
{
namespace
{
constexpr uint32_t kEcallWord = 0x00000073;

struct ArchitecturalState
{
    std::vector<uint64_t> gpr;
    std::vector<uint64_t> fpr;
    std::vector<uint8_t> dataMemory;
};

ArchitecturalState captureState(ProcessorBase &processor, std::size_t dataWindowBytes)
{
    ArchitecturalState state;
    state.gpr = processor.registers_.GetGprValues();
    state.fpr = processor.registers_.GetFprValues();
    const uint64_t dataStart = vm_config::config.getDataSectionStart();
    state.dataMemory.reserve(dataWindowBytes);
    for (std::size_t i = 0; i < dataWindowBytes; ++i)
        state.dataMemory.push_back(processor.memory_controller_.peekByte(dataStart + i));
    return state;
}

/** Exposes the protected undo stacks so long runs can drop them (no new signals or slots). */
template <typename P> class TrimmableProcessor final : public P
{
  public:
    void dropUndoHistory()
    {
        this->undo_stack_ = {};
        this->redo_stack_ = {};
    }
};

template <typename P> std::unique_ptr<ProcessorBase> makeTrimmable(std::function<void()> &trim)
{
    auto processor = std::make_unique<TrimmableProcessor<P>>();
    trim = [raw = processor.get()]() { raw->dropUndoHistory(); };
    return processor;
}

bool holdsEcall(const AssembledProgram &program, uint64_t pc)
{
    if (pc == INVALID_PC || pc % 4 != 0)
        return false;
    const uint64_t index = pc / 4;
    return index < program.text_buffer.size() && program.text_buffer[index] == kEcallWord;
}
} // namespace

struct KitesTraceRunner::CachedProcessor
{
    std::unique_ptr<ProcessorBase> processor;
    std::function<void()> dropUndoHistory;
    uint64_t mispredictionsAtStart{}; ///< Reset() does not clear branch_mispredictions_.
    bool prepared{};
};

KitesTraceRunner::KitesTraceRunner() = default;
KitesTraceRunner::~KitesTraceRunner() = default;

KitesTraceRunner::CachedProcessor *KitesTraceRunner::processorFor(ProcessorType type)
{
    auto &slot = m_cache[type];
    if (!slot)
    {
        // Constructed directly: ProcessorFactory is only populated by ProcessorManager.
        auto cached = std::make_unique<CachedProcessor>();
        switch (type)
        {
        case ProcessorType::RV5Stage_H_F:
            cached->processor = makeTrimmable<RV5StageProcessorHF>(cached->dropUndoHistory);
            break;
        case ProcessorType::RV5Stage_H_NF:
            cached->processor = makeTrimmable<RV5StageProcessorHNF>(cached->dropUndoHistory);
            break;
        case ProcessorType::RV5Stage_NH_F:
            cached->processor = makeTrimmable<RV5StageProcessorNHF>(cached->dropUndoHistory);
            break;
        case ProcessorType::RV5Stage_NH_NF:
            cached->processor = makeTrimmable<RV5StageProcessorNHNF>(cached->dropUndoHistory);
            break;
        default:
            return nullptr;
        }
        slot = std::move(cached);
    }
    return slot.get();
}

std::string KitesTraceRunner::prepare(const AssembledProgram &program, ProcessorType type)
{
    CachedProcessor *cached = processorFor(type);
    if (!cached)
        return "single-cycle processor: no pipeline to trace";
    ProcessorBase &processor = *cached->processor;
    processor.Reset();
    processor.LoadProgram(program);
    // LoadProgram() adds a hidden breakpoint at the program end; it only matters for Run(),
    // but clearing it keeps repeated loads from accumulating breakpoints.
    processor.SetBreakpoints({});
    processor.ClearStop();
    cached->dropUndoHistory();
    cached->mispredictionsAtStart = processor.branch_mispredictions_;
    cached->prepared = true;
    return {};
}

TraceRunResult KitesTraceRunner::run(const AssembledProgram &program,
                                     const TraceRunOptions &options)
{
    const std::string error = prepare(program, options.processor);
    if (!error.empty())
    {
        TraceRunResult result;
        result.error = error;
        return result;
    }
    return runPrepared(program, options);
}

TraceRunResult KitesTraceRunner::runPrepared(const AssembledProgram &program,
                                             const TraceRunOptions &options)
{
    TraceRunResult result;
    const auto found = m_cache.find(options.processor);
    if (found == m_cache.end() || !found->second || !found->second->prepared)
    {
        result.error = "processor not prepared";
        return result;
    }
    CachedProcessor &cached = *found->second;
    cached.prepared = false;
    ProcessorBase *processor = cached.processor.get();
    constexpr unsigned kPollInterval = 4096;

    PipelineTrace &trace = result.trace;
    trace.programSizeBytes = processor->program_size_;
    std::optional<ArchitecturalState> beforeEcall;
    const std::vector<uint64_t> &pcs = processor->processor_state_.programCounters;
    for (unsigned cycle = 0;; ++cycle)
    {
        if (processor->IsStopRequested())
        {
            // An ecall stop resets the processor inside Step(); keep the state from before it.
            trace.stoppedByEcall = true;
            break;
        }
        processor->setProcessorState(); // public in ProcessorBase (protected in the rv5s base)
        const bool drained =
            std::all_of(pcs.begin() + 1, pcs.end(), [](uint64_t pc) { return pc == INVALID_PC; });
        if (processor->GetProgramCounter() >= processor->program_size_ && drained)
            break;
        if (cycle >= options.maxCycles)
        {
            trace.hitCycleLimit = true;
            break;
        }
        if (cycle % kPollInterval == 0 && cycle > 0)
        {
            if (options.cancel && options.cancel->load())
            {
                result.cancelled = true;
                break;
            }
            if (options.undoTrimInterval != 0 && cycle % options.undoTrimInterval == 0)
                cached.dropUndoHistory();
        }

        CycleSnapshot snapshot;
        snapshot.pc[0] = pcs[0] < processor->program_size_ ? pcs[0] : INVALID_PC;
        for (std::size_t stage = 1; stage < kPipeStageCount; ++stage)
            snapshot.pc[stage] = pcs[stage];
        ++result.snapshotCount;
        if (options.recordTrace)
            trace.cycles.push_back(snapshot);
        if (options.observer)
            options.observer(snapshot);

        if (std::any_of(pcs.begin(), pcs.end(),
                        [&](uint64_t pc) { return holdsEcall(program, pc); }))
            beforeEcall = captureState(*processor, options.dataWindowBytes);
        try
        {
            processor->Step();
        }
        catch (const std::exception &e)
        {
            result.error = std::string("Kites stopped at cycle ") + std::to_string(cycle + 1) +
                           ": " + e.what();
            return result;
        }
    }

    if (trace.stoppedByEcall && beforeEcall)
    {
        result.gpr = std::move(beforeEcall->gpr);
        result.fpr = std::move(beforeEcall->fpr);
        result.dataMemory = std::move(beforeEcall->dataMemory);
        trace.kitesCycleCount = static_cast<unsigned>(result.snapshotCount);
        result.counters = {result.snapshotCount, 0, 0, false};
    }
    else
    {
        ArchitecturalState state = captureState(*processor, options.dataWindowBytes);
        result.gpr = std::move(state.gpr);
        result.fpr = std::move(state.fpr);
        result.dataMemory = std::move(state.dataMemory);
        trace.kitesCycleCount = processor->cycle_s_;
        result.counters = {processor->cycle_s_, processor->instructions_retired_,
                           processor->branch_mispredictions_ - cached.mispredictionsAtStart, true};
    }
    trace.instructionsRetired = processor->instructions_retired_;
    cached.dropUndoHistory();
    result.ok = true;
    return result;
}

ProfileRunResult profilePrepared(KitesTraceRunner &runner, const AssembledProgram &program,
                                 const std::string &sourceText, TraceRunOptions options)
{
    ProfileRunResult result;
    StallProfiler profiler(program, sourceText, {options.processor});
    options.recordTrace = false;
    options.observer = [&profiler](const CycleSnapshot &snapshot) { profiler.onCycle(snapshot); };
    const TraceRunResult run = runner.runPrepared(program, options);
    if (!run.ok)
    {
        result.error = run.error;
        return result;
    }
    result.profile = profiler.finish(run.counters, run.trace.hitCycleLimit, run.cancelled);
    result.ok = true;
    return result;
}

ProfileRunResult profileOnKites(KitesTraceRunner &runner, const AssembledProgram &program,
                                const std::string &sourceText, const TraceRunOptions &options)
{
    const std::string error = runner.prepare(program, options.processor);
    if (!error.empty())
    {
        ProfileRunResult result;
        result.error = error;
        return result;
    }
    return profilePrepared(runner, program, sourceText, options);
}
} // namespace Kites
