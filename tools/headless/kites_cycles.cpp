// kites_cycles: headless runner for the Kites core.
//
//   kites_cycles <file.s> <hf|hnf|nhf|nhnf|rvss> [--trace] [--run|--debugrun] [--quiet-regs]
//
// Default execution path:
//   * rv5s modes: steps the processor one cycle at a time with exactly the loop of
//     RV5StageVM_Base::DebugRun() (Step() until program_counter_ >= program_size_ and
//     is_pipeline_drained()), so --trace can sample every cycle.
//   * rvss: RVSSProcessor::DebugRun() (its per-instruction stdout chatter is suppressed).
//   --run: call the processor's own Run() (step_delay_ = 0, sentinel breakpoint cleared).
//   --debugrun: call RV5StageVM_Base::DebugRun() literally. Neither combines with --trace.
//
// Trace semantics (rv5s): after each Step() we call setProcessorState() and print
// processor_state_.programCounters = {program_counter_, if_id_reg_.pc, id_ex_reg_.pc,
// ex_mem_reg_.pc, mem_wb_reg_.pc} labelled IF/ID/EX/MEM/WB exactly as the GUI does.
// These are pipeline-register contents at the END of cycle N, i.e. the instruction that
// occupies each stage during cycle N+1. INVALID_PC (UINT64_MAX) = bubble, printed "--".
#include "assembler/assembler.h"
#include "processor/processor_constants.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "processor/rvss/rvss_processor.h"
#include "utils/utils.h"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <streambuf>
#include <string>

using namespace Kites;

namespace
{
// Friend-free access to the protected pipeline internals: a thin subclass that
// re-exports the protected members we need via using-declarations.
template <typename P> class Traceable : public P
{
  public:
    using RV5StageVM_Base::if_id_reg_;
    using RV5StageVM_Base::id_ex_reg_;
    using RV5StageVM_Base::ex_mem_reg_;
    using RV5StageVM_Base::mem_wb_reg_;
    using RV5StageVM_Base::is_pipeline_drained;
    using RV5StageVM_Base::setProcessorState;
};

struct NullBuf : std::streambuf
{
    int overflow(int c) override { return c; }
};

class CoutSilencer
{
  public:
    explicit CoutSilencer(bool on) : old_(on ? std::cout.rdbuf(&null_) : nullptr) {}
    ~CoutSilencer()
    {
        if (old_) std::cout.rdbuf(old_);
    }

  private:
    NullBuf null_;
    std::streambuf *old_;
};

std::vector<std::string> g_srcLines; // source file lines, for labelling PCs in --trace

std::string pcStr(const AssembledProgram &prog, uint64_t pc, bool withText)
{
    if (pc == INVALID_PC) return "--";
    std::ostringstream os;
    os << "0x" << std::hex << std::setw(2) << std::setfill('0') << pc;
    if (withText)
    {
        // instruction index -> 1-based source line (instruction_number_instruction_text_mapping is never filled)
        auto it = prog.instruction_number_line_number_mapping.find(static_cast<unsigned>(pc / 4));
        if (it != prog.instruction_number_line_number_mapping.end() && it->second >= 1 &&
            it->second <= g_srcLines.size())
        {
            std::string t = g_srcLines[it->second - 1];
            auto b = t.find_first_not_of(" \t");
            t = b == std::string::npos ? "" : t.substr(b);
            if (auto c = t.find('#'); c != std::string::npos) t = t.substr(0, c);
            while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.pop_back();
            if (t.size() > 17) t = t.substr(0, 17);
            os << " " << t;
        }
    }
    return os.str();
}

struct RunResult
{
    unsigned long derivedStalls = 0; // cycles where ID held + EX got a bubble
    unsigned long derivedFlushes = 0; // cycles where IF/ID and ID/EX were both squashed
};

template <typename P>
RunResult stepLoop(P &p, bool trace)
{
    RunResult r;
    const auto &prog = p.program_;
    std::vector<uint64_t> prev{INVALID_PC, INVALID_PC, INVALID_PC, INVALID_PC, INVALID_PC};
    if (trace)
        std::cout << std::left << std::setw(6) << "cycle" << std::setw(24) << "IF" << std::setw(24) << "ID"
                  << std::setw(24) << "EX" << std::setw(24) << "MEM" << std::setw(24) << "WB" << "event\n";
    p.ClearStop();
    while (!p.stop_requested_ && (p.program_counter_ < p.program_size_ || !p.is_pipeline_drained()))
    {
        p.Step();
        p.setProcessorState();
        const auto &pcs = p.processor_state_.programCounters; // {IF, ID, EX, MEM, WB}
        std::string ev;
        if (pcs[1] != INVALID_PC && pcs[1] == prev[1] && pcs[0] == prev[0] && pcs[2] == INVALID_PC)
        {
            ev = "STALL (IF/ID held, bubble into EX)";
            ++r.derivedStalls;
        }
        else if (pcs[1] == INVALID_PC && pcs[2] == INVALID_PC && prev[1] != INVALID_PC && prev[2] != INVALID_PC)
        {
            ev = "FLUSH (IF/ID + ID/EX squashed)";
            ++r.derivedFlushes;
        }
        if (trace)
        {
            std::cout << std::left << std::setw(6) << p.cycle_s_;
            for (int s = 0; s < 5; ++s) std::cout << std::setw(24) << pcStr(prog, pcs[s], true);
            std::cout << ev << "\n";
        }
        prev = pcs;
    }
    return r;
}

void usage()
{
    std::cerr << "usage: kites_cycles <file.s> <hf|hnf|nhf|nhnf|rvss> [--trace] [--run|--debugrun] [--quiet-regs]\n";
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        usage();
        return 2;
    }
    std::string file = argv[1], mode = argv[2];
    bool trace = false, useRun = false, useDebugRun = false, quietRegs = false;
    for (int i = 3; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--trace")) trace = true;
        else if (!std::strcmp(argv[i], "--run")) useRun = true;
        else if (!std::strcmp(argv[i], "--debugrun")) useDebugRun = true;
        else if (!std::strcmp(argv[i], "--quiet-regs")) quietRegs = true;
        else
        {
            usage();
            return 2;
        }
    }
    if (trace && (useRun || useDebugRun || mode == "rvss"))
    {
        std::cerr << "--trace needs an rv5s mode and the step loop (no --run)\n";
        return 2;
    }

