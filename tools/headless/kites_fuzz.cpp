// kites_fuzz: differential fuzzer for the Phase-2 scheduler (review tool, outside the repo).
//
//   kites_fuzz <seeds> [firstSeed] [--fp] [--dump dir] [--profile] [--select] [--emit]
//
// --profile: profile each program on HF/HNF/NH_F/NH_NF and check the accounting identity
//            (and, for straight-line programs, per-line data stalls against the static model).
// --select:  straight-line programs with label-separated blocks; every subset of accepted
//            blocks (up to 256) is masked, applied, re-assembled, verified and run on RVSS and
//            Kites; model cycles must equal Kites cycles.
//
// Generates random programs (ALU/M/W ops, many load/store widths with two aliasing bases,
// x0 writes, li/lui pseudo-instructions, forward branches, jal, counted loops, FP ops, labels
// on own/same line, comments, blank lines, tabs). For every program and for HF and HNF
// options: analyze, rewrite, re-assemble, verify; then run original and rewritten on RVSS
// (architectural reference) and on the Kites 5-stage processor; compare GPR/FPR/data memory.
#include "assembler/assembler.h"
#include "config/config.h"
#include "processor/rvss/rvss_processor.h"
#include "scheduler/runtime/kites_trace_runner.h"
#include "scheduler/selection_evaluator.h"
#include "scheduler/basic_block_scheduler.h"
#include "scheduler/source_rewriter.h"
#include "scheduler/suggestion_engine.h"
#include "utils/utils.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <streambuf>

using namespace Kites;

namespace
{
struct NullBuf : std::streambuf
{
    int overflow(int c) override { return c; }
};
struct Silence
{
    NullBuf n;
    std::streambuf *o;
    Silence() : o(std::cout.rdbuf(&n)) {}
    ~Silence() { std::cout.rdbuf(o); }
};

struct State
{
    bool ok{};
    std::string error;
    std::vector<uint64_t> gpr, fpr;
    std::vector<uint8_t> mem;
    bool operator==(const State &o) const { return gpr == o.gpr && fpr == o.fpr && mem == o.mem; }
};

State runRvss(const AssembledProgram &p)
{
    State s;
    RVSSProcessor proc;
    Silence q;
    try
    {
        proc.LoadProgram(p);
        proc.SetBreakpoints({});
        for (int i = 0; i < 20000 && proc.GetProgramCounter() < proc.program_size_; ++i)
            proc.Step();
    }
    catch (const std::exception &e)
    {
        s.error = e.what();
        return s;
    }
    s.ok = true;
    s.gpr = proc.registers_.GetGprValues();
    s.fpr = proc.registers_.GetFprValues();
    for (int i = 0; i < 256; ++i)
        s.mem.push_back(proc.memory_controller_.peekByte(0x10000000 + i));
    return s;
}

struct Gen
{
    std::mt19937 rng;
    bool fp;
    std::ostringstream out;
    int labelCounter = 0;
    std::vector<int> pendingLabels; // forward labels still to be placed

    bool straight = false; ///< No branches, loops or jumps; unreferenced labels split blocks.

    explicit Gen(unsigned seed, bool fp) : rng(seed), fp(fp) {}
    int r(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); }
    bool p(int pct) { return r(100) < pct; }

    std::string gpr(bool allowX0 = true)
    {
        static const int pool[] = {1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 28, 29, 30, 31};
        if (allowX0 && p(6))
            return "x0";
        return "x" + std::to_string(pool[r(16)]);
    }
    std::string src() { return p(4) ? "x21" : gpr(); }
    std::string freg() { return "f" + std::to_string(r(6)); }
    std::string base() { return p(50) ? "x20" : "x21"; }
    std::string ind() { static const char *s[] = {"    ", "\t", "  ", "        "}; return s[r(4)]; }

