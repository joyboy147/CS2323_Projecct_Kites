/** Tests for the deliberately conservative source-level loop unroller. */
#include "scheduler/loop_unroller.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace Kites
{
TEST(LoopUnroller, UnrollsKnownEvenCountdownLoopByTwo)
{
    const std::string source = "addi x5, x0, 4\n"
                               "loop:\n"
                               "  add x6, x6, x5\n"
                               "  addi x5, x5, -1\n"
                               "  bne x5, x0, loop\n";
    const LoopUnrollResult result = unrollSimpleCountedLoops(source, 2);
    EXPECT_TRUE(result.changed());
    EXPECT_EQ(result.loopsUnrolled, 1U);
    EXPECT_NE(result.source.find("# KITES loop unrolled x2"), std::string::npos);
    // The decrement is duplicated, while the loop-back branch remains once per two iterations.
    EXPECT_EQ(std::count(result.source.begin(), result.source.end(), '\n'), 8);
    EXPECT_EQ(result.source.find("bne x5, x0, loop"), result.source.rfind("bne x5, x0, loop"));
}

TEST(LoopUnroller, RejectsRemainderAndDynamicLoops)
{
    const std::string odd = "addi x5, x0, 3\n"
                            "loop:\n"
                            "  add x6, x6, x5\n"
                            "  addi x5, x5, -1\n"
                            "  bne x5, x0, loop\n";
    const std::string dynamic = "loop:\n"
                                "  add x6, x6, x5\n"
                                "  addi x5, x5, -1\n"
                                "  bne x5, x0, loop\n";
    EXPECT_FALSE(unrollSimpleCountedLoops(odd, 2).changed());
    EXPECT_FALSE(unrollSimpleCountedLoops(dynamic, 2).changed());
}

TEST(LoopUnroller, UnrollsKnownCountByFour)
{
    const std::string source = "addi x5, x0, 8\n"
                               "loop:\n"
                               "  add x6, x6, x5\n"
                               "  addi x5, x5, -1\n"
                               "  bne x5, x0, loop\n";
    const LoopUnrollResult result = unrollSimpleCountedLoops(source, 4);
    EXPECT_TRUE(result.changed());
    EXPECT_NE(result.source.find("# KITES loop unrolled x4"), std::string::npos);
    EXPECT_EQ(result.source.find("bne x5, x0, loop"), result.source.rfind("bne x5, x0, loop"));
}

TEST(LoopUnroller, UnrollsAbiAliasesWithExplicitBranch)
{
    // The scheduler accepts ABI register aliases while retaining KITES' explicit bne form.
    const std::string source = "la t0, arr\n"
                               "li t1, 16\n"
                               "li t2, 0\n"
                               "loop:\n"
                               "  lw t3, 0(t0)\n"
                               "  add t2, t2, t3\n"
                               "  addi t0, t0, 4\n"
                               "  addi t1, t1, -1\n"
                               "  bne t1, zero, loop\n";
    const LoopUnrollResult result = unrollSimpleCountedLoops(source, 4);
    EXPECT_TRUE(result.changed());
    EXPECT_EQ(result.loopsUnrolled, 1U);
    EXPECT_NE(result.source.find("# KITES loop unrolled x4"), std::string::npos);
    EXPECT_EQ(result.source.find("bne t1, zero, loop"),
              result.source.rfind("bne t1, zero, loop"));
}
} // namespace Kites
