/**
 * Validates the stall profiler against real Kites runs in all four 5-stage modes:
 * the accounting identity with zero unclassified cycles, agreement with Kites' own
 * counters and with DebugRun(), and the documented properties of the sample programs.
 */
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "scheduler/runtime/kites_trace_runner.h"
#include "scheduler/stall_profiler.h"
#include "scheduler/suggestion_engine.h"
#include "scheduler_test_support.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

namespace Kites
{
namespace
{
using test_support::assembleSource;

constexpr ProcessorType kModes[] = {ProcessorType::RV5Stage_H_F, ProcessorType::RV5Stage_H_NF,
                                    ProcessorType::RV5Stage_NH_F, ProcessorType::RV5Stage_NH_NF};

const char *modeName(ProcessorType type)
{
    switch (type)
    {
    case ProcessorType::RV5Stage_H_F:
        return "HF";
    case ProcessorType::RV5Stage_H_NF:
        return "HNF";
    case ProcessorType::RV5Stage_NH_F:
        return "NH_F";
    default:
        return "NH_NF";
    }
}

bool hasHazardDetection(ProcessorType type)
{
    return type == ProcessorType::RV5Stage_H_F || type == ProcessorType::RV5Stage_H_NF;
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/** Kites prints "Branch taken" lines from Step(); keep test output readable. */
class QuietCout
{
  public:
    QuietCout() : m_saved(std::cout.rdbuf(nullptr))
    {
    }
    ~QuietCout()
    {
        std::cout.rdbuf(m_saved);
    }

  private:
    std::streambuf *m_saved;
};

KitesTraceRunner &runner()
{
    static KitesTraceRunner instance;
    return instance;
}

StallProfile profileOf(const AssembledProgram &program, const std::string &source,
                       ProcessorType type, unsigned maxCycles = 200000)
{
    const QuietCout quiet;
    TraceRunOptions options;
    options.processor = type;
    options.maxCycles = maxCycles;
    const ProfileRunResult result = profileOnKites(runner(), program, source, options);
    EXPECT_TRUE(result.ok) << result.error;
    return result.profile;
}

unsigned debugRunCycles(const AssembledProgram &program, ProcessorType type)
{
    std::unique_ptr<ProcessorBase> processor;
    switch (type)
    {
    case ProcessorType::RV5Stage_H_F:
        processor = std::make_unique<RV5StageProcessorHF>();
        break;
    case ProcessorType::RV5Stage_H_NF:
        processor = std::make_unique<RV5StageProcessorHNF>();
        break;
    case ProcessorType::RV5Stage_NH_F:
        processor = std::make_unique<RV5StageProcessorNHF>();
        break;
    default:
        processor = std::make_unique<RV5StageProcessorNHNF>();
        break;
    }
    processor->LoadProgram(program);
    const QuietCout quiet;
    processor->DebugRun();
    return processor->cycle_s_;
}

bool hasControlTransfer(const AssembledProgram &program)
{
    for (uint32_t word : program.text_buffer)
    {
        const uint32_t opcode = word & 0x7F;
        if (opcode == 0b1100011 || opcode == 0b1101111 || opcode == 0b1100111)
            return true;
    }
    return false;
}

/** Random programs with ALU/M ops, loads/stores, FP data, forward branches, loops and calls. */
std::string randomProgram(std::mt19937 &rng, int seed)
{
    auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto reg = [&] { return "x" + std::to_string(pick(2, 9)); };
    auto offset = [&] { return std::to_string(4 * pick(0, 15)); };
    std::string text = ".data\nscratch: .word 3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3\n"
                       ".text\n    lui x10, 0x10000\n    nop\n    nop\n    nop\n";
    int labels = 0;
    auto straight = [&](int count)
    {
        for (int i = 0; i < count; ++i)
        {
            switch (pick(0, 8))
            {
            case 0:
                text += "    add " + reg() + ", " + reg() + ", " + reg() + "\n";
                break;
            case 1:
                text +=
                    "    addi " + reg() + ", " + reg() + ", " + std::to_string(pick(-8, 40)) + "\n";
                break;
            case 2:
            case 3:
                text += "    lw " + reg() + ", " + offset() + "(x10)\n";
                break;
            case 4:
                text += "    sw " + reg() + ", " + offset() + "(x10)\n";
                break;
            case 5:
                text += "    mul " + reg() + ", " + reg() + ", " + reg() + "\n";
                break;
            case 6:
                text += "    flw f" + std::to_string(pick(1, 4)) + ", " + offset() + "(x10)\n";
                break;
            case 7:
                text += "    fadd.s f" + std::to_string(pick(1, 4)) + ", f" +
                        std::to_string(pick(1, 4)) + ", f" + std::to_string(pick(1, 4)) + "\n";
                break;
            default:
                text +=
                    "    xori " + reg() + ", " + reg() + ", " + std::to_string(pick(0, 31)) + "\n";
                break;
            }
        }
    };
    std::vector<int> functions;
    const int segments = pick(3, 7);
    for (int s = 0; s < segments; ++s)
    {
        const int label = labels++;
        const std::string name = "L" + std::to_string(seed) + "_" + std::to_string(label);
        switch (pick(0, 3))
        {
        case 0: // forward branch over a few instructions
            text += "    " + std::string(pick(0, 1) ? "beq " : "blt ") + reg() + ", " + reg() +
                    ", " + name + "\n";
            straight(pick(1, 3));
            text += name + ":\n";
            break;
        case 1: // counted loop on x11
            text += "    addi x11, x0, " + std::to_string(pick(1, 6)) + "\n" + name + ":\n";
            straight(pick(1, 4));
            text += "    addi x11, x11, -1\n    bne x11, x0, " + name + "\n";
            break;
        case 2: // call
            functions.push_back(label);
            text += "    jal x1, " + name + "\n";
            break;
        default:
            straight(pick(2, 6));
            break;
        }
    }
    text += "    jal x0, end_" + std::to_string(seed) + "\n";
    for (int label : functions)
    {
        text += "L" + std::to_string(seed) + "_" + std::to_string(label) + ":\n";
        straight(pick(3, 5));
        text += "    jalr x0, 0(x1)\n";
    }
    text += "end_" + std::to_string(seed) + ":\n    nop\n";
    return text;
}

struct NamedSource
{
    std::string name;
    std::string source;
};

std::vector<NamedSource> allPrograms()
{
    std::vector<NamedSource> programs;
    for (const char *dir : {"../examples/stall_profiler", "../examples/scheduler_phase2",
                            "../examples/scheduler_phase1"})
    {
        std::vector<std::filesystem::path> files;
        for (const auto &entry : std::filesystem::directory_iterator(dir))
            if (entry.path().extension() == ".s")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const auto &file : files)
            programs.push_back({file.string(), readFile(file)});
    }
    std::mt19937 rng(3141);
    for (int i = 0; i < 60; ++i)
        programs.push_back({"random #" + std::to_string(i), randomProgram(rng, i)});
    return programs;
}

void expectConsistent(const StallProfile &p, ProcessorType type)
{
    EXPECT_TRUE(p.identityHolds()) << p.identityReport();
    EXPECT_EQ(p.unclassifiedCycles, 0U);
    if (!hasHazardDetection(type))
        EXPECT_EQ(p.stallCycles, 0U);
    EXPECT_EQ(p.byCause[std::size_t(LostCycleCause::BranchFlush)],
              3 * p.eventsByCause[std::size_t(LostCycleCause::BranchFlush)]);
    EXPECT_EQ(p.byCause[std::size_t(LostCycleCause::JalFlush)] +
                  p.byCause[std::size_t(LostCycleCause::JalrFlush)],
              2 * (p.eventsByCause[std::size_t(LostCycleCause::JalFlush)] +
                   p.eventsByCause[std::size_t(LostCycleCause::JalrFlush)]));
    uint64_t lost = 0;
    for (const LineProfile &line : p.lines)
        lost += line.lostCycles;
    EXPECT_EQ(lost + p.fillCycles + p.truncatedCycles, p.cycles - p.retired);
}

const LineProfile *lineWithText(const StallProfile &p, const std::string &needle)
{
    for (const LineProfile &line : p.lines)
        if (line.text.find(needle) != std::string::npos)
            return &line;
    return nullptr;
}

class StallProfilerKites : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        setupVmStateDirectory();
    }
};
} // namespace

