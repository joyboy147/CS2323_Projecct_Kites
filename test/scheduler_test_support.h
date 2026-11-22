/**
 * @file scheduler_test_support.h
 * @brief Helpers for the scheduler tests: RISC-V encoders, a hand-built program
 *        builder (no assembler needed) and an assembler wrapper for source text.
 */
#pragma once

#include "assembler/assembler.h"
#include "common/assembled_program.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace Kites::test_support
{
namespace enc
{
constexpr uint32_t R(uint32_t f7, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op)
{
    return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
constexpr uint32_t I(int32_t imm, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op)
{
    return (static_cast<uint32_t>(imm) & 0xFFF) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
constexpr uint32_t S(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t op)
{
    const auto u = static_cast<uint32_t>(imm);
    return ((u >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (u & 0x1F) << 7 | op;
}
constexpr uint32_t B(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t op)
{
    const auto u = static_cast<uint32_t>(imm);
    return ((u >> 12) & 1) << 31 | ((u >> 5) & 0x3F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 |
           ((u >> 1) & 0xF) << 8 | ((u >> 11) & 1) << 7 | op;
}
constexpr uint32_t U(uint32_t imm20, uint32_t rd, uint32_t op)
{
    return imm20 << 12 | rd << 7 | op;
}
constexpr uint32_t J(int32_t imm, uint32_t rd, uint32_t op)
{
    const auto u = static_cast<uint32_t>(imm);
    return ((u >> 20) & 1) << 31 | ((u >> 1) & 0x3FF) << 21 | ((u >> 11) & 1) << 20 |
           ((u >> 12) & 0xFF) << 12 | rd << 7 | op;
}

constexpr uint32_t add(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    return R(0, rs2, rs1, 0, rd, 0b0110011);
}
constexpr uint32_t mul(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    return R(1, rs2, rs1, 0, rd, 0b0110011);
}
constexpr uint32_t div(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    return R(1, rs2, rs1, 4, rd, 0b0110011);
}
constexpr uint32_t addi(uint32_t rd, uint32_t rs1, int32_t imm)
{
    return I(imm, rs1, 0, rd, 0b0010011);
}
constexpr uint32_t lw(uint32_t rd, int32_t imm, uint32_t rs1)
{
    return I(imm, rs1, 2, rd, 0b0000011);
}
constexpr uint32_t sw(uint32_t rs2, int32_t imm, uint32_t rs1)
{
    return S(imm, rs2, rs1, 2, 0b0100011);
}
constexpr uint32_t lui(uint32_t rd, uint32_t imm20)
{
    return U(imm20, rd, 0b0110111);
}
constexpr uint32_t beq(uint32_t rs1, uint32_t rs2, int32_t offset)
{
    return B(offset, rs2, rs1, 0, 0b1100011);
}
constexpr uint32_t jal(uint32_t rd, int32_t offset)
{
    return J(offset, rd, 0b1101111);
}
constexpr uint32_t flw(uint32_t fd, int32_t imm, uint32_t rs1)
{
    return I(imm, rs1, 2, fd, 0b0000111);
}
constexpr uint32_t fadd_s(uint32_t fd, uint32_t fs1, uint32_t fs2)
{
    return R(0, fs2, fs1, 0b111, fd, 0b1010011);
}
constexpr uint32_t ecall()
{
    return 0x00000073;
}
} // namespace enc

/**
 * Builds an AssembledProgram (intermediate code, words, symbols) together with a
 * matching source text, one source line per call, without running the assembler.
 */
class ProgramBuilder
{
  public:
    /** One source line holding one or more encoded words (a pseudo expands to several). */
    ProgramBuilder &line(const std::string &text, const std::vector<uint32_t> &words)
    {
        ++m_line;
        appendText(m_pendingLabel.empty() ? "    " + text : m_pendingLabel + ": " + text);
        if (!m_pendingLabel.empty())
        {
            m_program.symbol_table[m_pendingLabel] = {m_program.text_buffer.size() * 4, m_line,
                                                      false};
            m_pendingLabel.clear();
        }
        for (uint32_t word : words)
        {
            ICUnit unit;
            unit.setLineNumber(m_line);
            unit.setInstructionIndex(static_cast<unsigned>(m_program.text_buffer.size()));
            unit.setOpcode(text.substr(0, text.find(' ')));
            m_program.intermediate_code.emplace_back(unit, true);
            m_program.instruction_number_line_number_mapping[unit.getInstructionIndex()] = m_line;
            m_program.text_buffer.push_back(word);
        }
        return *this;
    }

    /** A label on its own line, defined at the next instruction. */
    ProgramBuilder &label(const std::string &name)
    {
        ++m_line;
        appendText(name + ":");
        m_program.symbol_table[name] = {m_program.text_buffer.size() * 4, m_line, false};
        return *this;
    }

    /** A label on the same line as the next instruction. */
    ProgramBuilder &labelOnNextLine(const std::string &name)
    {
        m_pendingLabel = name;
        return *this;
    }

    [[nodiscard]] const AssembledProgram &program() const
    {
        return m_program;
    }
    [[nodiscard]] const std::string &source() const
    {
        return m_source;
    }

  private:
    void appendText(const std::string &text)
    {
        m_source += text + "\n";
    }

    AssembledProgram m_program;
    std::string m_source;
    std::string m_pendingLabel;
    unsigned m_line{};
};

inline AssembledProgram assembleSource(const std::string &source)
{
    std::istringstream input(source);
    return assemble(input);
}
} // namespace Kites::test_support
