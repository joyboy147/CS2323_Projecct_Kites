// kites_profile: headless stall profiler for Kites 5-stage runs.
//
//   kites_profile <file.s> <hf|hnf|nhf|nhnf> [--max-cycles N]
//   kites_profile <file.s> --table      one markdown row: cycles and top causes for all 4 modes
//
// Prints the accounting identity (cycles = retired + stall + flush + fill + truncated), the
// per-cause totals, the per-line table and, in NH modes, unprotected data hazards.
#include "assembler/assembler.h"
#include "scheduler/runtime/kites_trace_runner.h"
#include "scheduler/stall_profiler.h"
#include "utils/utils.h"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace Kites;

namespace
{
struct Mode
{
    const char *name;
    ProcessorType type;
};
constexpr Mode kModes[] = {{"hf", ProcessorType::RV5Stage_H_F},
                           {"hnf", ProcessorType::RV5Stage_H_NF},
                           {"nhf", ProcessorType::RV5Stage_NH_F},
                           {"nhnf", ProcessorType::RV5Stage_NH_NF}};

class CoutSilencer
{
  public:
    CoutSilencer() : m_old(std::cout.rdbuf(nullptr)) {}
    ~CoutSilencer() { std::cout.rdbuf(m_old); }

  private:
    std::streambuf *m_old;
};

ProfileRunResult profile(const AssembledProgram &program, const std::string &source,
                         ProcessorType type, unsigned maxCycles)
{
    static KitesTraceRunner runner;
    TraceRunOptions options;
    options.processor = type;
    options.maxCycles = maxCycles;
    const CoutSilencer quiet; // Kites prints "Branch taken" lines from Step()
    return profileOnKites(runner, program, source, options);
}

std::string topCauses(const StallProfile &p)
{
    std::ostringstream out;
    bool first = true;
    for (std::size_t c = 0; c < kLostCycleCauseCount; ++c)
    {
        const auto cause = static_cast<LostCycleCause>(c);
        if (p.byCause[c] == 0 || cause == LostCycleCause::PipelineFill)
            continue;
        out << (first ? "" : ", ") << causeLabel(cause) << " " << p.byCause[c];
        first = false;
    }
    return first ? "-" : out.str();
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: kites_profile <file.s> <hf|hnf|nhf|nhnf|--table> [--max-cycles N]\n";
        return 2;
    }
    const std::string file = argv[1];
    const std::string mode = argv[2];
    unsigned maxCycles = 2000000;
    for (int i = 3; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--max-cycles"))
            maxCycles = static_cast<unsigned>(std::stoul(argv[i + 1]));

    setupVmStateDirectory();
    std::ifstream in(file);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();
    AssembledProgram program;
    try
    {
        std::istringstream text(source);
        program = assemble(text);
    }
    catch (const std::exception &e)
    {
        std::cerr << "assembly failed: " << e.what() << "\n";
        return 1;
    }

    if (mode == "--table")
    {
        std::cout << "| `" << file.substr(file.find_last_of('/') + 1) << "` ";
        std::string causes;
        bool allHold = true;
        for (const Mode &m : kModes)
        {
            const ProfileRunResult r = profile(program, source, m.type, maxCycles);
            if (!r.ok)
            {
                std::cout << "| error ";
                allHold = false;
                continue;
            }
            std::cout << "| " << r.profile.cycles << " ";
            allHold = allHold && r.profile.identityHolds();
            if (m.type == ProcessorType::RV5Stage_H_F || m.type == ProcessorType::RV5Stage_H_NF)
                causes += std::string(causes.empty() ? "" : " / ") + m.name + ": " +
                          topCauses(r.profile);
        }
        std::cout << "| " << causes << " | " << (allHold ? "yes" : "NO") << " |\n";
        return 0;
    }

    ProcessorType type = ProcessorType::RV5Stage_H_F;
    bool known = false;
    for (const Mode &m : kModes)
        if (mode == m.name)
        {
            type = m.type;
            known = true;
        }
    if (!known)
    {
        std::cerr << "unknown mode " << mode << "\n";
        return 2;
    }
    const ProfileRunResult r = profile(program, source, type, maxCycles);
    if (!r.ok)
    {
        std::cerr << "run failed: " << r.error << "\n";
        return 1;
    }
    const StallProfile &p = r.profile;
    std::cout << "mode " << mode << ": " << p.cycles << " cycles = " << p.retired << " retired + "
              << p.stallCycles << " stall + " << p.flushCycles << " flush + " << p.fillCycles
              << " fill + " << p.truncatedCycles << " truncated"
              << (p.hitCycleLimit ? " (cycle limit hit)" : "") << "\n";
    std::cout << "identity: " << (p.identityHolds() ? "holds" : "BROKEN: " + p.identityReport())
              << " (Kites: " << p.kites.cycles << " cycles, " << p.kites.retired << " retired, "
              << p.kites.branchMispredictions << " taken branches)\n";
    std::cout << "by cause:\n";
    for (std::size_t c = 0; c < kLostCycleCauseCount; ++c)
        if (p.byCause[c] > 0)
            std::cout << "  " << std::left << std::setw(22) << causeLabel(LostCycleCause(c))
                      << p.byCause[c] << " cycles, " << p.eventsByCause[c] << " events\n";
    std::cout << "lines (lost cycles desc):\n";
    for (const LineProfile &line : p.lines)
    {
        std::cout << "  L" << std::left << std::setw(4) << line.sourceLine << std::setw(26)
                  << line.text << std::right << std::setw(8) << line.lostCycles << "  "
                  << causeLabel(line.dominantCause()) << "  runs " << line.executionCount;
        if (!line.producerLines.empty())
            std::cout << "  producer L" << line.producerLines.begin()->first;
        if (!line.spuriousDetail.empty())
            std::cout << "  (" << line.spuriousDetail << ")";
        std::cout << "\n";
    }
    for (const UnprotectedHazard &h : p.unprotected)
        std::cout << "  unprotected: L" << h.consumerLine << " reads " << regName(h.reg)
                  << " from L" << h.producerLine << " at distance " << h.distance << " ("
                  << h.occurrenceCount << "x)\n";
    return p.identityHolds() ? 0 : 3;
}