    std::string instr()
    {
        static const char *rops[] = {"add", "sub", "and", "or",   "xor",  "sll",  "srl",
                                     "sra", "slt", "sltu", "mul", "mulh", "mulhu", "div",
                                     "divu", "rem", "remu", "mulw", "divw", "remw", "divuw", "remuw", "mulhsu", "add", "sub", "or"};
        static const char *iops[] = {"addi", "xori", "ori", "andi", "slti", "sltiu", "addi"};
        static const char *sh[] = {"slli", "srli", "srai"};
        static const char *ld[] = {"lb", "lbu", "lh", "lhu", "lw", "lwu", "ld"};
        static const int ldw[] = {1, 1, 2, 2, 4, 4, 8};
        static const char *st[] = {"sb", "sh", "sw", "sd"};
        static const int stw[] = {1, 2, 4, 8};
        const int k = r(fp ? 115 : 100);
        if (k < 30)
            return std::string(rops[r(25)]) + " " + gpr() + ", " + src() + ", " + src();
        if (k < 42)
            return std::string(iops[r(7)]) + " " + gpr() + ", " + src() + ", " + std::to_string(r(4096) - 2048);
        if (k < 46)
            return std::string(sh[r(3)]) + " " + gpr() + ", " + src() + ", " + std::to_string(r(64));
        if (k < 64)
        {
            const int i = r(7);
            return std::string(ld[i]) + " " + gpr() + ", " + std::to_string(ldw[i] * r(64 / ldw[i])) + "(" + base() + ")";
        }
        if (k < 78)
        {
            const int i = r(4);
            return std::string(st[i]) + " " + src() + ", " + std::to_string(stw[i] * r(64 / stw[i])) + "(" + base() + ")";
        }
        if (k < 82)
            return "lui " + gpr() + ", " + std::to_string(r(1 << 20));
        if (k < 87)
            return "li " + gpr() + ", " + std::to_string(static_cast<int64_t>(rng()) - (1LL << 31));
        if (k < 90)
            return "mv " + gpr() + ", " + src();
        if (k < 92)
            return "neg " + gpr() + ", " + src();
        if (k < 94)
            return "addi x21, x20, " + std::to_string(8 * r(9)); // re-alias the second base
        if (k < 95)
            return p(50) || straight ? "auipc " + gpr() + ", " + std::to_string(r(16))
                                     : "la " + gpr() + ", buf";
        if (k < 96)
            return p(70) ? "nop" : "fence";
        if (k < 100)
            return "sext.w " + gpr() + ", " + src();
        // FP
        const int f = r(12);
        switch (f)
        {
        case 0: return "flw " + freg() + ", " + std::to_string(4 * r(16)) + "(" + base() + ")";
        case 1: return "fsw " + freg() + ", " + std::to_string(4 * r(16)) + "(" + base() + ")";
        case 2: return "fadd.s " + freg() + ", " + freg() + ", " + freg();
        case 3: return "fmul.s " + freg() + ", " + freg() + ", " + freg();
        case 4: return "fcvt.s.w " + freg() + ", " + src();
        case 5: return "fcvt.w.s " + gpr() + ", " + freg();
        case 6: return "fmv.x.w " + gpr() + ", " + freg();
        case 7: return "fmv.w.x " + freg() + ", " + src();
        case 8: return "feq.s " + gpr() + ", " + freg() + ", " + freg();
        case 9: return "fld " + freg() + ", " + std::to_string(8 * r(8)) + "(" + base() + ")";
        case 10: return "fsd " + freg() + ", " + std::to_string(8 * r(8)) + "(" + base() + ")";
        default: return "fmadd.s " + freg() + ", " + freg() + ", " + freg() + ", " + freg();
        }
    }

    void line(const std::string &text, const std::string &label = "")
    {
        if (!label.empty())
        {
            if (p(50))
                out << label << ":\n";
            else
            {
                out << label << ": " << text;
                if (p(20))
                    out << "   # c";
                out << "\n";
                return;
            }
        }
        out << ind() << text;
        if (p(15))
            out << (p(50) ? "  # note " : "\t; semi ") << r(100);
        if (p(5))
            out << "   ";
        out << "\n";
        if (p(6))
            out << "\n";
        if (p(4))
            out << "# full-line comment\n";
    }

