/**
 * @file instruction_facts.h
 * @brief Decodes one assembled instruction into the facts the scheduler needs.
 */
#pragma once

#include "common/assembled_program.h"
#include "scheduler/scheduler_types.h"

#include <optional>

namespace Kites
{
/** @brief Architectural reads/writes, flags and Kites pipeline signals of one instruction. */
struct InstructionFacts
{
    std::size_t programIndex{};
    uint32_t word{0x00000013}; ///< NOP if unknown.
    bool hasWord{};            ///< False: derived from ICUnit strings (Phase-1 fallback).
    unsigned sourceLine{};
    std::vector<RegId> reads; ///< Architectural; x0 excluded, f0 included.
    std::vector<RegId> writes;
    bool load{};
    bool store{};
    bool controlTransfer{};
    bool unconditionalJump{};
    bool barrier{};
    bool readsPc{}; ///< auipc: its result depends on where it sits in the program.
    bool floatingPoint{};
    bool mul{};
    bool div{};
    /// FP instruction whose rs1 field is a GPR operand (flw/fsw base, fcvt.s.w, fmv.w.x).
    bool fpReadsGprRs1{};

    // Kites pipeline signals, replicated from RV5SControlUnit and RV5StageVM_Base::pipeline_decode.
    bool kitesRegWrite{};
    bool kitesFregWrite{};
    bool kitesMemRead{};
    uint8_t rdField{};
    uint8_t rs1Field{};
    uint8_t rs2Field{};
    uint8_t rs3Field{};
    std::optional<int64_t> branchOffsetBytes; ///< B/J immediates (numeric-offset leaders).

    [[nodiscard]] bool memoryAccess() const
    {
        return load || store;
    }
    [[nodiscard]] bool readsReg(const RegId &reg) const;
    [[nodiscard]] bool writesReg(const RegId &reg) const;
};

/** Decodes program.intermediate_code[programIndex], from its encoded word when available. */
InstructionFacts describeInstruction(const AssembledProgram &program, std::size_t programIndex,
                                     const SchedulingOptions &options);

/** Decodes a raw word (no program context). */
InstructionFacts describeWord(uint32_t word, const SchedulingOptions &options);
} // namespace Kites