    setupVmStateDirectory(); // creates ./vm_state (globals are bound to the cwd at startup)
    AssembledProgram program;
    try
    {
        program = assemble(file);
        std::ifstream in(file);
        for (std::string l; std::getline(in, l);) g_srcLines.push_back(l);
    }
    catch (const std::exception &e)
    {
        std::cerr << "assembly failed: " << e.what() << "\n";
        return 1;
    }

    std::unique_ptr<ProcessorBase> proc;
    RunResult rr;
    bool haveDerived = false;

    auto runRv5 = [&](auto tag) {
        using P = typename decltype(tag)::type;
        auto p = std::make_unique<Traceable<P>>();
        p->LoadProgram(program);
        if (useRun)
        {
            p->step_delay_ = 0;
            // LoadProgram() adds a sentinel breakpoint at program_size_; rv5s Run() would
            // pause on it while draining and block forever. The GUI replaces it via
            // SetBreakpoints(); do the same with an empty list.
            p->SetBreakpoints({});
            CoutSilencer q(true);
            static_cast<ProcessorBase &>(*p).Run(); // Run() is protected in RV5StageVM_Base, public in ProcessorBase
        }
        else if (useDebugRun)
        {
            CoutSilencer q(true); // DebugRun prints "Cycle: N | PC: ..." every cycle
            p->DebugRun();
        }
        else
        {
            rr = stepLoop(*p, trace);
            haveDerived = true;
        }
        proc = std::move(p);
    };
    if (mode == "hf") runRv5(std::type_identity<RV5StageProcessorHF>{});
    else if (mode == "hnf") runRv5(std::type_identity<RV5StageProcessorHNF>{});
    else if (mode == "nhf") runRv5(std::type_identity<RV5StageProcessorNHF>{});
    else if (mode == "nhnf") runRv5(std::type_identity<RV5StageProcessorNHNF>{});
    else if (mode == "rvss")
    {
        auto p = std::make_unique<RVSSProcessor>();
        p->LoadProgram(program);
        p->step_delay_ = 0;
        if (useRun) p->SetBreakpoints({}); // see rv5s note above
        {
            CoutSilencer q(true);
            if (useRun) p->Run();
            else p->DebugRun();
        }
        proc = std::move(p);
    }
    else
    {
        usage();
        return 2;
    }

    std::cout << "mode: " << mode << "  file: " << file << "  driver: "
              << (useRun ? "Run()" : (mode == "rvss" || useDebugRun ? "DebugRun()" : "Step() loop (== DebugRun)")) << "\n";
    std::cout << "cycles: " << proc->cycle_s_ << "\n";
    std::cout << "instructions_retired: " << proc->instructions_retired_ << "\n";
    if (haveDerived)
        std::cout << "stall_cycles (derived from trace): " << rr.derivedStalls << "\n"
                  << "flush_events (derived from trace): " << rr.derivedFlushes << "\n";
    std::cout << "stall_cycles_ (raw member; a countdown in H_F/H_NF, ends at 0): " << proc->stall_cycles_ << "\n";
    std::cout << "branch_mispredictions: " << proc->branch_mispredictions_ << "\n";
    if (proc->instructions_retired_)
        std::cout << "CPI: " << std::fixed << std::setprecision(3)
                  << double(proc->cycle_s_) / proc->instructions_retired_ << "\n";

    if (!quietRegs)
    {
        auto g = proc->registers_.GetGprValues();
        for (size_t i = 0; i < g.size(); ++i)
            if (g[i]) std::cout << "x" << std::dec << i << " = " << int64_t(g[i]) << " (0x" << std::hex << g[i] << ")\n";
        auto f = proc->registers_.GetFprValues();
        for (size_t i = 0; i < f.size(); ++i)
            if (f[i])
            {
                std::cout << "f" << std::dec << i << " = 0x" << std::hex << f[i] << std::dec;
                if ((f[i] >> 32) == 0xFFFFFFFFu)
                {
                    uint32_t b = uint32_t(f[i]);
                    float v;
                    std::memcpy(&v, &b, 4);
                    std::cout << " (float " << v << ")";
                }
                else
                {
                    double v;
                    std::memcpy(&v, &f[i], 8);
                    std::cout << " (double " << v << ")";
                }
                std::cout << "\n";
            }
        std::cout << std::dec;
    }
    return 0;
}
