#include "assembler/assembler.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "scheduler/basic_block_scheduler.h"
#include "utils/utils.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <sstream>

namespace Kites
{
namespace
{
ICUnit makeInstruction(unsigned int index, const std::string &opcode, const std::string &rd,
                       const std::string &rs1 = {}, const std::string &rs2 = {})
{
    ICUnit instruction;
    instruction.setInstructionIndex(index);
    instruction.setLineNumber(index + 1);
    instruction.setOpcode(opcode);
    instruction.setRd(rd);
    instruction.setRs1(rs1);
    instruction.setRs2(rs2);
    return instruction;
}

AssembledProgram programFrom(std::initializer_list<ICUnit> instructions)
{
    AssembledProgram program;
    for (const auto &instruction : instructions) program.intermediate_code.emplace_back(instruction, true);
    return program;
}

std::unique_ptr<RV5StageProcessorHF> runForwardingProgram(const std::string &source)
{
    setupVmStateDirectory();
    std::istringstream input(source);
    auto processor = std::make_unique<RV5StageProcessorHF>();
    processor->LoadProgram(assemble(input));
    processor->DebugRun();
    return processor;
}
} // namespace

TEST(BasicBlockScheduler, FillsLoadUseBubbleWithIndependentInstruction)
{
    const auto program = programFrom({makeInstruction(0, "lw", "x5", "x1"),
                                      makeInstruction(1, "add", "x6", "x5", "x2"),
                                      makeInstruction(2, "addi", "x7", "x3")});

    const auto result = BasicBlockScheduler{}.schedule(program, {.forwardingEnabled = true});

    ASSERT_EQ(result.blocks.size(), 1U);
    EXPECT_EQ(result.blocks.front().scheduledInstructionIndices,
              (std::vector<size_t>{0, 2, 1}));
    EXPECT_EQ(result.originalCycleCount, 4U);
    EXPECT_EQ(result.scheduledCycleCount, 3U);
}

TEST(BasicBlockScheduler, PreservesAntiDependencies)
{
    // Moving the final instruction above the add would overwrite x2 before it is read.
    const auto program = programFrom({makeInstruction(0, "lw", "x5", "x1"),
                                      makeInstruction(1, "add", "x6", "x5", "x2"),
                                      makeInstruction(2, "addi", "x2", "x3")});

    const auto result = BasicBlockScheduler{}.schedule(program, {.forwardingEnabled = true});

    ASSERT_EQ(result.blocks.size(), 1U);
    EXPECT_EQ(result.blocks.front().scheduledInstructionIndices,
              (std::vector<size_t>{0, 1, 2}));
}

TEST(BasicBlockScheduler, PreservesMemoryOrdering)
{
    const auto program = programFrom({makeInstruction(0, "lw", "x5", "x1"),
                                      makeInstruction(1, "sw", "", "x1", "x2"),
                                      makeInstruction(2, "add", "x6", "x5", "x3")});

    const auto result = BasicBlockScheduler{}.schedule(program, {.forwardingEnabled = true});

    ASSERT_EQ(result.blocks.size(), 1U);
    const auto &edges = result.blocks.front().dependencies;
    EXPECT_TRUE(std::any_of(edges.begin(), edges.end(), [](const DependencyEdge &edge)
                            { return edge.producer == 0 && edge.consumer == 1 &&
                                     edge.kind == DependencyKind::MemoryOrder; }));
}

TEST(BasicBlockScheduler, MatchesKitesCycleImprovementForLoadUseGap)
{
    const std::string original = R"(
        addi x1, x0, 100
        addi x2, x0, 20
        sw   x2, 0(x1)
        lw   x3, 0(x1)
        add  x6, x3, x1
        addi x7, x0, 1
    )";
    const std::string reordered = R"(
        addi x1, x0, 100
        addi x2, x0, 20
        sw   x2, 0(x1)
        lw   x3, 0(x1)
        addi x7, x0, 1
        add  x6, x3, x1
    )";

    const auto originalProcessor = runForwardingProgram(original);
    const auto reorderedProcessor = runForwardingProgram(reordered);

    EXPECT_EQ(originalProcessor->registers_.ReadGpr(6), reorderedProcessor->registers_.ReadGpr(6));
    EXPECT_EQ(originalProcessor->registers_.ReadGpr(7), reorderedProcessor->registers_.ReadGpr(7));
    EXPECT_LT(reorderedProcessor->cycle_s_, originalProcessor->cycle_s_);
}
} // namespace Kites
