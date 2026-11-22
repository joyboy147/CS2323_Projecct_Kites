/**
 * @file selection_evaluator.h
 * @brief Whole-program model evaluation of a partial suggestion (a subset of the accepted
 *        blocks), for interactive per-block accept/reject.
 *
 * Legality is per block (the DAG never crosses a block boundary), but cost and Kites quirk
 * pairs are not: a block's entry context is its predecessor's tail. The mixed program is
 * therefore always re-evaluated as a whole.
 */
#pragma once

#include "common/assembled_program.h"
#include "scheduler/suggestion_engine.h"

#include <string>
#include <vector>

namespace Kites
{
/** @brief Model result of applying only the selected blocks of a suggestion. */
struct SelectionEvaluation
{
    std::vector<bool> selected;     ///< Per block; unaccepted blocks are always false.
    unsigned stallCount{};          ///< Whole-program model stalls with mixed orders.
    unsigned straightLineCycles{};  ///< words + stalls + 4.
    std::vector<int> perBlockDelta; ///< Stalls saved by each selected block in this combination.
    bool safe{true};                ///< No Kites quirk pair that the original program lacks.
    std::string unsafeReason;
};

/** @brief Evaluate `selected` (indexed by block; missing entries count as false). */
SelectionEvaluation evaluateSelection(const AssembledProgram &program,
                                      const ProgramSuggestion &suggestion,
                                      const std::vector<bool> &selected);

/**
 * @brief A copy of `suggestion` with unselected blocks reverted (accepted=false, original order),
 * static stall fields and the rewritten source recomputed from `sourceText`.
 */
ProgramSuggestion maskSuggestion(const std::string &sourceText, const ProgramSuggestion &suggestion,
                                 const SelectionEvaluation &evaluation);
} // namespace Kites