    void segment(int n, bool allowBranches)
    {
        for (int i = 0; i < n; ++i)
        {
            std::string label;
            if (straight && p(12))
                label = "S" + std::to_string(labelCounter++);
            else if (!pendingLabels.empty() && p(15))
            {
                label = "L" + std::to_string(pendingLabels.back());
                pendingLabels.pop_back();
            }
            if (allowBranches && p(7))
            {
                const int l = labelCounter++;
                pendingLabels.insert(pendingLabels.begin(), l);
                static const char *br[] = {"beq", "bne", "blt", "bge", "bltu", "bgeu"};
                if (p(25))
                    line("jal x1, L" + std::to_string(l), label);
                else
                    line(std::string(br[r(6)]) + " " + gpr() + ", " + gpr() + ", L" + std::to_string(l), label);
                continue;
            }
            line(instr(), label);
        }
    }

    std::string program()
    {
        out << ".data\nbuf:";
        for (int i = 0; i < 64; ++i)
            out << (i ? ", " : " .word ") << static_cast<int32_t>(rng());
        out << "\n.text\n";
        line("li x20, 0x10000000");
        line("addi x21, x20, " + std::to_string(8 * r(9)));
        for (int reg : {1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 28, 29, 30, 31})
            if (p(70))
                line("li x" + std::to_string(reg) + ", " + std::to_string(static_cast<int32_t>(rng()) >> r(30)));
        if (fp)
            for (int f = 0; f < 6; ++f)
                line("flw f" + std::to_string(f) + ", " + std::to_string(4 * f) + "(x20)");
        const int parts = 1 + r(4);
        for (int s = 0; s < parts; ++s)
        {
            if (!straight && p(30))
            {
                // counted loop; body must not write x22 or branch
                const int l = labelCounter++;
                line("li x22, " + std::to_string(1 + r(4)));
                std::string lbl = "LOOP" + std::to_string(l);
                std::ostringstream save;
                segment(0, false);
                out << lbl << ":" << (p(50) ? "\n" : "");
                const int n = 2 + r(8);
                for (int i = 0; i < n; ++i)
                    line(instr());
                line("addi x22, x22, -1");
                line("bne x22, x0, " + lbl);
            }
            else
                segment(3 + r(14), !straight);
        }
        while (!pendingLabels.empty())
        {
            out << "L" << pendingLabels.back() << ":\n";
            pendingLabels.pop_back();
        }
        line("addi x23, x0, 1");
        return out.str();
    }
};

AssembledProgram assembleText(const std::string &t)
{
    std::istringstream in(t);
    Silence q;
    return assemble(in);
}

bool isStraightLine(const AssembledProgram &prog)
{
    for (uint32_t word : prog.text_buffer)
    {
        const uint32_t opcode = word & 0x7F;
        if (opcode == 0b1100011 || opcode == 0b1101111 || opcode == 0b1100111 || opcode == 0b1110011)
            return false;
    }
    return true;
}

