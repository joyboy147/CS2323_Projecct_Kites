// kites_suggest: headless driver of the Phase-2 Suggestions flow (what the GUI tab does).
//
//   kites_suggest <file.s> [hf|hnf] [--write out.s] [--verbose] [--table]
//
// Assembles the file, analyzes it, re-assembles and verifies the rewritten source, runs both
// versions on a private Kites processor via KitesTraceRunner and compares the architectural
// state. --table prints one markdown row (HF and HNF) for the README.
#include "assembler/assembler.h"
#include "scheduler/pipeline_trace.h"
#include "scheduler/runtime/kites_trace_runner.h"
#include "scheduler/source_rewriter.h"
#include "scheduler/suggestion_engine.h"
#include "utils/utils.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace Kites;

namespace
{
std::string readFile(const std::string &path)
{
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

AssembledProgram assembleText(const std::string &text)
{
    std::istringstream in(text);
    return assemble(in);
}

struct ModeResult
{
    ProgramSuggestion suggestion;
    TraceRunResult original;
    TraceRunResult suggested;
    bool verified{};
    std::string verifyReason;
    bool equivalent{};
};

ModeResult runMode(const std::string &source, bool forwarding)
{
    ModeResult r;
    const SchedulingOptions options{.forwardingEnabled = forwarding};
    const AssembledProgram program = assembleText(source);
    r.suggestion = SuggestionEngine{}.analyze(source, program, options);
    AssembledProgram suggestedProgram = program;
    if (r.suggestion.canApply)
    {
        suggestedProgram = assembleText(r.suggestion.rewrittenSource);
        const auto v = verifyRewrittenProgram(program, suggestedProgram,
                                              r.suggestion.rewrittenSource, r.suggestion);
        r.verified = v.ok;
        r.verifyReason = v.reason;
    }
    KitesTraceRunner runner;
    const TraceRunOptions runOptions{forwarding ? ProcessorType::RV5Stage_H_F
                                                : ProcessorType::RV5Stage_H_NF};
    r.original = runner.run(program, runOptions);
    r.suggested = runner.run(suggestedProgram, runOptions);
    r.equivalent = r.original.gpr == r.suggested.gpr && r.original.fpr == r.suggested.fpr &&
                   r.original.dataMemory == r.suggested.dataMemory;
    return r;
}

const char *statusName(ProgramSuggestion::Status status)
{
    switch (status)
    {
    case ProgramSuggestion::Status::Improved: return "Improved";
    case ProgramSuggestion::Status::NoImprovement: return "NoImprovement";
    case ProgramSuggestion::Status::NotApplicable: return "NotApplicable";
    default: return "Error";
    }
}

void printVerbose(const ModeResult &r, const char *mode)
{
    const auto &s = r.suggestion;
    std::cout << "=== " << mode << ": " << statusName(s.status) << " - " << s.statusMessage << "\n";
    std::cout << "model stalls " << s.originalStaticStalls << " -> " << s.suggestedStaticStalls
              << ", straight-line cycles " << s.originalStraightLineCycles << " -> "
              << s.suggestedStraightLineCycles << "\n";
    std::cout << "Kites cycles " << r.original.trace.kitesCycleCount << " -> "
              << r.suggested.trace.kitesCycleCount << ", equivalent: " << (r.equivalent ? "yes" : "NO")
              << ", verified: " << (s.canApply ? (r.verified ? "yes" : "NO: " + r.verifyReason) : "n/a")
              << "\n";
    std::cout << "-- suggested listing\n";
    for (const auto &row : s.suggestedListing)
    {
        std::cout << "  L" << row.sourceLine << "\t" << row.text;
        if (row.move != MoveKind::None)
            std::cout << "   [" << (row.move == MoveKind::Up ? "up " : "down ") << row.moveDistance << "]";
        if (row.stallCycles) std::cout << "   (+" << row.stallCycles << ": " << row.stallReason << ")";
        std::cout << "\n";
    }
    std::cout << "-- explanations\n";
    for (const auto &e : s.explanations) std::cout << "  * " << e.headline << "\n    " << e.detail << "\n";
    if (!s.canApply) std::cout << "-- apply blocked: " << s.applyBlockedReason << "\n";
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: kites_suggest <file.s> [hf|hnf] [--write out.s] [--verbose] [--table]\n";
        return 2;
    }
    std::string file = argv[1], mode = "hf", writePath;
    bool verbose = false, table = false;
    for (int i = 2; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--write") && i + 1 < argc) writePath = argv[++i];
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else if (!std::strcmp(argv[i], "--table")) table = true;
        else mode = argv[i];
    }
    setupVmStateDirectory();
    const std::string source = readFile(file);
    try
    {
        if (table)
        {
            const ModeResult hf = runMode(source, true);
            const ModeResult hnf = runMode(source, false);
            auto cell = [](const ModeResult &r)
            {
                return std::to_string(r.original.trace.kitesCycleCount) + " | " +
                       std::to_string(r.suggested.trace.kitesCycleCount);
            };
            auto ok = [](const ModeResult &r)
            { return r.equivalent && (!r.suggestion.canApply || r.verified); };
            std::cout << "| `" << file.substr(file.find_last_of('/') + 1) << "` | " << cell(hf)
                      << " | " << cell(hnf) << " | " << hf.suggestion.originalStaticStalls << " -> "
                      << hf.suggestion.suggestedStaticStalls << " | "
                      << hnf.suggestion.originalStaticStalls << " -> "
                      << hnf.suggestion.suggestedStaticStalls << " | "
                      << (ok(hf) && ok(hnf) ? "yes" : "NO") << " |\n";
            return 0;
        }
        const ModeResult r = runMode(source, mode != "hnf");
        printVerbose(r, mode.c_str());
        if (!writePath.empty() && r.suggestion.canApply)
            std::ofstream(writePath) << r.suggestion.rewrittenSource;
        (void)verbose;
    }
    catch (const std::exception &e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
