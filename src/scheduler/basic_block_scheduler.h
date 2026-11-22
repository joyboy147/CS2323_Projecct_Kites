/**
 * @file basic_block_scheduler.h
 * @brief Qt-free basic-block instruction scheduler for the Kites 5-stage pipeline.
 *
 * Splits an assembled program into basic blocks of source-line units, builds a
 * dependency DAG per block (RAW/WAR/WAW/memory/barrier), and picks the cheapest
 * legal order under the selected stall model. A block's new order is accepted only
 * if its model cost is strictly lower than the original's; otherwise it is kept.
 * The scheduler never modifies a program (see SuggestionEngine / rewriteSource).
 */
#pragma once

#include "common/assembled_program.h"
#include "scheduler/scheduler_types.h"

#include <vector>

namespace Kites
{
/** @brief Splits a program into blocks and picks a cheaper legal order for each. */
class BasicBlockScheduler
{
  public:
    [[nodiscard]] SchedulingResult schedule(const AssembledProgram &program,
                                            const SchedulingOptions &options) const;
};

/**
 * Units that moved when `original` is reordered into `suggested` (both permutations of
 * [0, n)), via a longest-common-subsequence match. On a tie the element with the higher
 * original position is the one reported as moved. Returns a mask indexed by unit id.
 */
std::vector<bool> movedUnits(const std::vector<std::size_t> &original,
                             const std::vector<std::size_t> &suggested);
} // namespace Kites
