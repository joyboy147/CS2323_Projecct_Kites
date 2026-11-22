#include "suggestionstab.h"

#include "assembler/assembler.h"
#include "custom_pseudo_manager/custom_pseudo_manager.h"
#include "scheduler/source_rewriter.h"
#include "ui/common/theme_colors.h"
#include "ui/theme/font_manager.h"
#include "ui/theme/icon_manager.h"
#include "ui/theme/theme_manager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <set>
#include <sstream>

namespace Kites
{
namespace
{
// m_modelCombo rows: match current processor, HF, HNF.
constexpr int kMatchCurrentModel = 0;
constexpr int kHnfModel = 2;
// m_latencyCombo rows: Kites latencies (exact), custom latencies.
constexpr int kCustomLatencies = 1;

constexpr int kLineColumn = 0;
constexpr int kTextColumn = 1;

class WaitCursor
{
  public:
    WaitCursor()
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    }
    ~WaitCursor()
    {
        QApplication::restoreOverrideCursor();
    }
    WaitCursor(const WaitCursor &) = delete;
    WaitCursor &operator=(const WaitCursor &) = delete;
};

AssembledProgram assembleText(const std::string &text)
{
    std::istringstream input(text);
    return assemble(input);
}

/** "L8  add x6, x3, x1" for a PC, from the program's line mapping and its source text. */
std::function<std::string(uint64_t)> labelMaker(const AssembledProgram &program,
                                                const std::string &source)
{
    return [lines = splitLines(source),
            mapping = program.instruction_number_line_number_mapping](uint64_t pc)
    {
        const auto it = mapping.find(static_cast<unsigned>(pc / 4));
        if (it == mapping.end())
            return std::string("pc ") + std::to_string(pc);
        return "L" + std::to_string(it->second) + "  " + displayLine(lines, it->second);
    };
}

QString signedDelta(long delta)
{
    if (delta == 0)
        return "&plusmn;0";
    return (delta < 0 ? "&minus;" : "+") + QString::number(delta < 0 ? -delta : delta);
}

QString escaped(const std::string &text)
{
    return QString::fromStdString(text).toHtmlEscaped();
}

} // namespace

SuggestionsTab::SuggestionsTab(QWidget *parent) : KitesTab(parent)
{
    buildUi();
    onThemeChanged();
}

void SuggestionsTab::setSourceProvider(std::function<std::string()> provider)
{
    m_sourceProvider = std::move(provider);
}

void SuggestionsTab::setProcessorTypeProvider(std::function<ProcessorType()> provider)
{
    m_processorTypeProvider = std::move(provider);
}

void SuggestionsTab::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    buildControls(layout);
    buildListingsAndExplanations(layout);
}

void SuggestionsTab::buildControls(QVBoxLayout *layout)
{
    // A plain button row rather than a QToolBar: MainWindow looks up its own toolbar with
    // findChild<QToolBar *>(), which would also find a toolbar inside this tab.
    auto *controls = new QHBoxLayout();
    controls->addWidget(new QLabel("Model:", this));
    m_modelCombo = new QComboBox(this);
    m_modelCombo->addItems({"Match current processor",
                            "5-stage · hazard detection + forwarding (HF)",
                            "5-stage · hazard detection, no forwarding (HNF)"});
    controls->addWidget(m_modelCombo);
    m_latencyCombo = new QComboBox(this);
    m_latencyCombo->addItems({"Kites latencies (exact)", "Custom latencies"});
    controls->addWidget(m_latencyCombo);
    controls->addWidget(new QLabel("Loop unrolling:", this));
    m_unrollCombo = new QComboBox(this);
    m_unrollCombo->addItems({"Off", "2", "4"});
    m_unrollCombo->setToolTip(
        "Unroll eligible simple counted loops before scheduling. Larger factors reduce taken "
        "loop branches but increase code size.");
    controls->addWidget(m_unrollCombo);
    m_analyzeButton =
        new QPushButton(IconManager::getInstance().getIcon(Icon::Play), "Analyze", this);
    m_applyButton = new QPushButton("Apply to editor", this);
    m_revertButton = new QPushButton("Revert", this);
    m_applyButton->setEnabled(false);
    m_revertButton->setEnabled(false);
    m_applyButton->setToolTip("Replace the editor text with the suggested order");
    m_revertButton->setToolTip("Restore the editor text from before the last Apply");
    controls->addWidget(m_analyzeButton);
    controls->addWidget(m_applyButton);
    controls->addWidget(m_revertButton);
    controls->addStretch();
    layout->addLayout(controls);

    m_latencyPanel = new QWidget(this);
    auto *latencyLayout = new QHBoxLayout(m_latencyPanel);
    latencyLayout->setContentsMargins(0, 0, 0, 0);
    auto addSpin = [&](const QString &label, int minimum, int maximum, int value)
    {
        latencyLayout->addWidget(new QLabel(label, m_latencyPanel));
        auto *spin = new QSpinBox(m_latencyPanel);
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        latencyLayout->addWidget(spin);
        return spin;
    };
    const LatencySettings defaults;
    m_loadUseSpin = addSpin("Load-use", 1, 6, static_cast<int>(defaults.loadUse));
    m_aluSpin = addSpin("ALU", 1, 6, static_cast<int>(defaults.alu));
    m_mulSpin = addSpin("MUL", 1, 10, static_cast<int>(defaults.mul));
    m_divSpin = addSpin("DIV", 1, 40, static_cast<int>(defaults.div));
    m_noForwardingSpin = addSpin("No forwarding", 1, 6, static_cast<int>(defaults.noForwarding));
    auto *latencyNote = new QLabel(
        "Model-only: Kites itself uses fixed latencies; cycle counts come from real Kites runs.",
        m_latencyPanel);
    latencyNote->setWordWrap(true);
    latencyLayout->addWidget(latencyNote, 1);
    m_latencyPanel->setVisible(false);
    layout->addWidget(m_latencyPanel);

    m_banner = new QLabel(this);
    m_banner->setWordWrap(true);
    m_banner->setTextFormat(Qt::RichText);
    m_banner->setText("Press <b>Analyze</b> to look for instruction orders of the editor program "
                      "that avoid pipeline stalls.");
    layout->addWidget(m_banner);
}

