/**
 * @file source_rewriter.h
 * @brief Applies a suggestion to source text as a permutation of whole lines.
 */
#pragma once

#include "scheduler/suggestion_engine.h"

#include <string>
#include <vector>

namespace Kites
{
/** @brief Rewritten source text, or why the rewrite was refused. */
struct RewriteResult
{
    bool ok{};
    std::string text;
    std::string reason;
};

/**
 * Permutes the instruction lines of every accepted block (or of the blocks selected
 * by `acceptedBlocks`, indexed by block; empty = all accepted blocks). Whole physical
 * lines move, including indentation and trailing comments; every other line stays.
 * Refuses when a directive line sits inside a reordered block's line range.
 */
RewriteResult rewriteSource(const std::string &sourceText, const ProgramSuggestion &suggestion,
                            const std::vector<bool> &acceptedBlocks = {});

/** @brief Outcome of verifyRewrittenProgram(). */
struct RewriteVerification
{
    bool ok{};
    std::string reason;
};

/**
 * Checks that `rewritten` (the assembled `rewrittenSource`) is what the suggestion promised:
 * same instruction count, the same multiset of words per block (lines holding an auipc, such
 * as `la`, are excepted: their PC-relative offset legitimately changes) and, re-analysed in
 * its own order, exactly the suggested number of model stalls.
 */
RewriteVerification verifyRewrittenProgram(const AssembledProgram &original,
                                           const AssembledProgram &rewritten,
                                           const std::string &rewrittenSource,
                                           const ProgramSuggestion &suggestion);
} // namespace Kites
