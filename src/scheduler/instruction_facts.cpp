#include "scheduler/instruction_facts.h"

#include "common/instructions.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace Kites
{
std::string regName(const RegId &reg)
{
    return (reg.file == RegFile::Gpr ? "x" : "f") + std::to_string(reg.index);
}

bool InstructionFacts::readsReg(const RegId &reg) const
{
    return std::find(reads.begin(), reads.end(), reg) != reads.end();
}

bool InstructionFacts::writesReg(const RegId &reg) const
{
    return std::find(writes.begin(), writes.end(), reg) != writes.end();
}

namespace
{
constexpr uint32_t kNopWord = 0x00000013;

namespace opcode
{
constexpr uint8_t Op = 0b0110011;
constexpr uint8_t Op32 = 0b0111011;
constexpr uint8_t OpImm = 0b0010011;
constexpr uint8_t OpImm32 = 0b0011011;
constexpr uint8_t Load = 0b0000011;
constexpr uint8_t Store = 0b0100011;
constexpr uint8_t Lui = 0b0110111;
constexpr uint8_t Auipc = 0b0010111;
constexpr uint8_t Jal = 0b1101111;
constexpr uint8_t Jalr = 0b1100111;
constexpr uint8_t Branch = 0b1100011;
constexpr uint8_t LoadFp = 0b0000111;
constexpr uint8_t StoreFp = 0b0100111;
constexpr uint8_t OpFp = 0b1010011;
constexpr uint8_t FmAdd = 0b1000011;
constexpr uint8_t FmSub = 0b1000111;
constexpr uint8_t FnmSub = 0b1001011;
constexpr uint8_t FnmAdd = 0b1001111;
} // namespace opcode

void addGprRead(InstructionFacts &facts, uint8_t reg)
{
    if (reg != 0)
        facts.reads.push_back({RegFile::Gpr, reg});
}

void addGprWrite(InstructionFacts &facts, uint8_t reg)
{
    if (reg != 0)
        facts.writes.push_back({RegFile::Gpr, reg});
}

void addFprRead(InstructionFacts &facts, uint8_t reg)
{
    facts.reads.push_back({RegFile::Fpr, reg});
}

void addFprWrite(InstructionFacts &facts, uint8_t reg)
{
    facts.writes.push_back({RegFile::Fpr, reg});
}

int64_t signExtend(uint64_t value, unsigned bits)
{
    const uint64_t signBit = uint64_t{1} << (bits - 1);
    return static_cast<int64_t>((value ^ signBit) - signBit);
}

int64_t branchImmediate(uint32_t word)
{
    const uint64_t imm = (((word >> 31) & 0x1) << 12) | (((word >> 7) & 0x1) << 11) |
                         (((word >> 25) & 0x3F) << 5) | (((word >> 8) & 0xF) << 1);
    return signExtend(imm, 13);
}

int64_t jumpImmediate(uint32_t word)
{
    const uint64_t imm = (((word >> 31) & 0x1) << 20) | (((word >> 12) & 0xFF) << 12) |
                         (((word >> 20) & 0x1) << 11) | (((word >> 21) & 0x3FF) << 1);
    return signExtend(imm, 21);
}

bool kitesWritesGpr(uint8_t op)
{
    // Copied from RV5SControlUnit::SetControlSignals: every opcode that sets reg_write_.
    static constexpr std::array<uint8_t, 15> regWriteOpcodes = {
        opcode::Op,   opcode::OpImm, opcode::Load,    opcode::Lui,    opcode::Auipc,
        opcode::Jal,  opcode::Jalr,  opcode::OpImm32, opcode::Op32,   opcode::LoadFp,
        opcode::OpFp, opcode::FmAdd, opcode::FmSub,   opcode::FnmSub, opcode::FnmAdd};
    return std::find(regWriteOpcodes.begin(), regWriteOpcodes.end(), op) != regWriteOpcodes.end();
}

void decodeFloatingPoint(InstructionFacts &facts, uint8_t op, uint32_t word)
{
    const uint8_t funct5 = static_cast<uint8_t>(word >> 27);
    facts.floatingPoint = true;
    switch (op)
    {
    case opcode::LoadFp:
        addGprRead(facts, facts.rs1Field);
        addFprWrite(facts, facts.rdField);
        facts.load = true;
        facts.fpReadsGprRs1 = true;
        break;
    case opcode::StoreFp:
        addGprRead(facts, facts.rs1Field);
        addFprRead(facts, facts.rs2Field);
        facts.store = true;
        facts.fpReadsGprRs1 = true;
        break;
    case opcode::OpFp:
        switch (funct5)
        {
        case 0b11000: // fcvt.w[u].{s,d}
        case 0b11100: // fmv.x.w / fclass
            addFprRead(facts, facts.rs1Field);
            addGprWrite(facts, facts.rdField);
            break;
        case 0b10100: // feq / flt / fle
            addFprRead(facts, facts.rs1Field);
            addFprRead(facts, facts.rs2Field);
            addGprWrite(facts, facts.rdField);
            break;
        case 0b11010: // fcvt.{s,d}.w[u]
        case 0b11110: // fmv.w.x
            addGprRead(facts, facts.rs1Field);
            addFprWrite(facts, facts.rdField);
            facts.fpReadsGprRs1 = true;
            break;
        case 0b01011: // fsqrt
        case 0b01000: // fcvt.s.d / fcvt.d.s
            addFprRead(facts, facts.rs1Field);
            addFprWrite(facts, facts.rdField);
            break;
        default:
            addFprRead(facts, facts.rs1Field);
            addFprRead(facts, facts.rs2Field);
            addFprWrite(facts, facts.rdField);
            break;
        }
        break;
    default: // FMA group
        addFprRead(facts, facts.rs1Field);
        addFprRead(facts, facts.rs2Field);
        addFprRead(facts, facts.rs3Field);
        addFprWrite(facts, facts.rdField);
        break;
    }
}

/** Integer ALU, load/store, upper-immediate and control-transfer opcodes. */
bool decodeIntegerOp(InstructionFacts &facts, uint8_t op, uint32_t word)
{
    const uint8_t funct3 = (word >> 12) & 0x7;
    const uint8_t funct7 = static_cast<uint8_t>(word >> 25);
    switch (op)
    {
    case opcode::Op:
    case opcode::Op32:
        addGprRead(facts, facts.rs1Field);
        addGprRead(facts, facts.rs2Field);
        addGprWrite(facts, facts.rdField);
        facts.mul = funct7 == 1 && funct3 < 4;
        facts.div = funct7 == 1 && funct3 >= 4;
        return true;
    case opcode::OpImm:
    case opcode::OpImm32:
        addGprRead(facts, facts.rs1Field);
        addGprWrite(facts, facts.rdField);
        return true;
    case opcode::Load:
        addGprRead(facts, facts.rs1Field);
        addGprWrite(facts, facts.rdField);
        facts.load = true;
        return true;
    case opcode::Store:
        addGprRead(facts, facts.rs1Field);
        addGprRead(facts, facts.rs2Field);
        facts.store = true;
        return true;
    case opcode::Lui:
        addGprWrite(facts, facts.rdField);
        return true;
    case opcode::Auipc:
        addGprWrite(facts, facts.rdField);
        facts.readsPc = true;
        return true;
    default:
        return false;
    }
}

/** jal, jalr and B-type branches. */
bool decodeControlTransfer(InstructionFacts &facts, uint8_t op, uint32_t word)
{
    switch (op)
    {
    case opcode::Jal:
        addGprWrite(facts, facts.rdField);
        facts.controlTransfer = facts.unconditionalJump = true;
        facts.branchOffsetBytes = jumpImmediate(word);
        return true;
    case opcode::Jalr:
        addGprRead(facts, facts.rs1Field);
        addGprWrite(facts, facts.rdField);
        facts.controlTransfer = facts.unconditionalJump = true;
        return true;
    case opcode::Branch:
        addGprRead(facts, facts.rs1Field);
        addGprRead(facts, facts.rs2Field);
        facts.controlTransfer = true;
        facts.branchOffsetBytes = branchImmediate(word);
        return true;
    default:
        return false;
    }
}

bool isFloatingPointOpcode(uint8_t op)
{
    return op == opcode::LoadFp || op == opcode::StoreFp || op == opcode::OpFp ||
           op == opcode::FmAdd || op == opcode::FmSub || op == opcode::FnmSub ||
           op == opcode::FnmAdd;
}

/** Kites' pipeline control signals for the word (see RV5SControlUnit / pipeline_decode). */
void decodeKitesSignals(InstructionFacts &facts, uint8_t op, uint32_t word)
{
    if (word == kNopWord) // pipeline_decode short-circuits a NOP before the control unit runs.
        return;
    facts.kitesRegWrite = kitesWritesGpr(op);
    facts.kitesMemRead = op == opcode::Load || op == opcode::LoadFp;
    facts.kitesFregWrite =
        instruction_set::isFInstruction(word) || instruction_set::isDInstruction(word);
}

void decodeWord(InstructionFacts &facts, uint32_t word, const SchedulingOptions &options)
{
    const uint8_t op = word & 0x7F;
    facts.word = word;
    facts.hasWord = true;
    facts.rdField = (word >> 7) & 0x1F;
    facts.rs1Field = (word >> 15) & 0x1F;
    facts.rs2Field = (word >> 20) & 0x1F;
    facts.rs3Field = static_cast<uint8_t>(word >> 27);

    if (isFloatingPointOpcode(op))
    {
        decodeFloatingPoint(facts, op, word);
        facts.barrier = !options.scheduleFloatingPoint;
    }
    else if (!decodeIntegerOp(facts, op, word) && !decodeControlTransfer(facts, op, word))
    {
        facts.barrier = true; // SYSTEM (ecall/csr), FENCE and anything unknown.
    }
    decodeKitesSignals(facts, op, word);
}

std::optional<RegId> parseRegister(const std::string &name)
{
    if (name == "zero")
        return RegId{RegFile::Gpr, 0};
    if (name.size() < 2 || (name[0] != 'x' && name[0] != 'f'))
        return std::nullopt;
    unsigned value = 0;
    const char *begin = name.data() + 1;
    const char *end = name.data() + name.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end || value > 31)
        return std::nullopt;
    return RegId{name[0] == 'x' ? RegFile::Gpr : RegFile::Fpr, static_cast<uint8_t>(value)};
}

bool isLoadMnemonic(const std::string &op)
{
    return op == "lb" || op == "lh" || op == "lw" || op == "ld" || op == "lbu" || op == "lhu" ||
           op == "lwu";
}

/** Phase-1 fallback: facts from the ICUnit's mnemonic and register strings. */
void describeFromStrings(InstructionFacts &facts, const ICUnit &unit)
{
    const std::string op = unit.getOpcode();
    bool unparsable = false;
    auto read = [&](const std::string &name)
    {
        if (name.empty())
            return;
        const auto reg = parseRegister(name);
        if (!reg)
            unparsable = true;
        else if (reg->file == RegFile::Fpr || reg->index != 0)
            facts.reads.push_back(*reg);
    };
    auto write = [&](const std::string &name)
    {
        if (name.empty())
            return;
        const auto reg = parseRegister(name);
        if (!reg)
            unparsable = true;
        else if (reg->file == RegFile::Fpr || reg->index != 0)
            facts.writes.push_back(*reg);
    };

    if (instruction_set::isValidRTypeInstruction(op))
    {
        read(unit.getRs1());
        read(unit.getRs2());
        write(unit.getRd());
        const bool mExtension =
            op.starts_with("mul") || op.starts_with("div") || op.starts_with("rem");
        facts.mul = mExtension && op.starts_with("mul");
        facts.div = mExtension && !facts.mul;
    }
    else if (instruction_set::isValidI1TypeInstruction(op) ||
             instruction_set::isValidI2TypeInstruction(op))
    {
        read(unit.getRs1());
        write(unit.getRd());
        facts.load = isLoadMnemonic(op);
        facts.controlTransfer = facts.unconditionalJump = op == "jalr";
    }
    else if (instruction_set::isValidSTypeInstruction(op))
    {
        read(unit.getRs1());
        read(unit.getRs2());
        facts.store = true;
    }
    else if (instruction_set::isValidBTypeInstruction(op))
    {
        read(unit.getRs1());
        read(unit.getRs2());
        facts.controlTransfer = true;
    }
    else if (instruction_set::isValidUTypeInstruction(op))
    {
        write(unit.getRd());
        facts.readsPc = op == "auipc";
    }
    else if (instruction_set::isValidJTypeInstruction(op))
    {
        write(unit.getRd());
        facts.controlTransfer = facts.unconditionalJump = true;
    }
    else
    {
        facts.barrier = true;
    }
    facts.barrier = facts.barrier || unparsable;
}
} // namespace

InstructionFacts describeWord(uint32_t word, const SchedulingOptions &options)
{
    InstructionFacts facts;
    decodeWord(facts, word, options);
    return facts;
}

InstructionFacts describeInstruction(const AssembledProgram &program, std::size_t programIndex,
                                     const SchedulingOptions &options)
{
    const ICUnit &unit = program.intermediate_code[programIndex].first;
    InstructionFacts facts;
    const bool haveWords = program.text_buffer.size() == program.intermediate_code.size() &&
                           unit.getInstructionIndex() < program.text_buffer.size();
    if (haveWords)
        decodeWord(facts, program.text_buffer[unit.getInstructionIndex()], options);
    else
        describeFromStrings(facts, unit);
    facts.programIndex = programIndex;
    facts.sourceLine = unit.getLineNumber();
    return facts;
}
} // namespace Kites