void SuggestionsTab::buildListingsAndExplanations(QVBoxLayout *layout)
{
    auto *codeSplit = new QSplitter(Qt::Horizontal);
    auto *origBox = new QGroupBox("Original");
    auto *origLayout = new QVBoxLayout(origBox);
    m_origTable = createListingTable({"Line", "Instruction", "Stall"});
    origLayout->addWidget(m_origTable);
    auto *suggBox = new QGroupBox("Suggested");
    auto *suggLayout = new QVBoxLayout(suggBox);
    m_suggTable = createListingTable({"Line", "Instruction", "Move", "Stall"});
    suggLayout->addWidget(m_suggTable);
    codeSplit->addWidget(origBox);
    codeSplit->addWidget(suggBox);

    auto *explainBox = new QGroupBox("Why these moves (untick a block to leave it unchanged)");
    auto *explainLayout = new QVBoxLayout(explainBox);
    auto *selectionRow = new QHBoxLayout();
    m_selectAllButton = new QPushButton("Select all", explainBox);
    m_selectNoneButton = new QPushButton("Select none", explainBox);
    m_selectionStatus = new QLabel(explainBox);
    m_selectionStatus->setWordWrap(true);
    selectionRow->addWidget(m_selectAllButton);
    selectionRow->addWidget(m_selectNoneButton);
    selectionRow->addWidget(m_selectionStatus, 1);
    explainLayout->addLayout(selectionRow);
    m_explanations = new QListWidget(explainBox);
    m_explanations->setWordWrap(true);
    explainLayout->addWidget(m_explanations);
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(300);

    auto *verticalSplit = new QSplitter(Qt::Vertical, this);
    verticalSplit->addWidget(codeSplit);
    verticalSplit->addWidget(explainBox);
    verticalSplit->setStretchFactor(0, 3);
    verticalSplit->setStretchFactor(1, 2);
    layout->addWidget(verticalSplit, 1);

    connect(m_analyzeButton, &QPushButton::clicked, this, &SuggestionsTab::onAnalyze);
    connect(m_selectAllButton, &QPushButton::clicked, this,
            [this]() { setSelection(std::vector<bool>(m_full.schedule.blocks.size(), true)); });
    connect(m_selectNoneButton, &QPushButton::clicked, this,
            [this]() { setSelection(std::vector<bool>(m_full.schedule.blocks.size(), false)); });
    connect(m_explanations, &QListWidget::itemChanged, this, &SuggestionsTab::onExplanationChanged);
    connect(m_refreshTimer, &QTimer::timeout, this,
            [this]()
            {
                refreshSuggestedRun();
                m_selectionStatus->clear();
                render();
            });
    connect(m_applyButton, &QPushButton::clicked, this, &SuggestionsTab::onApply);
    connect(m_revertButton, &QPushButton::clicked, this, &SuggestionsTab::onRevert);
    connect(m_latencyCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_latencyPanel->setVisible(index == kCustomLatencies); });
    connect(m_unrollCombo, &QComboBox::currentIndexChanged, this,
            [this](int)
            {
                if (!m_hasResult)
                    return;
                m_stale = true;
                m_applyButton->setEnabled(false);
                renderBanner();
            });
    connect(m_origTable, &QTableWidget::itemSelectionChanged, this,
            [this]() { syncSelection(m_origTable, m_suggTable); });
    connect(m_suggTable, &QTableWidget::itemSelectionChanged, this,
            [this]() { syncSelection(m_suggTable, m_origTable); });
    connect(m_explanations, &QListWidget::currentRowChanged, this,
            [this](int row)
            {
                const QListWidgetItem *item = m_explanations->item(row);
                if (!item)
                    return;
                const int index = item->data(Qt::UserRole).toInt();
                if (index < 0 || index >= static_cast<int>(m_full.explanations.size()))
                    return;
                const Explanation &e = m_full.explanations[static_cast<std::size_t>(index)];
                std::vector<std::size_t> units{e.movedUnitId};
                units.insert(units.end(), e.relatedUnitIds.begin(), e.relatedUnitIds.end());
                selectUnits(units);
            });
    linkScrollBars(m_origTable, m_suggTable, true);
    connect(&ThemeManager::getInstance(), &ThemeManager::themeChangedSignal, this,
            &SuggestionsTab::onThemeChanged);
    connect(&FontManager::getInstance(), &FontManager::fontChangedSignal, this,
            [this](const QFont &font)
            {
                m_origTable->setFont(font);
                m_suggTable->setFont(font);
            });
}