/** --profile: identity on every mode; straight-line per-line data stalls == static model. */
int fuzzProfile(unsigned first, unsigned seeds, bool fp, const std::string &dump)
{
    static const ProcessorType kModes[] = {ProcessorType::RV5Stage_H_F, ProcessorType::RV5Stage_H_NF,
                                           ProcessorType::RV5Stage_NH_F, ProcessorType::RV5Stage_NH_NF};
    KitesTraceRunner runner;
    unsigned runs = 0, identityFail = 0, cycleFail = 0, lineFail = 0, errors = 0, straightRuns = 0;
    for (unsigned seed = first; seed < first + seeds; ++seed)
    {
        Gen g(seed, fp);
        g.straight = seed % 2 == 0;
        const std::string src = g.program();
        AssembledProgram prog;
        try { prog = assembleText(src); }
        catch (...) { continue; }
        const bool straight = isStraightLine(prog);
        for (ProcessorType mode : kModes)
        {
            TraceRunOptions ro{mode};
            ro.maxCycles = 20000;
            ProfileRunResult pr;
            TraceRunResult plain;
            { Silence q; pr = profileOnKites(runner, prog, src, ro); plain = runner.run(prog, ro); }
            const std::string tag = std::to_string(seed) + "_" + std::to_string(static_cast<int>(mode));
            if (!pr.ok || !plain.ok)
            {
                if (pr.ok != plain.ok) { ++errors; std::cerr << "profile/run disagree on error " << tag << "\n"; }
                continue;
            }
            ++runs;
            const StallProfile &p = pr.profile;
            if (!p.identityHolds())
            {
                ++identityFail;
                std::cerr << "IDENTITY " << tag << ": " << p.identityReport() << "\n";
                std::ofstream(dump + "_ident_" + tag + ".s") << src;
            }
            if (!plain.trace.hitCycleLimit && p.cycles != plain.trace.kitesCycleCount)
            {
                ++cycleFail;
                std::cerr << "CYCLES " << tag << ": profile " << p.cycles << " run " << plain.trace.kitesCycleCount << "\n";
            }
            const bool hazard = mode == ProcessorType::RV5Stage_H_F || mode == ProcessorType::RV5Stage_H_NF;
            if (straight && hazard)
            {
                ++straightRuns;
                SchedulingOptions opt{.forwardingEnabled = mode == ProcessorType::RV5Stage_H_F};
                opt.scheduleFloatingPoint = true;
                const SchedulingResult sched = BasicBlockScheduler{}.schedule(prog, opt);
                std::map<unsigned, uint64_t> model, profiled;
                for (const BasicBlockSchedule &b : sched.blocks)
                    for (std::size_t u = 0; u < b.units.size(); ++u)
                        if (b.originalStalls[u].cycles)
                            model[b.units[u].sourceLine] += b.originalStalls[u].cycles;
                for (const LineProfile &l : p.lines)
                {
                    uint64_t data = 0;
                    for (std::size_t c = 0; c < kLostCycleCauseCount; ++c)
                        if (isDataStallCause(LostCycleCause(c)))
                            data += l.byCause[c];
                    if (data)
                        profiled[l.sourceLine] = data;
                }
                if (model != profiled)
                {
                    ++lineFail;
                    std::cerr << "PER-LINE " << tag << ":";
                    for (auto &[l, c] : model) if (profiled[l] != c) std::cerr << " L" << l << " model " << c << " prof " << profiled[l];
                    for (auto &[l, c] : profiled) if (!model.count(l)) std::cerr << " L" << l << " model 0 prof " << c;
                    std::cerr << "\n";
                    std::ofstream(dump + "_line_" + tag + ".s") << src;
                }
            }
        }
    }
    std::cout << "profile runs " << runs << " (straight-line H runs " << straightRuns << ") identityFail "
              << identityFail << " cycleFail " << cycleFail << " perLineFail " << lineFail << " errors " << errors << "\n";
    return identityFail || cycleFail || lineFail || errors ? 1 : 0;
}

