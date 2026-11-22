/**
 * @file loop_unroller.h
 * @brief Conservative source-to-source unrolling for simple counted RISC-V loops.
 *
 * The transform intentionally accepts only loops whose iteration count is visible as an
 * `addi counter, x0, constant` in the loop's straight-line preheader.  This keeps the first
 * implementation semantics-preserving without needing scratch registers or a remainder path.
 */
#pragma once

#include <string>
#include <vector>

namespace Kites
{
struct LoopUnrollResult
{
    std::string source;
    unsigned factor{1};
    unsigned loopsUnrolled{};
    std::vector<std::string> notes;

    [[nodiscard]] bool changed() const
    {
        return loopsUnrolled != 0;
    }
};

/**
 * Unroll eligible countdown loops by `factor` (currently 1, 2, or 4).
 *
 * An eligible loop has a label, no labels/directives/control transfers in its body, a final
 * `addi counter, counter, -1`, and `bne counter, x0, label`.  The counter must have a positive,
 * factor-divisible constant loaded with `addi counter, x0, constant` in the straight-line
 * preheader. Unsupported loops are left unchanged and described in `notes`.
 */
[[nodiscard]] LoopUnrollResult unrollSimpleCountedLoops(const std::string &source,
                                                         unsigned factor);
} // namespace Kites