QTableWidget *SuggestionsTab::createListingTable(const QStringList &headers)
{
    auto *table = new QTableWidget(0, static_cast<int>(headers.size()));
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(kTextColumn, QHeaderView::Stretch);
    table->setFont(FontManager::getInstance().currentFont());
    return table;
}

unsigned SuggestionsTab::selectedUnrollFactor() const
{
    switch (m_unrollCombo->currentIndex())
    {
    case 1:
        return 2;
    case 2:
        return 4;
    default:
        return 1;
    }
}

bool SuggestionsTab::hasApplicableChange() const
{
    if (m_customPseudo)
        return false; // Applying would replace the user's pseudo-instruction source with expansion.
    const bool schedulerChange = m_last.status == ProgramSuggestion::Status::Improved &&
                                 m_last.canApply;
    const bool anyChange = schedulerChange || m_unrollResult.changed();
    // Every source-changing path must be confirmed by the Kites equivalence run.
    return anyChange && m_run.available && m_run.equivalent;
}

std::string SuggestionsTab::sourceToApply() const
{
    // If scheduling is not applicable (or the user unticked every scheduling block), applying
    // the verified unrolled source is still valid.
    return m_last.canApply ? m_last.rewrittenSource : m_expandedSource;
}

void SuggestionsTab::linkScrollBars(QTableWidget *first, QTableWidget *second, bool vertical)
{
    QScrollBar *a = vertical ? first->verticalScrollBar() : first->horizontalScrollBar();
    QScrollBar *b = vertical ? second->verticalScrollBar() : second->horizontalScrollBar();
    auto follow = [this](QScrollBar *target)
    {
        return [this, target](int value)
        {
            if (m_syncing)
                return;
            m_syncing = true;
            target->setValue(value);
            m_syncing = false;
        };
    };
    connect(a, &QScrollBar::valueChanged, this, follow(b));
    connect(b, &QScrollBar::valueChanged, this, follow(a));
}

QString SuggestionsTab::processorName(ProcessorType type) const
{
    switch (type)
    {
    case ProcessorType::RVSS:
        return "single-cycle (RVSS)";
    case ProcessorType::RV5Stage_NH_NF:
        return "5-stage without hazard detection or forwarding";
    case ProcessorType::RV5Stage_NH_F:
        return "5-stage without hazard detection";
    case ProcessorType::RV5Stage_H_NF:
        return "HNF";
    case ProcessorType::RV5Stage_H_F:
        return "HF";
    default:
        return "unknown";
    }
}

SuggestionsTab::AnalysisContext SuggestionsTab::resolveContext() const
{
    AnalysisContext context;
    const int model = m_modelCombo->currentIndex();
    bool forwarding = model != kHnfModel;
    if (model == kMatchCurrentModel && m_processorTypeProvider)
    {
        const ProcessorType current = m_processorTypeProvider();
        if (current == ProcessorType::RV5Stage_H_NF)
        {
            forwarding = false;
        }
        else if (current != ProcessorType::RV5Stage_H_F)
        {
            const QString lacks =
                current == ProcessorType::RVSS ? "has no pipeline" : "has no hazard detection";
            context.modelNote = "Current processor (" + processorName(current) + ") " + lacks +
                                ": showing what the HF pipeline would do. Switch the processor to "
                                "HF to run the suggestion with stalls.";
        }
    }
    context.options.forwardingEnabled = forwarding;
    context.options.hazardDetectionEnabled = true;
    context.runProcessor = forwarding ? ProcessorType::RV5Stage_H_F : ProcessorType::RV5Stage_H_NF;
    if (m_latencyCombo->currentIndex() == kCustomLatencies)
    {
        context.options.stallModel = StallModelKind::CustomLatency;
        context.options.latencies.loadUse = static_cast<unsigned>(m_loadUseSpin->value());
        context.options.latencies.alu = static_cast<unsigned>(m_aluSpin->value());
        context.options.latencies.mul = static_cast<unsigned>(m_mulSpin->value());
        context.options.latencies.div = static_cast<unsigned>(m_divSpin->value());
        context.options.latencies.noForwarding = static_cast<unsigned>(m_noForwardingSpin->value());
    }
    return context;
}