/** --select: every subset of accepted blocks of straight-line programs. */
int fuzzSelect(unsigned first, unsigned seeds, bool fp, const std::string &dump)
{
    KitesTraceRunner runner;
    unsigned programs = 0, subsets = 0, unsafe = 0, applied = 0, asmFail = 0, verifyFail = 0,
             rvssFail = 0, kitesFail = 0, cycleFail = 0, fullInconsistent = 0;
    for (unsigned seed = first; seed < first + seeds; ++seed)
    {
        Gen g(seed, fp);
        g.straight = true;
        const std::string src = g.program();
        AssembledProgram prog;
        try { prog = assembleText(src); }
        catch (...) { continue; }
        const State refOrig = runRvss(prog);
        if (!refOrig.ok) continue;
        ++programs;
        for (bool fwd : {true, false})
        {
            SchedulingOptions opt{.forwardingEnabled = fwd};
            ProgramSuggestion full;
            { Silence q; full = SuggestionEngine{}.analyze(src, prog, opt); }
            if (full.status != ProgramSuggestion::Status::Improved) continue;
            std::vector<std::size_t> accepted;
            for (std::size_t b = 0; b < full.schedule.blocks.size(); ++b)
                if (full.schedule.blocks[b].accepted) accepted.push_back(b);
            TraceRunOptions ro{fwd ? ProcessorType::RV5Stage_H_F : ProcessorType::RV5Stage_H_NF};
            TraceRunResult kOrig;
            { Silence q; kOrig = runner.run(prog, ro); }
            const std::size_t k = std::min<std::size_t>(accepted.size(), 8);
            for (unsigned mask = 0; mask < (1u << k); ++mask)
            {
                std::vector<bool> sel(full.schedule.blocks.size(), false);
                for (std::size_t i = 0; i < k; ++i)
                    sel[accepted[i]] = (mask >> i) & 1;
                for (std::size_t i = k; i < accepted.size(); ++i)
                    sel[accepted[i]] = true;
                ++subsets;
                const SelectionEvaluation ev = evaluateSelection(prog, full, sel);
                const std::string tag = std::to_string(seed) + (fwd ? "_hf_" : "_hnf_") + std::to_string(mask);
                const bool all = mask == (1u << k) - 1;
                if (all && ev.safe != full.canApply)
                {
                    ++fullInconsistent;
                    std::cerr << "FULL-SELECTION safe=" << ev.safe << " but engine canApply=" << full.canApply
                              << " " << tag << " (" << ev.unsafeReason << " / " << full.applyBlockedReason << ")\n";
                }
                if (all && ev.stallCount != full.suggestedStaticStalls)
                {
                    ++fullInconsistent;
                    std::cerr << "FULL-SELECTION stalls " << ev.stallCount << " != engine " << full.suggestedStaticStalls << " " << tag << "\n";
                }
                if (!ev.safe) { ++unsafe; continue; }
                const ProgramSuggestion masked = maskSuggestion(src, full, ev);
                if (!masked.canApply) continue;
                ++applied;
                AssembledProgram prog2;
                try { prog2 = assembleText(masked.rewrittenSource); }
                catch (...) { ++asmFail; std::ofstream(dump + "_selasm_" + tag + ".s") << src; continue; }
                RewriteVerification v;
                { Silence q; v = verifyRewrittenProgram(prog, prog2, masked.rewrittenSource, masked); }
                if (!v.ok) { ++verifyFail; std::cerr << "verify " << tag << ": " << v.reason << "\n"; std::ofstream(dump + "_selverify_" + tag + ".s") << src; }
                const State ref2 = runRvss(prog2);
                if (!(ref2 == refOrig)) { ++rvssFail; std::cerr << "RVSS MISMATCH " << tag << "\n"; std::ofstream(dump + "_selrvss_" + tag + ".s") << src; }
                TraceRunResult kNew;
                { Silence q; kNew = runner.run(prog2, ro); }
                if (kNew.gpr != kOrig.gpr || kNew.fpr != kOrig.fpr || kNew.dataMemory != kOrig.dataMemory)
                {
                    ++kitesFail;
                    std::cerr << "KITES MISMATCH " << tag << " (rvss same " << (ref2 == refOrig) << ")\n";
                    std::ofstream(dump + "_selkites_" + tag + ".s") << src;
                    std::ofstream(dump + "_selkites_" + tag + "_new.s") << masked.rewrittenSource;
                }
                if (kNew.trace.kitesCycleCount != ev.straightLineCycles)
                {
                    ++cycleFail;
                    std::cerr << "CYCLES " << tag << ": model " << ev.straightLineCycles << " Kites " << kNew.trace.kitesCycleCount << "\n";
                    std::ofstream(dump + "_selcyc_" + tag + ".s") << src;
                }
            }
        }
    }
    std::cout << "select programs " << programs << " subsets " << subsets << " unsafe " << unsafe << " applied " << applied
              << " asmFail " << asmFail << " verifyFail " << verifyFail << " RVSS-mismatch " << rvssFail
              << " Kites-mismatch " << kitesFail << " cycle-mismatch " << cycleFail << " full-inconsistent " << fullInconsistent << "\n";
    return asmFail || verifyFail || rvssFail || kitesFail || cycleFail || fullInconsistent ? 1 : 0;
}
} // namespace

