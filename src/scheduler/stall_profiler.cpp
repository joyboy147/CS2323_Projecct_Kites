#include "scheduler/stall_profiler.h"

#include "common/instructions.h"
#include "processor/processor_constants.h"
#include "scheduler/basic_block_scheduler.h"
#include "scheduler/kites_hazard_model.h"
#include "scheduler/suggestion_engine.h"

#include <algorithm>
#include <sstream>

namespace Kites
{
namespace
{
constexpr uint8_t kBranchOpcode = 0b1100011;
constexpr uint8_t kJalOpcode = 0b1101111;
constexpr uint8_t kJalrOpcode = 0b1100111;
constexpr unsigned kBranchFlushCycles = 3;
constexpr unsigned kJumpFlushCycles = 2;

std::size_t index(LostCycleCause cause)
{
    return static_cast<std::size_t>(cause);
}

bool isFpWord(uint32_t word)
{
    return instruction_set::isFInstruction(word) || instruction_set::isDInstruction(word);
}

/** The spurious-alias subtype shown in tooltips. */
std::string spuriousDetail(const InstructionFacts &consumer, const InstructionFacts &producer)
{
    if (isFpWord(producer.word))
        return producer.store ? "store's immediate low bits are treated as an FPR destination "
                                "(freg_write on fsw/fsd)"
                              : "FP instruction's rd is treated as a GPR write (Kites control "
                                "unit sets reg_write for FP opcodes)";
    const uint8_t opcode = consumer.word & 0x7F;
    if (opcode == 0b0110111 || opcode == 0b0010111 || opcode == kJalOpcode)
        return "U/J immediate sits in rs1/rs2";
    return "immediate bits sit in the rs2 field (I-type)";
}

bool hasForwarding(ProcessorType type)
{
    return type == ProcessorType::RV5Stage_H_F || type == ProcessorType::RV5Stage_NH_F;
}

bool hasHazardDetection(ProcessorType type)
{
    return type == ProcessorType::RV5Stage_H_F || type == ProcessorType::RV5Stage_H_NF;
}
} // namespace

const char *causeLabel(LostCycleCause cause)
{
    switch (cause)
    {
    case LostCycleCause::LoadUse:
        return "load-use";
    case LostCycleCause::RawNoForwardDist1:
        return "RAW no-fwd d=1";
    case LostCycleCause::RawNoForwardDist2:
        return "RAW no-fwd d=2";
    case LostCycleCause::SpuriousFieldAlias:
        return "spurious field alias";
    case LostCycleCause::SpuriousFpGprAlias:
        return "spurious FP alias";
    case LostCycleCause::BranchFlush:
        return "branch flush";
    case LostCycleCause::JalFlush:
        return "jal flush";
    case LostCycleCause::JalrFlush:
        return "jalr flush";
    case LostCycleCause::PipelineFill:
        return "pipeline fill";
    case LostCycleCause::Truncated:
        return "truncated";
    case LostCycleCause::Unclassified:
        return "unclassified";
    case LostCycleCause::Count:
        break;
    }
    return "?";
}

const char *causeExplanation(LostCycleCause cause)
{
    switch (cause)
    {
    case LostCycleCause::LoadUse:
        return "The instruction needs a value that a load produces only at the end of MEM, so "
               "the hazard unit holds it in ID for one cycle even with forwarding.";
    case LostCycleCause::RawNoForwardDist1:
        return "Without forwarding the instruction waits until the producer right before it "
               "writes the register back in WB (2 cycles).";
    case LostCycleCause::RawNoForwardDist2:
        return "Without forwarding the instruction waits until the producer two slots ahead "
               "writes the register back in WB (1 cycle).";
    case LostCycleCause::SpuriousFieldAlias:
        return "Kites' hazard unit compares raw register fields, so immediate bits that look "
               "like a source register cause a stall although nothing is read.";
    case LostCycleCause::SpuriousFpGprAlias:
        return "Kites' control unit marks FP instructions as register writers, so an FP "
               "destination number is mistaken for an integer register (or a store's immediate "
               "for an FP destination).";
    case LostCycleCause::BranchFlush:
        return "A taken branch is resolved in MEM; the 3 younger instructions are flushed.";
    case LostCycleCause::JalFlush:
        return "jal redirects fetch in EX; the 2 younger instructions are flushed.";
    case LostCycleCause::JalrFlush:
        return "jalr redirects fetch in EX; the 2 younger instructions are flushed.";
    case LostCycleCause::PipelineFill:
        return "The first instruction needs 4 cycles to reach WB.";
    case LostCycleCause::Truncated:
        return "The run stopped (cycle limit or ecall) before the pipeline drained.";
    case LostCycleCause::Unclassified:
        return "A cycle the profiler could not explain (should never happen).";
    case LostCycleCause::Count:
        break;
    }
    return "";
}

bool isDataStallCause(LostCycleCause cause)
{
    return cause <= LostCycleCause::SpuriousFpGprAlias;
}

LostCycleCause LineProfile::dominantCause() const
{
    const auto it = std::max_element(byCause.begin(), byCause.end());
    return static_cast<LostCycleCause>(it - byCause.begin());
}

bool StallProfile::identityHolds() const
{
    return identityReport().empty();
}

std::string StallProfile::identityReport() const
{
    std::ostringstream out;
    const uint64_t sum = retired + stallCycles + flushCycles + fillCycles + truncatedCycles;
    if (sum != cycles)
        out << "cycles " << cycles << " != retired " << retired << " + stall " << stallCycles
            << " + flush " << flushCycles << " + fill " << fillCycles << " + truncated "
            << truncatedCycles << "; ";
    if (unclassifiedCycles != 0)
        out << unclassifiedCycles << " unclassified cycles; ";
    if (kites.exact)
    {
        if (cycles != kites.cycles)
            out << "cycles " << cycles << " != Kites " << kites.cycles << "; ";
        if (retired != kites.retired)
            out << "retired " << retired << " != Kites " << kites.retired << "; ";
        // A truncated run may stop after Kites resolved a taken branch in MEM but before the
        // redirected instruction retired, so one in-flight branch is not yet visible here.
        const bool truncated = hitCycleLimit || cancelled;
        const bool branchesAgree =
            takenBranchCount == kites.branchMispredictions ||
            (truncated && kites.branchMispredictions == takenBranchCount + 1);
        if (!branchesAgree)
            out << "taken branches " << takenBranchCount << " != Kites "
                << kites.branchMispredictions << "; ";
    }
    return out.str();
}

StallProfiler::StallProfiler(const AssembledProgram &program, const std::string &sourceText,
                             const StallProfilerOptions &options)
    : m_program(program), m_sourceLines(splitLines(sourceText)), m_options(options),
      m_forwarding(hasForwarding(options.processor)),
      m_hazardDetection(hasHazardDetection(options.processor))
{
    SchedulingOptions decode;
    decode.scheduleFloatingPoint = true; // decode FP registers too
    m_facts.reserve(program.text_buffer.size());
    for (uint32_t word : program.text_buffer)
        m_facts.push_back(describeWord(word, decode));
    m_tracker = std::make_unique<PipelineInstanceTracker>([this](const InstanceEvent &event)
                                                          { onInstance(event); });
    m_profile.processor = options.processor;
    m_profile.forwarding = m_forwarding;
    m_profile.hazardDetection = m_hazardDetection;
}

StallProfiler::~StallProfiler() = default;

const InstructionFacts *StallProfiler::factsAt(uint64_t pc) const
{
    if (pc == INVALID_PC || pc % 4 != 0 || pc / 4 >= m_facts.size())
        return nullptr;
    return &m_facts[pc / 4];
}

unsigned StallProfiler::lineOf(uint64_t pc) const
{
    const auto it =
        m_program.instruction_number_line_number_mapping.find(static_cast<unsigned>(pc / 4));
    return it == m_program.instruction_number_line_number_mapping.end() ? 0 : it->second;
}

LineProfile &StallProfiler::line(unsigned sourceLine)
{
    auto [it, inserted] = m_lines.try_emplace(sourceLine);
    if (inserted)
    {
        it->second.sourceLine = sourceLine;
        it->second.text = displayLine(m_sourceLines, sourceLine);
    }
    return it->second;
}

void StallProfiler::charge(uint64_t pc, LostCycleCause cause, uint64_t cycles)
{
    if (cycles == 0)
        return;
    LineProfile &target = line(lineOf(pc));
    target.byCause[index(cause)] += cycles;
    target.lostCycles += cycles;
    m_profile.byCause[index(cause)] += cycles;
    ++m_profile.eventsByCause[index(cause)];
}

void StallProfiler::onCycle(const CycleSnapshot &snapshot)
{
    m_tracker->onCycle(snapshot);
}

void StallProfiler::classifyStall(const InstanceEvent &event, unsigned stall)
{
    const InstructionFacts *consumer = factsAt(event.pc);
    unsigned explained = 0;
    unsigned k = 0;
    while (consumer && k + 1 < event.idCycles && explained < stall)
    {
        const auto &[exPc, memPc] = event.idContext[k];
        SlotHistory history{IssueSlot{factsAt(memPc)}, IssueSlot{factsAt(exPc)}};
        const StallVerdict verdict = kitesStallsBefore(*consumer, history, m_forwarding);
        if (verdict.cycles == 0)
            break;
        const unsigned cycles = std::min(verdict.cycles, stall - explained);
        const InstructionFacts *producer = verdict.producer;
        LostCycleCause cause;
        if (verdict.spurious)
            cause = producer && isFpWord(producer->word) ? LostCycleCause::SpuriousFpGprAlias
                                                         : LostCycleCause::SpuriousFieldAlias;
        else if (m_forwarding)
            cause = LostCycleCause::LoadUse;
        else
            cause = verdict.producerDistance == 1 ? LostCycleCause::RawNoForwardDist1
                                                  : LostCycleCause::RawNoForwardDist2;
        charge(event.pc, cause, cycles);
        LineProfile &consumerLine = line(lineOf(event.pc));
        const uint64_t producerPc = verdict.producerDistance == 1 ? exPc : memPc;
        consumerLine.producerLines[lineOf(producerPc)] += cycles;
        if (verdict.spurious && producer && consumerLine.spuriousDetail.empty())
            consumerLine.spuriousDetail = spuriousDetail(*consumer, *producer);
        explained += cycles;
        k += verdict.cycles;
    }
    m_profile.stallCycles += stall;
    if (explained < stall)
        charge(event.pc, LostCycleCause::Unclassified, stall - explained);
}

void StallProfiler::recordUnprotected(const InstanceEvent &event)
{
    const InstructionFacts *consumer = factsAt(event.pc);
    if (!consumer || event.idContext.empty())
        return;
    const auto &[exPc, memPc] = event.idContext.front();
    SlotHistory history{IssueSlot{factsAt(memPc)}, IssueSlot{factsAt(exPc)}};
    const StallVerdict verdict = kitesStallsBefore(*consumer, history, m_forwarding);
    if (verdict.cycles == 0 || verdict.spurious)
        return;
    const uint64_t producerPc = verdict.producerDistance == 1 ? exPc : memPc;
    ++m_unprotected[{lineOf(event.pc), lineOf(producerPc), verdict.reg, verdict.producerDistance}];
}

void StallProfiler::onInstance(const InstanceEvent &event)
{
    if (event.kind != InstanceEvent::Kind::Retired)
        return; // wrong-path work: its stalls are hidden by the flush that removed it
    ++m_profile.retired;
    ++line(lineOf(event.pc)).executionCount;
    const unsigned stall = event.idCycles > 0 ? event.idCycles - 1 : 0;
    if (stall > 0)
        classifyStall(event, stall);
    if (!m_hazardDetection)
        recordUnprotected(event);

    if (!m_haveRetired)
    {
        m_profile.fillCycles = event.wbCycle - stall;
        m_profile.byCause[index(LostCycleCause::PipelineFill)] = m_profile.fillCycles;
    }
    else
    {
        const unsigned gap = event.wbCycle - m_lastWb - 1;
        const unsigned control = gap >= stall ? gap - stall : 0;
        if (gap < stall)
            charge(event.pc, LostCycleCause::Unclassified, stall - gap);
        if (control > 0)
        {
            const InstructionFacts *redirector = factsAt(m_lastPc);
            const uint8_t opcode = redirector ? redirector->word & 0x7F : 0;
            LostCycleCause cause = LostCycleCause::Unclassified;
            if (opcode == kBranchOpcode && control == kBranchFlushCycles)
            {
                cause = LostCycleCause::BranchFlush;
                ++m_profile.takenBranchCount;
            }
            else if (opcode == kJalOpcode && control == kJumpFlushCycles)
            {
                cause = LostCycleCause::JalFlush;
                ++m_profile.jumpCount;
            }
            else if (opcode == kJalrOpcode && control == kJumpFlushCycles)
            {
                cause = LostCycleCause::JalrFlush;
                ++m_profile.jumpCount;
            }
            m_profile.flushCycles += control;
            charge(m_lastPc, cause, control);
        }
    }
    m_haveRetired = true;
    m_lastWb = event.wbCycle;
    m_lastPc = event.pc;
}

StallProfile StallProfiler::finish(const KitesCounters &counters, bool hitCycleLimit,
                                   bool cancelled)
{
    m_tracker->finish();
    m_profile.cycles = m_tracker->cycleCount();
    m_profile.truncatedCycles =
        m_haveRetired ? m_profile.cycles - (m_lastWb + 1) : m_profile.cycles;
    m_profile.byCause[index(LostCycleCause::Truncated)] = m_profile.truncatedCycles;
    m_profile.unclassifiedCycles = m_profile.byCause[index(LostCycleCause::Unclassified)];
    m_profile.hitCycleLimit = hitCycleLimit;
    m_profile.cancelled = cancelled;
    m_profile.kites = counters;

    SchedulingOptions scheduling;
    scheduling.forwardingEnabled = m_forwarding;
    const SchedulingResult schedule = BasicBlockScheduler{}.schedule(m_program, scheduling);
    std::map<unsigned, std::size_t> blockOfLine;
    for (std::size_t b = 0; b < schedule.blocks.size(); ++b)
        for (const ScheduleUnit &unit : schedule.blocks[b].units)
            blockOfLine[unit.sourceLine] = b;

    for (auto &[number, profile] : m_lines)
    {
        if (profile.lostCycles == 0)
            continue;
        if (const auto it = blockOfLine.find(number); it != blockOfLine.end())
            profile.blockIndex = it->second;
        m_profile.lines.push_back(profile);
    }
    std::sort(m_profile.lines.begin(), m_profile.lines.end(),
              [](const LineProfile &a, const LineProfile &b)
              {
                  return a.lostCycles != b.lostCycles ? a.lostCycles > b.lostCycles
                                                      : a.sourceLine < b.sourceLine;
              });
    for (const auto &[key, count] : m_unprotected)
    {
        const auto &[consumerLine, producerLine, reg, distance] = key;
        m_profile.unprotected.push_back({consumerLine, producerLine, reg, distance, count});
    }
    return m_profile;
}
} // namespace Kites