void SuggestionsTab::analyzeAndFocusLine(int line)
{
    onAnalyze();
    if (!m_hasResult)
        return;
    const auto unit =
        std::find_if(m_last.units.begin(), m_last.units.end(), [line](const ScheduleUnit &u)
                     { return static_cast<int>(u.sourceLine) == line; });
    if (unit == m_last.units.end())
        return;
    const auto unitId = static_cast<std::size_t>(unit - m_last.units.begin());
    selectUnits({unitId});
    const std::size_t block = m_last.unitBlock[unitId];
    for (int row = 0; row < m_explanations->count(); ++row)
    {
        const int index = m_explanations->item(row)->data(Qt::UserRole).toInt();
        if (index >= 0 && m_full.explanations[static_cast<std::size_t>(index)].blockIndex == block)
        {
            m_explanations->setCurrentRow(row);
            return;
        }
    }
    const BasicBlockSchedule &schedule = m_last.schedule.blocks[block];
    if (!schedule.accepted)
        m_banner->setText(m_banner->text() +
                          QString("<br>Line %1: no safe reordering in its block (%2).")
                              .arg(line)
                              .arg(QString::fromStdString(schedule.note).toHtmlEscaped()));
}

void SuggestionsTab::onAnalyze()
{
    if (!m_sourceProvider)
        return;
    const WaitCursor waitCursor;
    m_refreshTimer->stop();
    const std::string raw = m_sourceProvider();
    const std::string expanded = customPseudoManager::expandPseudoInstruction(raw);
    m_context = resolveContext();
    AssembledProgram originalProgram;
    if (!assembleForAnalysis(expanded, originalProgram))
        return;

    m_unrollResult = unrollSimpleCountedLoops(expanded, selectedUnrollFactor());
    AssembledProgram program;
    if (!assembleForAnalysis(m_unrollResult.source, program))
        return;

    m_originalProgram = std::move(originalProgram);
    m_originalSource = expanded;
    m_program = std::move(program); // The unrolled source is subsequently DAG-scheduled.
    m_expandedSource = m_unrollResult.source;
    m_customPseudo = expanded != raw;
    m_full = SuggestionEngine{}.analyze(m_expandedSource, m_program, m_context.options);
    if (selectedUnrollFactor() > 1)
    {
        if (m_unrollResult.changed())
        {
            m_full.statusMessage = "Loop unrolling ×" +
                                   std::to_string(m_unrollResult.factor) + " applied to " +
                                   std::to_string(m_unrollResult.loopsUnrolled) +
                                   (m_unrollResult.loopsUnrolled == 1 ? " eligible loop. "
                                                                     : " eligible loops. ") +
                                   m_full.statusMessage;
        }
        else if (!m_unrollResult.notes.empty())
        {
            m_full.statusMessage = "Loop unrolling ×" +
                                   std::to_string(m_unrollResult.factor) + ": " +
                                   m_unrollResult.notes.front() + " " + m_full.statusMessage;
        }
    }
    m_selection.assign(m_full.schedule.blocks.size(), false);
    for (std::size_t b = 0; b < m_selection.size(); ++b)
        m_selection[b] = m_full.schedule.blocks[b].accepted;
    m_last = m_full;
    if (m_customPseudo && m_last.canApply)
    {
        m_last.canApply = false;
        m_last.applyBlockedReason = "the program uses custom pseudo-instructions";
    }

    TraceRunOptions runOptions;
    runOptions.processor = m_context.runProcessor;
    m_originalRun = m_runner.run(m_originalProgram, runOptions);
    m_origDiag = buildDiagram(m_originalRun, m_originalProgram, m_originalSource);
    refreshSuggestedRun();

    m_selectionStatus->clear();
    m_analyzedSource = raw;
    m_hasResult = true;
    m_stale = false;
    render();
}

bool SuggestionsTab::assembleForAnalysis(const std::string &expanded, AssembledProgram &program)
{
    try
    {
        program = assembleText(expanded);
    }
    catch (const std::exception &e)
    {
        showError("The program does not assemble (" + QString::fromStdString(e.what()) +
                  "). Fix the errors shown in the Editor tab first.");
        return false;
    }
    if (program.text_buffer.empty())
    {
        showError("The program has no instructions to analyze.");
        return false;
    }
    return true;
}