int main(int argc, char **argv)
{
    unsigned seeds = argc > 1 ? std::stoul(argv[1]) : 100;
    unsigned first = argc > 2 && argv[2][0] != '-' ? std::stoul(argv[2]) : 1;
    bool fp = false;
    bool fpSched = false;
    std::string dump = "fuzz_fail";
    for (int i = 2; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--fp")) fp = true;
        if (!std::strcmp(argv[i], "--fpsched")) fp = fpSched = true;
        if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
    }
    setupVmStateDirectory();
    for (int i = 2; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--profile")) return fuzzProfile(first, seeds, fp, dump);
        if (!std::strcmp(argv[i], "--select")) return fuzzSelect(first, seeds, fp, dump);
        if (!std::strcmp(argv[i], "--emit")) // write the generated programs (<dump>_<seed>.s) only
        {
            for (unsigned seed = first; seed < first + seeds; ++seed)
            {
                Gen g(seed, fp);
                g.straight = seed % 2 == 0;
                std::ofstream(dump + "_" + std::to_string(seed) + ".s") << g.program();
            }
            return 0;
        }
    }
    unsigned programs = 0, applied = 0, rvssFail = 0, kitesFail = 0, verifyFail = 0, asmFail = 0,
             refused = 0, rvssBad = 0;
    KitesTraceRunner runner;
    for (unsigned seed = first; seed < first + seeds; ++seed)
    {
        Gen g(seed, fp);
        const std::string src = g.program();
        AssembledProgram prog;
        try { prog = assembleText(src); }
        catch (const std::exception &e) { ++asmFail; std::ofstream(dump + "_asm_" + std::to_string(seed) + ".s") << src; continue; }
        ++programs;
        const State refOrig = runRvss(prog);
        if (!refOrig.ok) { ++rvssBad; continue; }
        for (bool fwd : {true, false})
        {
            SchedulingOptions opt{.forwardingEnabled = fwd};
            opt.scheduleFloatingPoint = fpSched;
            ProgramSuggestion s;
            { Silence q; s = SuggestionEngine{}.analyze(src, prog, opt); }
            if (s.status == ProgramSuggestion::Status::Improved && !s.canApply) ++refused;
            if (!s.canApply) continue;
            ++applied;
            const std::string tag = std::to_string(seed) + (fwd ? "_hf" : "_hnf");
            AssembledProgram prog2;
            try { prog2 = assembleText(s.rewrittenSource); }
            catch (...) { ++asmFail; std::ofstream(dump + "_reasm_" + tag + ".s") << src; continue; }
            RewriteVerification v;
            { Silence q; v = verifyRewrittenProgram(prog, prog2, s.rewrittenSource, s); }
            if (!v.ok) { ++verifyFail; std::cerr << "verify " << tag << ": " << v.reason << "\n"; std::ofstream(dump + "_verify_" + tag + ".s") << src; }
            const State ref2 = runRvss(prog2);
            if (!(ref2 == refOrig))
            {
                ++rvssFail;
                std::cerr << "RVSS MISMATCH " << tag << "\n";
                std::ofstream(dump + "_rvss_" + tag + ".s") << src;
                std::ofstream(dump + "_rvss_" + tag + "_new.s") << s.rewrittenSource;
            }
            TraceRunOptions ro{fwd ? ProcessorType::RV5Stage_H_F : ProcessorType::RV5Stage_H_NF};
            TraceRunResult a, b;
            { Silence q; a = runner.run(prog, ro); b = runner.run(prog2, ro); }
            if (a.gpr != b.gpr || a.fpr != b.fpr || a.dataMemory != b.dataMemory || a.ok != b.ok)
            {
                ++kitesFail;
                std::cerr << "KITES MISMATCH " << tag << " (rvss same: " << (ref2 == refOrig) << ")\n";
                std::ofstream(dump + "_kites_" + tag + ".s") << src;
                std::ofstream(dump + "_kites_" + tag + "_new.s") << s.rewrittenSource;
            }
        }
    }
    std::cout << "programs " << programs << " applied " << applied << " refused " << refused
              << " asmFail " << asmFail << " rvssBad " << rvssBad << " verifyFail " << verifyFail
              << " RVSS-mismatch " << rvssFail << " Kites-mismatch " << kitesFail << "\n";
    return rvssFail || kitesFail || verifyFail ? 1 : 0;
}
