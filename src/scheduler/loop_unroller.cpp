#include "scheduler/loop_unroller.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <sstream>

namespace Kites
{
namespace
{
std::string trim(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::vector<std::string> splitSource(const std::string &source)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < source.size())
    {
        const std::size_t end = source.find('\n', start);
        lines.push_back(source.substr(start, end == std::string::npos ? std::string::npos
                                                                       : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return lines;
}

std::string withoutComment(const std::string &line)
{
    const std::size_t comment = line.find_first_of("#;");
    return trim(line.substr(0, comment));
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::optional<std::string> labelOnLine(const std::string &line)
{
    const std::string text = withoutComment(line);
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos || !trim(text.substr(colon + 1)).empty())
        return std::nullopt;
    const std::string name = trim(text.substr(0, colon));
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) ||
                          name.front() == '_' || name.front() == '.'))
        return std::nullopt;
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char c)
                       { return std::isalnum(c) || c == '_' || c == '.'; })
               ? std::optional<std::string>(name)
               : std::nullopt;
}

struct Instruction
{
    std::string mnemonic;
    std::vector<std::string> operands;
};

std::optional<Instruction> parseInstruction(const std::string &line)
{
    const std::string text = withoutComment(line);
    if (text.empty() || text.front() == '.' || text.find(':') != std::string::npos)
        return std::nullopt;
    std::istringstream input(text);
    Instruction result;
    input >> result.mnemonic;
    result.mnemonic = lower(result.mnemonic);
    std::string operandText;
    std::getline(input, operandText);
    std::istringstream operands(operandText);
    std::string operand;
    while (std::getline(operands, operand, ','))
    {
        operand = lower(trim(operand));
        if (operand.empty())
            return std::nullopt;
        result.operands.push_back(std::move(operand));
    }
    return result;
}

std::optional<std::string> canonicalGpr(const std::string &text)
{
    if (text.size() >= 2 && text.front() == 'x')
    {
        unsigned value{};
        const auto parsed = std::from_chars(text.data() + 1, text.data() + text.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value < 32)
            return "x" + std::to_string(value);
    }

    // The source editor accepts the standard RISC-V ABI names.  Normalising them here means
    // loop recognition works equally for compiler-style x-register code and student-friendly
    // examples using t0/t1/a0/etc., while the emitted program preserves the user's spelling.
    static const std::pair<const char *, unsigned> aliases[] = {
        {"zero", 0}, {"ra", 1},  {"sp", 2},  {"gp", 3},  {"tp", 4},
        {"t0", 5},   {"t1", 6},  {"t2", 7},  {"s0", 8},  {"fp", 8},
        {"s1", 9},   {"a0", 10}, {"a1", 11}, {"a2", 12}, {"a3", 13},
        {"a4", 14},  {"a5", 15}, {"a6", 16}, {"a7", 17}, {"s2", 18},
        {"s3", 19},  {"s4", 20}, {"s5", 21}, {"s6", 22}, {"s7", 23},
        {"s8", 24},  {"s9", 25}, {"s10", 26}, {"s11", 27}, {"t3", 28},
        {"t4", 29},  {"t5", 30}, {"t6", 31},
    };
    for (const auto &[name, number] : aliases)
        if (text == name)
            return "x" + std::to_string(number);
    return std::nullopt;
}

bool sameRegister(const std::string &left, const std::string &right)
{
    const auto canonicalLeft = canonicalGpr(left);
    const auto canonicalRight = canonicalGpr(right);
    return canonicalLeft && canonicalRight && *canonicalLeft == *canonicalRight;
}

bool isRegister(const std::string &text)
{
    return canonicalGpr(text).has_value();
}

std::optional<long> decimal(const std::string &text)
{
    long value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
               ? std::optional<long>(value)
               : std::nullopt;
}

bool isCountdown(const Instruction &instruction, const std::string &counter)
{
    return instruction.mnemonic == "addi" && instruction.operands.size() == 3 &&
           sameRegister(instruction.operands[0], counter) &&
           sameRegister(instruction.operands[1], counter) &&
           instruction.operands[2] == "-1";
}

bool isLoopBranch(const Instruction &instruction, const std::string &counter,
                  const std::string &label)
{
    if (instruction.mnemonic == "bne" && instruction.operands.size() == 3)
        return sameRegister(instruction.operands[0], counter) &&
               sameRegister(instruction.operands[1], "x0") &&
               instruction.operands[2] == lower(label);
    return false;
}

bool isBackBranchToLabel(const Instruction &instruction, const std::string &label)
{
    if (instruction.mnemonic == "bne" && instruction.operands.size() == 3)
        return sameRegister(instruction.operands[1], "x0") &&
               instruction.operands[2] == lower(label);
    return false;
}

bool isControlTransfer(const Instruction &instruction)
{
    const std::string &op = instruction.mnemonic;
    return (!op.empty() && op.front() == 'b') || op == "jal" || op == "jalr" || op == "j" ||
           op == "call" || op == "ret" || op == "ecall" || op == "ebreak";
}