void SuggestionsTab::verifySuggestion(AssembledProgram &suggestedProgram,
                                      std::string &suggestedSource)
{
    suggestedProgram = m_program;
    suggestedSource = m_expandedSource;
    // Loop unrolling changes instruction count, so its safety is confirmed by the final Kites
    // equivalence run below. Source rewriting itself still uses the existing strict checker.
    if (!m_last.canApply)
        return;
    try
    {
        AssembledProgram rewritten = assembleText(m_last.rewrittenSource);
        const RewriteVerification verification =
            verifyRewrittenProgram(m_program, rewritten, m_last.rewrittenSource, m_last);
        if (verification.ok)
        {
            suggestedProgram = std::move(rewritten);
            suggestedSource = m_last.rewrittenSource;
        }
        else
        {
            m_last.canApply = false;
            m_last.applyBlockedReason = "verification failed: " + verification.reason;
        }
    }
    catch (const std::exception &)
    {
        m_last.canApply = false;
        m_last.applyBlockedReason = "verification failed: the rewritten program does not "
                                    "assemble";
    }
}

void SuggestionsTab::runOnKites(const AssembledProgram &suggestedProgram,
                                const std::string &suggestedSource)
{
    const bool changed = suggestedSource != m_originalSource;
    TraceRunOptions runOptions;
    runOptions.processor = m_context.runProcessor;
    const TraceRunResult suggested =
        changed ? m_runner.run(suggestedProgram, runOptions) : m_originalRun;

    m_run = RunSummary{};
    if (!m_originalRun.ok || !suggested.ok)
    {
        m_run.error =
            QString::fromStdString(m_originalRun.ok ? suggested.error : m_originalRun.error);
        if (m_last.canApply)
        {
            m_last.canApply = false;
            m_last.applyBlockedReason = "the program could not be run on Kites to confirm it";
        }
    }
    else
    {
        m_run.available = true;
        m_run.suggestedRan = changed;
        m_run.equivalent = m_originalRun.gpr == suggested.gpr &&
                           m_originalRun.fpr == suggested.fpr &&
                           m_originalRun.dataMemory == suggested.dataMemory;
        m_run.originalCycles = m_originalRun.trace.kitesCycleCount;
        m_run.suggestedCycles = suggested.trace.kitesCycleCount;
        m_run.originalTakenBranches = m_originalRun.counters.branchMispredictions;
        m_run.suggestedTakenBranches = suggested.counters.branchMispredictions;
        m_run.hitCycleLimit = m_originalRun.trace.hitCycleLimit || suggested.trace.hitCycleLimit;
        if (!m_run.equivalent && m_last.canApply)
        {
            m_last.canApply = false;
            m_last.applyBlockedReason =
                "Kites produced different registers or memory for the suggested order";
        }
    }
    m_suggDiag = buildDiagram(suggested, suggestedProgram, suggestedSource);
}

PipelineDiagram SuggestionsTab::buildDiagram(const TraceRunResult &run,
                                             const AssembledProgram &program,
                                             const std::string &source) const
{
    if (!run.ok)
        return PipelineDiagram{};
    // Tooltips describe what Kites really does, even when ranking used custom latencies.
    SchedulingOptions noteOptions = m_context.options;
    noteOptions.stallModel = StallModelKind::KitesExact;
    DiagramBuildOptions options;
    options.labelForPc = labelMaker(program, source);
    options.stallNoteForPc =
        [notes = SuggestionEngine{}.stallNotesByPc(source, program, noteOptions)](uint64_t pc)
    {
        const auto it = notes.find(pc);
        return it == notes.end() ? std::string() : it->second;
    };
    return buildPipelineDiagram(run.trace, options);
}

void SuggestionsTab::refreshSuggestedRun()
{
    const WaitCursor waitCursor;
    AssembledProgram suggestedProgram;
    std::string suggestedSource;
    verifySuggestion(suggestedProgram, suggestedSource);
    runOnKites(suggestedProgram, suggestedSource);
}