TEST_F(StallProfilerKites, IdentityHoldsOnEveryProgramAndMode)
{
    unsigned runs = 0;
    unsigned withFlushes = 0;
    for (const NamedSource &named : allPrograms())
    {
        const AssembledProgram program = assembleSource(named.source);
        for (ProcessorType type : kModes)
        {
            SCOPED_TRACE(named.name + " " + modeName(type));
            const StallProfile p = profileOf(program, named.source, type);
            expectConsistent(p, type);
            ++runs;
            withFlushes += p.flushCycles > 0 ? 1 : 0;
            if (!p.hitCycleLimit)
                EXPECT_EQ(p.cycles, debugRunCycles(program, type));
            if (hasHazardDetection(type) && !hasControlTransfer(program))
            {
                EXPECT_EQ(p.fillCycles, 4U);
                const ProgramSuggestion s = SuggestionEngine{}.analyze(
                    named.source, program,
                    {.forwardingEnabled = type == ProcessorType::RV5Stage_H_F});
                EXPECT_EQ(p.stallCycles, s.originalStaticStalls);
            }
        }
    }
    std::cout << "[ info     ] " << runs << " profiled runs, " << withFlushes << " with flushes\n";
    EXPECT_GT(withFlushes, 100U);
}

TEST_F(StallProfilerKites, SampleProgramProperties)
{
    auto load = [](const char *name)
    {
        const std::string source = readFile(std::string("../examples/stall_profiler/") + name);
        return std::make_pair(source, assembleSource(source));
    };
    constexpr auto HF = ProcessorType::RV5Stage_H_F;
    constexpr auto HNF = ProcessorType::RV5Stage_H_NF;
    auto cause = [](const StallProfile &p, LostCycleCause c) { return p.byCause[std::size_t(c)]; };

    {
        const auto [source, program] = load("01_function_calls.s");
        const StallProfile p = profileOf(program, source, HF);
        EXPECT_EQ(p.jumpCount, 7U); // 3 jal + 3 ret + j done
        EXPECT_EQ(p.flushCycles, 14U);
        const LineProfile *mul = lineWithText(p, "mul a0, a0, a0");
        ASSERT_NE(mul, nullptr);
        EXPECT_EQ(mul->byCause[std::size_t(LostCycleCause::LoadUse)], 3U);
    }
    {
        const auto [source, program] = load("02_nested_loops.s");
        const StallProfile p = profileOf(program, source, HF);
        EXPECT_EQ(p.takenBranchCount, 19U);
        EXPECT_EQ(p.flushCycles, 57U);
        ASSERT_FALSE(p.lines.empty());
        EXPECT_EQ(p.lines.front().text, "bne x6, x0, inner");
    }
    {
        const auto [source, program] = load("03_bubble_sort.s");
        const StallProfile p = profileOf(program, source, HF);
        const LineProfile *compare = lineWithText(p, "bge x6, x5, noswap");
        ASSERT_NE(compare, nullptr);
        EXPECT_EQ(compare->byCause[std::size_t(LostCycleCause::LoadUse)], 49U);
        const TraceRunResult run = runner().run(program, {HF});
        ASSERT_TRUE(run.ok);
        for (std::size_t i = 0; i + 4 < 32; i += 4)
            EXPECT_LE(run.dataMemory[i], run.dataMemory[i + 4]);
    }
    {
        const auto [source, program] = load("04_fibonacci.s");
        const StallProfile p = profileOf(program, source, HF);
        EXPECT_GT(cause(p, LostCycleCause::JalrFlush), 0U);
        const TraceRunResult run = runner().run(program, {HF});
        EXPECT_EQ(run.gpr[7], 6765U);
        EXPECT_EQ(run.gpr[8], 21U);
        bool retLoadUse = false;
        for (const LineProfile &line : p.lines)
            retLoadUse = retLoadUse || (line.text.starts_with("ret") &&
                                        line.byCause[std::size_t(LostCycleCause::LoadUse)] > 0);
        EXPECT_TRUE(retLoadUse);
    }
    {
        const auto [source, program] = load("05_memcpy.s");
        const StallProfile p = profileOf(program, source, HF);
        const LineProfile *naive = lineWithText(p, "sw t0, 0(a1)");
        ASSERT_NE(naive, nullptr);
        EXPECT_EQ(naive->byCause[std::size_t(LostCycleCause::LoadUse)], 16U);
        EXPECT_EQ(cause(p, LostCycleCause::LoadUse), 16U); // the paired loop has none
    }
    {
        const auto [source, program] = load("06_fp_kernel.s");
        const StallProfile hf = profileOf(program, source, HF);
        EXPECT_EQ(cause(hf, LostCycleCause::LoadUse), 4U);
        const StallProfile hnf = profileOf(program, source, HNF);
        EXPECT_GT(cause(hnf, LostCycleCause::RawNoForwardDist1), 0U);
        const TraceRunResult run = runner().run(program, {HF});
        EXPECT_EQ(run.fpr[0] & 0xFFFFFFFF, 0x41300000U); // 11.0f
    }
    {
        const auto [source, program] = load("07_fp_alias_quirk.s");
        EXPECT_EQ(profileOf(program, source, HF).stallCycles, 0U);
        EXPECT_GT(cause(profileOf(program, source, HNF), LostCycleCause::SpuriousFpGprAlias), 0U);
    }
    {
        const auto [source, program] = load("08_muldiv.s");
        EXPECT_EQ(profileOf(program, source, HF).stallCycles, 0U);
        const TraceRunResult run = runner().run(program, {HF});
        EXPECT_EQ(run.gpr[11], 1000U);
        EXPECT_EQ(run.gpr[12], 6U);
        EXPECT_EQ(static_cast<int64_t>(run.gpr[14]), -315);
    }
    {
        const auto [source, program] = load("09_rv64.s");
        const StallProfile p = profileOf(program, source, HF);
        const LineProfile *mulw = lineWithText(p, "mulw");
        ASSERT_NE(mulw, nullptr);
        EXPECT_EQ(mulw->dominantCause(), LostCycleCause::LoadUse);
        EXPECT_EQ(runner().run(program, {HF}).gpr[6], 0x123456789ULL);
    }
    {
        const auto [source, program] = load("10_spurious_imm.s");
        const StallProfile p = profileOf(program, source, HF);
        EXPECT_EQ(cause(p, LostCycleCause::SpuriousFieldAlias), 2U);
        const LineProfile *addi = lineWithText(p, "addi x7, x0, 3");
        const LineProfile *lui = lineWithText(p, "lui x9, 0x18");
        ASSERT_TRUE(addi && lui);
        EXPECT_NE(addi->spuriousDetail, lui->spuriousDetail);
    }
}