std::optional<unsigned> initialCount(const std::vector<std::string> &lines, std::size_t labelLine,
                                     const std::string &counter)
{
    // A small straight-line preheader may initialise other registers after the loop counter.
    // Walk backwards until a label/directive/control transfer, stopping if the counter itself is
    // assigned by anything other than the supported constant initialiser.
    while (labelLine > 0)
    {
        --labelLine;
        const std::string candidate = withoutComment(lines[labelLine]);
        if (candidate.empty())
            continue;
        if (labelOnLine(lines[labelLine]) || candidate.front() == '.')
            return std::nullopt;
        const auto instruction = parseInstruction(lines[labelLine]);
        if (!instruction || (!instruction->mnemonic.empty() && instruction->mnemonic.front() == 'b') ||
            instruction->mnemonic == "jal" || instruction->mnemonic == "jalr" ||
            instruction->mnemonic == "j" || instruction->mnemonic == "call" ||
            instruction->mnemonic == "ret")
            return std::nullopt;
        if (instruction->operands.empty() || !sameRegister(instruction->operands[0], counter))
            continue;
        std::optional<long> value;
        if (instruction->mnemonic == "li" && instruction->operands.size() == 2)
            value = decimal(instruction->operands[1]);
        else if (instruction->mnemonic == "addi" && instruction->operands.size() == 3 &&
                 sameRegister(instruction->operands[1], "x0"))
            value = decimal(instruction->operands[2]);
        else
            return std::nullopt;
        if (!value || *value <= 0 || *value > 1000000)
            return std::nullopt;
        return static_cast<unsigned>(*value);
    }
    return std::nullopt;
}

struct Replacement
{
    std::size_t begin{};
    std::size_t end{};
    std::vector<std::string> lines;
};
} // namespace

LoopUnrollResult unrollSimpleCountedLoops(const std::string &source, unsigned factor)
{
    LoopUnrollResult result;
    result.source = source;
    result.factor = factor;
    if (factor == 1)
        return result;
    if (factor != 2 && factor != 4)
    {
        result.notes.push_back("Unsupported unroll factor.");
        return result;
    }

    std::vector<std::string> lines = splitSource(source);
    const bool endsWithNewline = !source.empty() && source.back() == '\n';
    std::vector<Replacement> replacements;

    for (std::size_t labelLine = 0; labelLine < lines.size(); ++labelLine)
    {
        const std::optional<std::string> label = labelOnLine(lines[labelLine]);
        if (!label)
            continue;

        std::size_t nextLabel = labelLine + 1;
        while (nextLabel < lines.size() && !labelOnLine(lines[nextLabel]))
            ++nextLabel;
        std::optional<std::size_t> branchLine;
        for (std::size_t i = labelLine + 1; i < nextLabel; ++i)
        {
            const auto instruction = parseInstruction(lines[i]);
            if (instruction && isBackBranchToLabel(*instruction, *label))
            {
                branchLine = i;
                break;
            }
        }
        if (!branchLine)
            continue;

        std::optional<std::size_t> decrementLine;
        for (std::size_t i = *branchLine; i-- > labelLine + 1;)
            if (!withoutComment(lines[i]).empty())
            {
                decrementLine = i;
                break;
            }
        if (!decrementLine)
            continue;
        const auto decrement = parseInstruction(lines[*decrementLine]);
        const auto branch = parseInstruction(lines[*branchLine]);
        if (!decrement || !branch || decrement->mnemonic != "addi" ||
            decrement->operands.size() != 3 || !isRegister(decrement->operands[0]) ||
            !isCountdown(*decrement, decrement->operands[0]) ||
            !isLoopBranch(*branch, decrement->operands[0], *label))
            continue;

        const std::optional<unsigned> count = initialCount(lines, labelLine, decrement->operands[0]);
        if (!count || *count % factor != 0)
        {
            result.notes.push_back("Loop '" + *label + "' was not unrolled: its visible count " +
                                   "must be positive and divisible by " + std::to_string(factor) + ".");
            continue;
        }

        bool safeBody = true;
        for (std::size_t i = labelLine + 1; i <= *decrementLine; ++i)
        {
            const std::string text = withoutComment(lines[i]);
            if (text.empty())
                continue;
            const auto instruction = parseInstruction(lines[i]);
            if (!instruction || instruction->mnemonic == "auipc" ||
                (i != *decrementLine && isControlTransfer(*instruction)))
            {
                safeBody = false;
                break;
            }
        }
        if (!safeBody)
        {
            result.notes.push_back("Loop '" + *label + "' was not unrolled: its body contains "
                                   "a label, directive, control transfer, or PC-relative instruction.");
            continue;
        }

        std::vector<std::string> iteration(lines.begin() + static_cast<std::ptrdiff_t>(labelLine + 1),
                                           lines.begin() + static_cast<std::ptrdiff_t>(*branchLine));
        Replacement replacement;
        replacement.begin = labelLine + 1;
        replacement.end = *branchLine;
        replacement.lines.push_back("    # KITES loop unrolled x" + std::to_string(factor));
        for (unsigned copy = 0; copy < factor; ++copy)
            replacement.lines.insert(replacement.lines.end(), iteration.begin(), iteration.end());
        replacements.push_back(std::move(replacement));
        ++result.loopsUnrolled;
    }

    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it)
    {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(it->begin),
                    lines.begin() + static_cast<std::ptrdiff_t>(it->end));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(it->begin), it->lines.begin(),
                     it->lines.end());
    }

    if (replacements.empty())
    {
        result.notes.push_back("No eligible simple counted loops found.");
        return result;
    }
    result.source.clear();
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        result.source += lines[i];
        if (i + 1 < lines.size() || endsWithNewline)
            result.source += '\n';
    }
    return result;
}
} // namespace Kites