void SuggestionsTab::setSelection(const std::vector<bool> &requested)
{
    if (!m_hasResult)
        return;
    const SelectionEvaluation evaluation = evaluateSelection(m_program, m_full, requested);
    if (!evaluation.safe)
    {
        m_selectionStatus->setText("Not applied: " +
                                   QString::fromStdString(evaluation.unsafeReason));
        fillExplanations(); // restore the check boxes to the current selection
        return;
    }
    m_selection = evaluation.selected;
    m_last = maskSuggestion(m_expandedSource, m_full, evaluation);
    if (m_customPseudo && m_last.canApply)
    {
        m_last.canApply = false;
        m_last.applyBlockedReason = "the program uses custom pseudo-instructions";
    }
    m_last.status = m_last.suggestedStaticStalls < m_last.originalStaticStalls
                        ? ProgramSuggestion::Status::Improved
                        : ProgramSuggestion::Status::NoImprovement;
    // m_full's message counts the stalls saved by all blocks; restate it for this selection.
    if (m_last.status == ProgramSuggestion::Status::Improved)
    {
        const unsigned saved = m_last.originalStaticStalls - m_last.suggestedStaticStalls;
        m_last.statusMessage = "The selected blocks remove " + std::to_string(saved) +
                               (saved == 1 ? " stall cycle" : " stall cycles") +
                               " (model, straight-line).";
    }
    else
    {
        m_last.statusMessage = "The selected blocks do not lower the stall count.";
    }
    rebuildSuggestedListing();

    unsigned selectedCount = 0;
    for (bool selected : m_selection)
        selectedCount += selected ? 1 : 0;
    m_selectionStatus->setText(QString("%1 block(s) selected: model %2 cycles. Kites re-run "
                                       "pending…")
                                   .arg(selectedCount)
                                   .arg(m_last.suggestedStraightLineCycles));
    m_run.pending = true;
    render();
    m_refreshTimer->start(); // debounced: re-run the suggested program once toggling stops
}

void SuggestionsTab::rebuildSuggestedListing()
{
    // Unselected blocks are listed in their original order, without move markers.
    m_last.suggestedListing.clear();
    for (std::size_t b = 0; b < m_full.schedule.blocks.size(); ++b)
    {
        const bool selected = b < m_selection.size() && m_selection[b];
        const auto &rows = selected ? m_full.suggestedListing : m_full.originalListing;
        for (const ListingRow &row : rows)
            if (row.blockIndex == b)
                m_last.suggestedListing.push_back(row);
    }
}

void SuggestionsTab::onExplanationChanged(QListWidgetItem *item)
{
    if (m_filling || !item)
        return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= static_cast<int>(m_full.explanations.size()))
        return;
    std::vector<bool> requested = m_selection;
    const std::size_t block = m_full.explanations[static_cast<std::size_t>(index)].blockIndex;
    if (block < requested.size())
        requested[block] = item->checkState() == Qt::Checked;
    setSelection(requested);
}

void SuggestionsTab::onApply()
{
    if (!m_hasResult || !hasApplicableChange() || m_run.pending || !m_sourceProvider)
        return;
    const std::string current = m_sourceProvider();
    if (current != m_analyzedSource)
    {
        m_stale = true;
        renderBanner();
        m_applyButton->setEnabled(false);
        return;
    }
    m_preApplyText = current;
    emit applyToEditorRequested(QString::fromStdString(sourceToApply()));
    m_applyButton->setEnabled(false);
    m_revertButton->setEnabled(true);
    m_stale = true;
    renderBanner();
}

void SuggestionsTab::onRevert()
{
    if (m_preApplyText.empty())
        return;
    emit applyToEditorRequested(QString::fromStdString(m_preApplyText));
    m_revertButton->setEnabled(false);
}

void SuggestionsTab::onProcessorChanged()
{
    if (!m_hasResult || m_modelCombo->currentIndex() != kMatchCurrentModel)
        return;
    m_stale = true;
    m_applyButton->setEnabled(false);
    renderBanner();
}

void SuggestionsTab::onThemeChanged()
{
    const TabColors colors = currentColors();
    m_banner->setStyleSheet(
        QString("QLabel { padding: 8px; border-radius: 6px; background-color: %1; }")
            .arg(colors.surface.name()));
    if (m_hasResult)
        render();
}

void SuggestionsTab::showError(const QString &message)
{
    m_hasResult = false;
    m_last = ProgramSuggestion{};
    m_origTable->setRowCount(0);
    m_suggTable->setRowCount(0);
    m_explanations->clear();
    m_applyButton->setEnabled(false);
    m_banner->setText("<b>Cannot analyze:</b> " + message.toHtmlEscaped());
}

void SuggestionsTab::render()
{
    renderBanner();
    fillListing(m_origTable, m_last.originalListing, false);
    fillListing(m_suggTable, m_last.suggestedListing, true);
    fillExplanations();

    m_applyButton->setEnabled(!m_stale && !m_run.pending && hasApplicableChange());
}