TEST_F(StallProfilerKites, CycleLimitCancelAndEcallStop)
{
    const std::string loop =
        "    li x5, 1000000\nloop:\n    addi x5, x5, -1\n    bne x5, x0, loop\n";
    const AssembledProgram program = assembleSource(loop);
    const StallProfile limited = profileOf(program, loop, ProcessorType::RV5Stage_H_F, 1000000);
    EXPECT_TRUE(limited.hitCycleLimit);
    EXPECT_EQ(limited.cycles, 1000000U);
    EXPECT_TRUE(limited.identityHolds()) << limited.identityReport();

    std::atomic<bool> cancel{true};
    TraceRunOptions options;
    options.cancel = &cancel;
    ProfileRunResult cancelled;
    {
        const QuietCout quiet;
        cancelled = profileOnKites(runner(), program, loop, options);
    }
    ASSERT_TRUE(cancelled.ok);
    EXPECT_TRUE(cancelled.profile.cancelled);
    EXPECT_LE(cancelled.profile.cycles, 4096U);
    EXPECT_TRUE(cancelled.profile.identityHolds()) << cancelled.profile.identityReport();

    const std::string ecall = "addi x5, x0, 1\naddi a7, x0, 10\necall\naddi x6, x0, 2\n"
                              "addi x7, x0, 3\naddi x8, x0, 4\n";
    const AssembledProgram stopped = assembleSource(ecall);
    const StallProfile p = profileOf(stopped, ecall, ProcessorType::RV5Stage_NH_NF);
    EXPECT_FALSE(p.kites.exact);
    EXPECT_LT(p.retired, 6U);
    EXPECT_TRUE(p.identityHolds()) << p.identityReport();
}

TEST_F(StallProfilerKites, UnprotectedHazardsInNoHazardModes)
{
    const std::string source = readFile("../examples/scheduler_phase1/load_use_original.s");
    const AssembledProgram program = assembleSource(source);
    const StallProfile p = profileOf(program, source, ProcessorType::RV5Stage_NH_F);
    ASSERT_EQ(p.unprotected.size(), 1U);
    EXPECT_EQ(p.unprotected[0].reg, (RegId{RegFile::Gpr, 3}));
    EXPECT_TRUE(profileOf(program, source, ProcessorType::RV5Stage_H_F).unprotected.empty());
}
} // namespace Kites
