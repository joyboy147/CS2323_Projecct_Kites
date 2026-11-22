#include "scheduler/source_rewriter.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace Kites
{
namespace
{
std::string trimmed(const std::string &line)
{
    const auto begin = line.find_first_not_of(" \t\r");
    if (begin == std::string::npos)
        return {};
    const auto end = line.find_last_not_of(" \t\r");
    return line.substr(begin, end - begin + 1);
}

bool isDirectiveLine(const std::string &line)
{
    std::string text = trimmed(line);
    const auto colon = text.find(':');
    if (colon != std::string::npos)
    {
        const std::string label = text.substr(0, colon);
        const bool isLabel =
            !label.empty() && std::all_of(label.begin(), label.end(), [](unsigned char c)
                                          { return std::isalnum(c) || c == '_' || c == '.'; });
        if (isLabel)
            text = trimmed(text.substr(colon + 1));
    }
    return !text.empty() && text.front() == '.';
}
} // namespace

RewriteResult rewriteSource(const std::string &sourceText, const ProgramSuggestion &suggestion,
                            const std::vector<bool> &acceptedBlocks)
{
    RewriteResult result;
    std::vector<std::string> lines = splitLines(sourceText);
    const bool endsWithNewline = !sourceText.empty() && sourceText.back() == '\n';
    const std::vector<std::string> original = lines;

    const auto &blocks = suggestion.schedule.blocks;
    for (std::size_t b = 0; b < blocks.size(); ++b)
    {
        const BasicBlockSchedule &block = blocks[b];
        const bool selected = acceptedBlocks.empty() ? block.accepted
                                                     : (b < acceptedBlocks.size() &&
                                                        acceptedBlocks[b] && block.accepted);
        if (!selected)
            continue;

        std::vector<unsigned> blockLines;
        for (const ScheduleUnit &unit : block.units)
            blockLines.push_back(unit.sourceLine);
        std::sort(blockLines.begin(), blockLines.end());
        if (std::adjacent_find(blockLines.begin(), blockLines.end()) != blockLines.end() ||
            blockLines.front() == 0 || blockLines.back() > lines.size())
        {
            result.reason = "internal error: block units do not map to distinct source lines";
            return result;
        }

        const std::set<unsigned> instructionLines(blockLines.begin(), blockLines.end());
        for (unsigned line = blockLines.front() + 1; line < blockLines.back(); ++line)
        {
            if (!instructionLines.contains(line) && isDirectiveLine(original[line - 1]))
            {
                result.reason = "line " + std::to_string(line) +
                                " is an assembler directive inside a reordered block";
                return result;
            }
        }

        for (std::size_t k = 0; k < blockLines.size(); ++k)
        {
            const unsigned from = block.units[block.scheduledUnitOrder[k]].sourceLine;
            lines[blockLines[k] - 1] = original[from - 1];
        }
    }

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        result.text += lines[i];
        if (i + 1 < lines.size() || endsWithNewline)
            result.text += '\n';
    }
    result.ok = true;
    return result;
}

RewriteVerification verifyRewrittenProgram(const AssembledProgram &original,
                                           const AssembledProgram &rewritten,
                                           const std::string &rewrittenSource,
                                           const ProgramSuggestion &suggestion)
{
    constexpr uint32_t kAuipcOpcode = 0b0010111;
    RewriteVerification verification;
    if (original.text_buffer.size() != rewritten.text_buffer.size() ||
        original.intermediate_code.size() != rewritten.intermediate_code.size())
    {
        verification.reason = "instruction count changed";
        return verification;
    }

    // An auipc-based pseudo (la, call) re-encodes its PC-relative offset when it moves, so
    // every word on the same line as an auipc is left out of the comparison.
    auto blockWords = [](const AssembledProgram &program, const std::vector<std::size_t> &indices)
    {
        auto wordAt = [&](std::size_t index) {
            return program
                .text_buffer[program.intermediate_code[index].first.getInstructionIndex()];
        };
        std::set<unsigned> pcRelativeLines;
        for (std::size_t index : indices)
            if ((wordAt(index) & 0x7F) == kAuipcOpcode)
                pcRelativeLines.insert(program.intermediate_code[index].first.getLineNumber());
        std::vector<uint32_t> words;
        for (std::size_t index : indices)
            if (!pcRelativeLines.contains(program.intermediate_code[index].first.getLineNumber()))
                words.push_back(wordAt(index));
        std::sort(words.begin(), words.end());
        return words;
    };
    for (std::size_t b = 0; b < suggestion.schedule.blocks.size(); ++b)
    {
        const auto &indices = suggestion.schedule.blocks[b].originalInstructionIndices;
        if (blockWords(original, indices) != blockWords(rewritten, indices))
        {
            verification.reason = "block " + std::to_string(b + 1) + " changed its instructions";
            return verification;
        }
    }

    const ProgramSuggestion again =
        SuggestionEngine{}.analyze(rewrittenSource, rewritten, suggestion.options);
    if (again.originalStaticStalls != suggestion.suggestedStaticStalls)
    {
        verification.reason = "re-analysis found " + std::to_string(again.originalStaticStalls) +
                              " stalls, expected " +
                              std::to_string(suggestion.suggestedStaticStalls);
        return verification;
    }
    verification.ok = true;
    return verification;
}
} // namespace Kites