void SuggestionsTab::renderBanner()
{
    if (!m_hasResult)
        return;
    const ProgramSuggestion &s = m_last;
    const bool schedulerImproved = s.status == ProgramSuggestion::Status::Improved;
    const bool improved = schedulerImproved || m_unrollResult.changed();
    const QString mode = processorName(m_context.runProcessor);
    QStringList parts;

    if (m_run.available)
    {
        const long kitesDelta =
            static_cast<long>(m_run.suggestedCycles) - static_cast<long>(m_run.originalCycles);
        if (improved && kitesDelta != 0)
        {
            const double percent = m_run.originalCycles == 0
                                       ? 0.0
                                       : 100.0 * static_cast<double>(-kitesDelta) /
                                             static_cast<double>(m_run.originalCycles);
            parts << QString("<b>Kites %1: %2 &rarr; %3 cycles (%4, %5%)</b>")
                         .arg(mode)
                         .arg(m_run.originalCycles)
                         .arg(m_run.suggestedCycles)
                         .arg(signedDelta(kitesDelta))
                         .arg(percent, 0, 'f', 1);
        }
        else
        {
            parts << QString("<b>Kites %1: %2 cycles</b>").arg(mode).arg(m_run.originalCycles);
        }
        if (improved && !m_run.suggestedRan)
        {
            // Apply is blocked, so the suggested order was never assembled or run: there is
            // no measured saving or equivalence to report.
            parts << QString("stalls %1").arg(m_origDiag.totalStallCycles);
            parts << "model predicts " +
                         signedDelta(static_cast<long>(s.suggestedStraightLineCycles) -
                                     static_cast<long>(s.originalStraightLineCycles)) +
                         " cycles; the suggested order was not run on Kites";
        }
        else if (improved)
        {
            parts << QString("stalls %1 &rarr; %2")
                         .arg(m_origDiag.totalStallCycles)
                         .arg(m_suggDiag.totalStallCycles);
            if (m_unrollResult.changed())
            {
                parts << QString("loop unrolled &times;%1 (%2 loop%3)")
                             .arg(m_unrollResult.factor)
                             .arg(m_unrollResult.loopsUnrolled)
                             .arg(m_unrollResult.loopsUnrolled == 1 ? "" : "s");
                // Kites resolves every taken conditional branch in MEM, flushing three younger
                // instructions. Keep this separate from jal/jalr flushes, which unrolling does
                // not target.
                parts << QString("taken-branch flushes %1 &rarr; %2 cycles (%3 &rarr; %4 taken "
                                 "branches)")
                             .arg(m_run.originalTakenBranches * 3)
                             .arg(m_run.suggestedTakenBranches * 3)
                             .arg(m_run.originalTakenBranches)
                             .arg(m_run.suggestedTakenBranches);
            }
            if (schedulerImproved)
            {
                const long modelDelta = static_cast<long>(s.suggestedStraightLineCycles) -
                                        static_cast<long>(s.originalStraightLineCycles);
                if (!m_unrollResult.changed() && modelDelta == kitesDelta)
                    parts << "model predicted " + signedDelta(modelDelta) + " &#10003;";
                else
                    parts << "scheduler model predicted " + signedDelta(modelDelta) +
                                 " after unrolling";
            }
            parts << (m_run.equivalent ? "final registers &amp; data memory identical &#10003;"
                                       : "final registers or data memory differ &#10007;");
        }
        else
        {
            parts << QString("stalls %1").arg(m_origDiag.totalStallCycles);
        }
    }

    QString html = parts.join(" &middot; ");
    html += "<br>" + escaped(s.statusMessage);
    if (schedulerImproved && !s.canApply && !m_unrollResult.changed())
        html += "<br>Apply is disabled: " + escaped(s.applyBlockedReason) + ".";
    if (!m_context.modelNote.isEmpty())
        html += "<br>" + m_context.modelNote.toHtmlEscaped();
    if (m_context.options.stallModel == StallModelKind::CustomLatency)
        html += "<br>Custom latencies are model-only: Kites itself uses fixed latencies, and the "
                "cycle counts above are from real Kites runs.";
    if (m_run.hitCycleLimit)
        html += QString("<br>Cycle limit hit (%1): counts are partial.")
                    .arg(TraceRunOptions{}.maxCycles);
    if (!m_run.error.isEmpty())
        html += "<br>Kites run failed: " + m_run.error.toHtmlEscaped();
    if (m_run.pending)
        html += QString("<br><i>Block selection changed: the model now predicts %1 cycles. The "
                        "Kites numbers above are from the previous selection until the re-run "
                        "finishes.</i>")
                    .arg(s.suggestedStraightLineCycles);
    if (m_stale)
        html += "<br><i>The editor or processor changed since this analysis. Press Analyze "
                "again.</i>";
    m_banner->setText(html);
}

