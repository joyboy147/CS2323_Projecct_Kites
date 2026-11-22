/**
 * @file kites_hazard_model.h
 * @brief Static stall models: an exact replica of the Kites 5-stage HDU and a
 *        model-only custom-latency variant, plus the sequence evaluator.
 */
#pragma once

#include "scheduler/instruction_facts.h"

#include <set>
#include <utility>

namespace Kites
{
/** One ID->EX issue slot; a bubble when facts == nullptr. */
struct IssueSlot
{
    const InstructionFacts *facts{};
    std::size_t unit{kNoUnit};
};

/** Issue history, most recent LAST. */
using SlotHistory = std::vector<IssueSlot>;

inline constexpr std::size_t kHistoryDepth = 8;

/** A history of `count` bubbles (pipeline start, or after a flush). */
SlotHistory bubbleHistory(std::size_t count = kHistoryDepth);

/** @brief Stall decision for one word: cycles, producer and matched register. */
struct StallVerdict
{
    unsigned cycles{};
    int producerDistance{}; ///< Issue-slot distance of the producer (1 = adjacent).
    RegId reg{};
    bool spurious{};
    const InstructionFacts *producer{};
    std::size_t producerUnit{kNoUnit};
};

/** Exact replica of the Kites HDU decision for `consumer` sitting in IF/ID. Needs encoded words. */
StallVerdict kitesStallsBefore(const InstructionFacts &consumer, const SlotHistory &history,
                               bool forwarding);

/** Model-only: max over producers k of (latency(p_k, consumer) - k). */
StallVerdict customStallsBefore(const InstructionFacts &consumer, const SlotHistory &history,
                                const SchedulingOptions &options);

/** Dispatches on options (no stalls without hazard detection; custom model without words). */
StallVerdict stallsBefore(const InstructionFacts &consumer, const SlotHistory &history,
                          const SchedulingOptions &options);

/**
 * The quirk pairs formed when `words` issue in order after `entryHistory`, as
 * (producer, consumer) program indices. A reorder is only safe for Kites if it creates no
 * pair that the original order did not already have (comparing counts is not enough: a
 * move can remove one pair and create another).
 */
std::set<std::pair<std::size_t, std::size_t>> quirkPairSet(const std::vector<IssueSlot> &words,
                                                           const SlotHistory &entryHistory,
                                                           const SchedulingOptions &options);

/** @brief Stalls of a word sequence, per word, plus the history after it. */
struct SequenceCost
{
    unsigned stalls{};
    std::vector<StallVerdict> perWord; ///< Aligned with the evaluated words.
    unsigned quirkPairs{};
    SlotHistory exitHistory; ///< Last kHistoryDepth slots after the sequence.
};

/** Issues `words` in order after `entryHistory`, inserting bubbles exactly as Kites does. */
SequenceCost evaluateSequence(const std::vector<IssueSlot> &words, const SlotHistory &entryHistory,
                              const SchedulingOptions &options);

/** Appends one word (with its stalls) to `history`; returns the verdict. Used incrementally. */
StallVerdict issueWord(const IssueSlot &word, SlotHistory &history,
                       const SchedulingOptions &options, unsigned &quirkPairs);
} // namespace Kites
