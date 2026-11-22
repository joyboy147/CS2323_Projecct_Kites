/**
 * @file suggestion_engine.h
 * @brief Turns scheduler output into user-facing suggestions: listings, moved rows,
 *        plain-language explanations and a rewritten source text. Qt-free.
 */
#pragma once

#include "common/assembled_program.h"
#include "scheduler/scheduler_types.h"

#include <map>
#include <string>
#include <vector>

namespace Kites
{
/** @brief Direction a row moved in the suggested listing. */
enum class MoveKind
{
    None,
    Up,
    Down
};

/** @brief One source line in the original or suggested listing. */
struct ListingRow
{
    std::size_t unitId{}; ///< Global unit id = index into ProgramSuggestion::units.
    std::size_t blockIndex{};
    unsigned sourceLine{};         ///< 1-based, in the original editor text.
    std::string text;              ///< Source line for display (see displayLine()).
    unsigned stallCycles{};        ///< Model stalls before this row.
    std::string stallReason;       ///< e.g. "load-use on x3 from `lw x3, 0(x1)` (line 7)".
    MoveKind move{MoveKind::None}; ///< Filled in suggestedListing only.
    int moveDistance{};            ///< |newPos - oldPos| within the block.
    bool pinned{};
};

/** @brief Plain-language reason for one moved line. */
struct Explanation
{
    std::size_t blockIndex{};
    std::size_t movedUnitId{};
    std::string headline;
    std::string detail;
    unsigned cyclesSaved{};                  ///< Model, per execution of the block.
    std::vector<std::size_t> relatedUnitIds; ///< Producer, consumer, crossed units.
};

/** @brief Everything the Suggestions tab shows for one analysis. */
struct ProgramSuggestion
{
    enum class Status
    {
        Improved,
        NoImprovement,
        NotApplicable,
        Error
    };
    Status status{Status::NoImprovement};
    std::string statusMessage;
    SchedulingOptions options;
    std::vector<ScheduleUnit> units;          ///< Global, original order.
    std::vector<std::size_t> unitBlock;       ///< Block index of each global unit.
    std::vector<ListingRow> originalListing;  ///< One row per unit, original order.
    std::vector<ListingRow> suggestedListing; ///< Same units, suggested order.
    std::vector<Explanation> explanations;
    SchedulingResult schedule;
    unsigned originalStaticStalls{};
    unsigned suggestedStaticStalls{};
    unsigned originalStraightLineCycles{};
    unsigned suggestedStraightLineCycles{};
    bool canApply{};
    std::string applyBlockedReason;
    std::string rewrittenSource; ///< Valid iff canApply.
};

/** @brief Turns scheduler output into listings, explanations and a rewritten source. */
class SuggestionEngine
{
  public:
    /** `sourceText` must be the exact text that produced `program`. */
    [[nodiscard]] ProgramSuggestion analyze(const std::string &sourceText,
                                            const AssembledProgram &program,
                                            const SchedulingOptions &options) const;

    /** Stall reason per PC for `program` in its own order (diagram tooltips). */
    [[nodiscard]] std::map<uint64_t, std::string>
    stallNotesByPc(const std::string &sourceText, const AssembledProgram &program,
                   const SchedulingOptions &options) const;
};

/** Source line `line` (1-based) without its `#`/`;` comment, whitespace collapsed. */
std::string displayLine(const std::vector<std::string> &lines, unsigned line);

/** Splits on '\n' (a trailing newline does not produce an extra empty line). */
std::vector<std::string> splitLines(const std::string &text);
} // namespace Kites