void SuggestionsTab::fillListing(QTableWidget *table, const std::vector<ListingRow> &rows,
                                 bool suggested)
{
    const TabColors colors = currentColors();
    const int stallColumn = suggested ? 3 : 2;
    table->clearSelection();
    table->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r)
    {
        const ListingRow &row = rows[static_cast<std::size_t>(r)];
        const bool moved = suggested && row.move != MoveKind::None;
        const QBrush rowBrush = moved                     ? QBrush(colors.moved)
                                : row.blockIndex % 2 == 1 ? QBrush(colors.blockTint)
                                                          : QBrush(Qt::transparent);

        auto *line = new QTableWidgetItem(QString::number(row.sourceLine));
        line->setData(Qt::UserRole, QVariant(static_cast<qulonglong>(row.unitId)));
        auto *text = new QTableWidgetItem(QString::fromStdString(row.text));
        if (row.pinned)
            text->setToolTip("Stays in place: label, control transfer or auipc (reads the PC)");
        auto *stall = new QTableWidgetItem(
            row.stallCycles > 0 ? "+" + QString::number(row.stallCycles) : QString());
        stall->setTextAlignment(Qt::AlignCenter);
        if (row.stallCycles > 0)
            stall->setToolTip(QString::fromStdString(row.stallReason));

        table->setItem(r, kLineColumn, line);
        table->setItem(r, kTextColumn, text);
        table->setItem(r, stallColumn, stall);
        if (suggested)
        {
            QString arrow;
            if (row.move == MoveKind::Up)
                arrow = "↑" + QString::number(row.moveDistance);
            if (row.move == MoveKind::Down)
                arrow = "↓" + QString::number(row.moveDistance);
            auto *move = new QTableWidgetItem(arrow);
            move->setTextAlignment(Qt::AlignCenter);
            table->setItem(r, 2, move);
        }
        for (int c = 0; c < table->columnCount(); ++c)
            table->item(r, c)->setBackground(rowBrush);
        if (row.stallCycles > 0)
            stall->setBackground(QBrush(colors.stall));
    }
}

void SuggestionsTab::fillExplanations()
{
    // All moves of the full suggestion are listed; the check box of each item is its block's
    // selection (every item of a block mirrors the same flag).
    m_filling = true;
    m_explanations->clear();
    if (m_full.explanations.empty())
    {
        auto *item = new QListWidgetItem(QString::fromStdString(m_last.statusMessage));
        item->setData(Qt::UserRole, -1);
        m_explanations->addItem(item);
    }
    for (int i = 0; i < static_cast<int>(m_full.explanations.size()); ++i)
    {
        const Explanation &e = m_full.explanations[static_cast<std::size_t>(i)];
        auto *item = new QListWidgetItem(QString("Block %1 · %2\n%3")
                                             .arg(static_cast<qulonglong>(e.blockIndex + 1))
                                             .arg(QString::fromStdString(e.headline))
                                             .arg(QString::fromStdString(e.detail)));
        item->setData(Qt::UserRole, i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool selected = e.blockIndex < m_selection.size() && m_selection[e.blockIndex];
        item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
        QFont font = item->font();
        font.setBold(selected && e.cyclesSaved > 0);
        item->setFont(font);
        m_explanations->addItem(item);
    }
    m_filling = false;
    const bool selectable = !m_full.explanations.empty();
    m_selectAllButton->setEnabled(selectable);
    m_selectNoneButton->setEnabled(selectable);
}

void SuggestionsTab::selectUnits(const std::vector<std::size_t> &unitIds)
{
    const std::set<std::size_t> wanted(unitIds.begin(), unitIds.end());
    m_syncing = true;
    for (QTableWidget *table : {m_origTable, m_suggTable})
    {
        QItemSelection selection;
        int firstRow = -1;
        for (int r = 0; r < table->rowCount(); ++r)
        {
            const QTableWidgetItem *item = table->item(r, kLineColumn);
            if (!item || !wanted.contains(item->data(Qt::UserRole).toULongLong()))
                continue;
            selection.select(table->model()->index(r, 0),
                             table->model()->index(r, table->columnCount() - 1));
            if (firstRow < 0)
                firstRow = r;
        }
        table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
        if (firstRow >= 0)
            table->scrollToItem(table->item(firstRow, kLineColumn));
    }
    m_syncing = false;
}

void SuggestionsTab::syncSelection(QTableWidget *from, QTableWidget *to)
{
    if (m_syncing)
        return;
    std::vector<std::size_t> units;
    for (const QModelIndex &index : from->selectionModel()->selectedRows())
    {
        const QTableWidgetItem *item = from->item(index.row(), kLineColumn);
        if (item)
            units.push_back(item->data(Qt::UserRole).toULongLong());
    }
    m_syncing = true;
    QItemSelection selection;
    const std::set<std::size_t> wanted(units.begin(), units.end());
    for (int r = 0; r < to->rowCount(); ++r)
    {
        const QTableWidgetItem *item = to->item(r, kLineColumn);
        if (item && wanted.contains(item->data(Qt::UserRole).toULongLong()))
            selection.select(to->model()->index(r, 0),
                             to->model()->index(r, to->columnCount() - 1));
    }
    to->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    m_syncing = false;
}
} // namespace Kites
