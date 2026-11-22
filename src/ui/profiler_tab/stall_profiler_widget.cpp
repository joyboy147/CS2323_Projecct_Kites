#include "stall_profiler_widget.h"

#include "assembler/assembler.h"
#include "cause_bar.h"
#include "custom_pseudo_manager/custom_pseudo_manager.h"
#include "scheduler/suggestion_engine.h"
#include "ui/common/theme_colors.h"
#include "ui/theme/font_manager.h"
#include "ui/theme/theme_manager.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <sstream>

namespace Kites
{
namespace
{
constexpr int kLineColumn = 0;
constexpr int kFixColumn = 7;
const char *kAutoProfileKey = "stallProfiler/auto";

// m_modeCombo rows after "Current processor".
constexpr ProcessorType kModeRows[] = {ProcessorType::RV5Stage_H_F, ProcessorType::RV5Stage_H_NF,
                                       ProcessorType::RV5Stage_NH_F, ProcessorType::RV5Stage_NH_NF};

QString modeName(ProcessorType type)
{
    switch (type)
    {
    case ProcessorType::RV5Stage_H_F:
        return "HF";
    case ProcessorType::RV5Stage_H_NF:
        return "HNF";
    case ProcessorType::RV5Stage_NH_F:
        return "NH_F";
    case ProcessorType::RV5Stage_NH_NF:
        return "NH_NF";
    default:
        return "single-cycle";
    }
}

QTableWidgetItem *numberItem(uint64_t value)
{
    auto *item = new QTableWidgetItem();
    item->setData(Qt::DisplayRole, QVariant(static_cast<qulonglong>(value)));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QTableWidgetItem *percentItem(uint64_t part, uint64_t whole)
{
    auto *item = new QTableWidgetItem();
    const double percent = whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / whole;
    item->setData(Qt::DisplayRole, QVariant(static_cast<double>(qRound(percent * 10) / 10.0)));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QColor causeColor(LostCycleCause cause, const TabColors &colors)
{
    QColor stall = colors.stall;
    stall.setAlpha(255);
    switch (cause)
    {
    case LostCycleCause::LoadUse:
        return stall;
    case LostCycleCause::RawNoForwardDist1:
    case LostCycleCause::RawNoForwardDist2:
        return stall.darker(130);
    case LostCycleCause::SpuriousFieldAlias:
    case LostCycleCause::SpuriousFpGprAlias:
        return colors.muted;
    case LostCycleCause::BranchFlush:
    case LostCycleCause::JalFlush:
    case LostCycleCause::JalrFlush:
        return colors.currentLine;
    case LostCycleCause::PipelineFill:
    case LostCycleCause::Truncated:
        return colors.surface.lighter(150);
    default:
        return QColor(Qt::black);
    }
}

std::string expandAndRead(const std::string &raw)
{
    return customPseudoManager::expandPseudoInstruction(raw);
}
} // namespace

StallProfilerWidget::StallProfilerWidget(QWidget *parent) : QWidget(parent)
{
    buildUi();
    connect(&m_watcher, &QFutureWatcher<ProfileRunResult>::finished, this,
            &StallProfilerWidget::onProfileFinished);
    connect(&ThemeManager::getInstance(), &ThemeManager::themeChangedSignal, this,
            &StallProfilerWidget::onThemeChanged);
}

StallProfilerWidget::~StallProfilerWidget()
{
    // The worker uses m_runner and m_cancel; never destroy them under a running job.
    m_cancel = true;
    m_watcher.waitForFinished();
}

void StallProfilerWidget::setSourceProvider(std::function<std::string()> provider)
{
    m_sourceProvider = std::move(provider);
}

void StallProfilerWidget::setProcessorTypeProvider(std::function<ProcessorType()> provider)
{
    m_processorTypeProvider = std::move(provider);
}

void StallProfilerWidget::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);

    // Buttons, not a QToolBar: MainWindow::runFinishedSlot() uses findChildren<QToolBar *>()[0].
    auto *controls = new QHBoxLayout();
    controls->addWidget(new QLabel("Stall profile for:", this));
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItems({"Current processor", "HF", "HNF", "NH_F", "NH_NF"});
    controls->addWidget(m_modeCombo);
    m_profileButton = new QPushButton("Profile", this);
    m_profileButton->setToolTip("Re-run the editor program on a private Kites processor and "
                                "attribute every lost cycle");
    m_cancelButton = new QPushButton("Cancel", this);
    m_cancelButton->setVisible(false);
    m_autoCheck = new QCheckBox("Profile after each Run", this);
    QSettings settings("Kites", "Kites");
    m_autoCheck->setChecked(settings.value(kAutoProfileKey, true).toBool());
    m_status = new QLabel(this);
    controls->addWidget(m_profileButton);
    controls->addWidget(m_cancelButton);
    controls->addWidget(m_autoCheck);
    controls->addWidget(m_status, 1);
    layout->addLayout(controls);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setText("Run the program or press <b>Profile</b> to see which lines lose cycles "
                       "to pipeline stalls and flushes.");
    layout->addWidget(m_summary);
    m_bar = new CauseBar(this);
    layout->addWidget(m_bar);

    auto makeTable = [](const QStringList &headers)
    {
        auto *table = new QTableWidget(0, static_cast<int>(headers.size()));
        table->setHorizontalHeaderLabels(headers);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->setFont(FontManager::getInstance().currentFont());
        return table;
    };
    m_lineTable =
        makeTable({"Line", "Instruction", "Lost", "%", "Hazard", "Producer", "Runs", "Fix"});
    m_lineTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_causeTable = makeTable({"Cause", "Cycles", "%", "Events"});
    m_unprotectedTable = makeTable({"Consumer", "Producer", "Register", "Distance", "Count"});
    m_unprotectedTable->setVisible(false);

    auto *right = new QWidget();
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(m_causeTable);
    rightLayout->addWidget(m_unprotectedTable);
    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_lineTable);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);

    connect(m_profileButton, &QPushButton::clicked, this,
            &StallProfilerWidget::profileEditorProgram);
    connect(m_cancelButton, &QPushButton::clicked, this, &StallProfilerWidget::cancelProfile);
    connect(m_autoCheck, &QCheckBox::toggled, this,
            [](bool checked)
            {
                QSettings store("Kites", "Kites");
                store.setValue(kAutoProfileKey, checked);
            });
    auto lineAt = [this](int row)
    {
        const QTableWidgetItem *item = m_lineTable->item(row, kLineColumn);
        return item ? item->data(Qt::UserRole).toInt() : 0;
    };
    connect(m_lineTable, &QTableWidget::cellClicked, this,
            [this, lineAt](int row, int)
            {
                if (const int line = lineAt(row); line > 0)
                    emit lineSelected(line);
            });
    connect(m_lineTable, &QTableWidget::cellDoubleClicked, this,
            [this, lineAt](int row, int)
            {
                if (const int line = lineAt(row); line > 0)
                    emit jumpToEditorLineRequested(line);
            });
}

std::optional<ProcessorType> StallProfilerWidget::resolveMode(ProcessorType current) const
{
    const int row = m_modeCombo->currentIndex();
    if (row > 0)
        return kModeRows[row - 1];
    if (current == ProcessorType::RVSS)
        return std::nullopt;
    return current;
}

bool StallProfilerWidget::isProfiling() const
{
    return m_watcher.isRunning();
}

void StallProfilerWidget::profileEditorProgram()
{
    if (!m_sourceProvider)
        return;
    const std::string source = expandAndRead(m_sourceProvider());
    auto program = std::make_shared<AssembledProgram>();
    try
    {
        std::istringstream input(source);
        *program = assemble(input);
    }
    catch (const std::exception &)
    {
        m_status->setText("The program does not assemble; fix it in the Editor tab.");
        return;
    }
    const ProcessorType current =
        m_processorTypeProvider ? m_processorTypeProvider() : ProcessorType::RV5Stage_H_F;
    const std::optional<ProcessorType> mode = resolveMode(current);
    if (!mode)
    {
        m_summary->setText("Single-cycle processor: every instruction takes one cycle, so there "
                           "are no stalls to profile. Pick a 5-stage model above to see what a "
                           "pipeline would lose.");
        return;
    }
    start({program, source, *mode, std::nullopt});
}

void StallProfilerWidget::onRunFinished(const AssembledProgram &program, const std::string &source,
                                        ProcessorType type, uint64_t userCycles)
{
    if (!m_autoCheck->isChecked() || program.text_buffer.empty())
        return;
    const std::optional<ProcessorType> mode = resolveMode(type);
    if (!mode)
        return;
    // Compare with the user's run only when it ran on the processor being profiled.
    const std::optional<uint64_t> cycles =
        *mode == type ? std::optional<uint64_t>(userCycles) : std::nullopt;
    start({std::make_shared<AssembledProgram>(program), source, *mode, cycles});
}

void StallProfilerWidget::start(Request request)
{
    if (m_watcher.isRunning())
    {
        m_pending = std::move(request); // started from onProfileFinished()
        m_cancel = true;
        m_status->setText("Restarting…");
        return;
    }
    if (!m_runner)
        m_runner = std::make_unique<KitesTraceRunner>();
    // GUI thread: constructing a processor builds its circuit scene in GUI builds.
    const std::string error = m_runner->prepare(*request.program, request.processor);
    if (!error.empty())
    {
        m_status->setText(QString::fromStdString(error));
        return;
    }
    m_active = std::move(request);
    m_cancel = false;
    TraceRunOptions options;
    options.processor = m_active.processor;
    options.maxCycles = kMaxCycles;
    options.cancel = &m_cancel;
    KitesTraceRunner *runner = m_runner.get();
    m_watcher.setFuture(
        QtConcurrent::run([runner, program = m_active.program, source = m_active.source, options]()
                          { return profilePrepared(*runner, *program, source, options); }));
    setRunning(true);
}

void StallProfilerWidget::cancelProfile()
{
    m_pending.reset();
    m_cancel = true;
}

void StallProfilerWidget::setRunning(bool running)
{
    m_profileButton->setEnabled(!running);
    m_cancelButton->setVisible(running);
    if (running)
        m_status->setText(QString("Profiling on %1 (up to %2 cycles)…")
                              .arg(modeName(m_active.processor))
                              .arg(kMaxCycles));
}

void StallProfilerWidget::onProfileFinished()
{
    setRunning(false);
    ProfileRunResult result;
    try
    {
        result = m_watcher.result();
    }
    catch (const std::exception &e)
    {
        result.error = e.what();
    }
    if (m_pending)
    {
        Request next = std::move(*m_pending);
        m_pending.reset();
        start(std::move(next));
        return;
    }
    if (!result.ok)
    {
        m_status->setText("Profiling failed: " + QString::fromStdString(result.error));
        return;
    }
    m_profile = std::move(result.profile);
    m_profiled = m_active;
    m_hasProfile = true;
    m_stale = false;
    m_status->setText(m_profile.cancelled ? "Cancelled: partial profile" : QString());
    render();
    emit profileUpdated();
}

void StallProfilerWidget::markStaleIfChanged()
{
    if (!m_hasProfile || !m_sourceProvider)
        return;
    m_stale = expandAndRead(m_sourceProvider()) != m_profiled.source;
    if (m_stale && !isProfiling())
        m_status->setText("Stale: the editor changed since this profile.");
}

std::map<int, uint64_t> StallProfilerWidget::lineStallCycles() const
{
    std::map<int, uint64_t> cycles;
    for (const LineProfile &line : m_profile.lines)
        cycles[static_cast<int>(line.sourceLine)] = line.lostCycles;
    return cycles;
}

void StallProfilerWidget::onThemeChanged()
{
    if (m_hasProfile)
        render();
}

void StallProfilerWidget::render()
{
    renderSummary();
    fillLineTable();
    fillCauseTable();
}

void StallProfilerWidget::renderSummary()
{
    const StallProfile &p = m_profile;
    const TabColors colors = currentColors();
    const uint64_t lost = p.cycles - p.retired;
    const double percent = p.cycles == 0 ? 0.0 : 100.0 * static_cast<double>(lost) / p.cycles;
    auto sum = [&p](std::initializer_list<LostCycleCause> causes)
    {
        uint64_t total = 0;
        for (LostCycleCause cause : causes)
            total += p.byCause[static_cast<std::size_t>(cause)];
        return total;
    };
    const uint64_t flush =
        sum({LostCycleCause::BranchFlush, LostCycleCause::JalFlush, LostCycleCause::JalrFlush});

    QString html = QString("<b>Stall profile on %1: %2 of %3 cycles lost (%4%)</b>: ")
                       .arg(modeName(p.processor))
                       .arg(static_cast<qulonglong>(lost))
                       .arg(static_cast<qulonglong>(p.cycles))
                       .arg(percent, 0, 'f', 1);
    if (p.hazardDetection)
        html +=
            QString("load-use %1 &middot; RAW no-fwd %2 &middot; spurious %3 &middot; ")
                .arg(static_cast<qulonglong>(sum({LostCycleCause::LoadUse})))
                .arg(static_cast<qulonglong>(
                    sum({LostCycleCause::RawNoForwardDist1, LostCycleCause::RawNoForwardDist2})))
                .arg(static_cast<qulonglong>(
                    sum({LostCycleCause::SpuriousFieldAlias, LostCycleCause::SpuriousFpGprAlias})));
    else
        html += "no hazard detection, so 0 stall cycles &middot; ";
    html += QString("flush %1 &middot; fill %2")
                .arg(static_cast<qulonglong>(flush))
                .arg(static_cast<qulonglong>(p.fillCycles));
    if (p.truncatedCycles > 0)
        html += QString(" &middot; truncated %1").arg(static_cast<qulonglong>(p.truncatedCycles));

    if (p.identityHolds())
        html += QString("<br>&#10003; matches Kites (%1 cycles, %2 retired)")
                    .arg(static_cast<qulonglong>(p.kites.cycles))
                    .arg(static_cast<qulonglong>(p.kites.retired));
    else
        html += "<br>&#9888; accounting mismatch: " +
                QString::fromStdString(p.identityReport()).toHtmlEscaped();
    if (m_profiled.userCycles)
        html += *m_profiled.userCycles == p.cycles
                    ? QString(" &middot; &#10003; same as your run")
                    : QString(" &middot; your run stopped at %1 cycles; the profile covers the "
                              "full run")
                          .arg(static_cast<qulonglong>(*m_profiled.userCycles));
    if (p.hitCycleLimit)
        html += QString("<br>Cycle limit (%1) reached: the profile covers the first %1 cycles.")
                    .arg(kMaxCycles);
    if (!p.hazardDetection && !p.unprotected.empty())
        html += QString("<br>%1 unprotected data hazards (values may be stale), listed on the "
                        "right.")
                    .arg(static_cast<qulonglong>(p.unprotected.size()));
    m_summary->setText(html);

    std::vector<CauseBar::Segment> segments;
    for (std::size_t c = 0; c < kLostCycleCauseCount; ++c)
    {
        if (p.byCause[c] == 0)
            continue;
        const auto cause = static_cast<LostCycleCause>(c);
        segments.push_back({static_cast<double>(p.byCause[c]), causeColor(cause, colors),
                            QString("%1: %2 cycles")
                                .arg(QString::fromLatin1(causeLabel(cause)))
                                .arg(static_cast<qulonglong>(p.byCause[c]))});
    }
    m_bar->setSegments(segments);
}

void StallProfilerWidget::fillLineTable()
{
    const StallProfile &p = m_profile;
    const TabColors colors = currentColors();
    const std::vector<std::string> sourceLines = splitLines(m_profiled.source);
    const uint64_t lost = p.cycles - p.retired;

    m_lineTable->setSortingEnabled(false);
    m_lineTable->clearContents();
    m_lineTable->setRowCount(static_cast<int>(p.lines.size()));
    for (int r = 0; r < static_cast<int>(p.lines.size()); ++r)
    {
        const LineProfile &line = p.lines[static_cast<std::size_t>(r)];
        const LostCycleCause dominant = line.dominantCause();

        QTableWidgetItem *lineItem = numberItem(line.sourceLine);
        lineItem->setData(Qt::UserRole, static_cast<int>(line.sourceLine));
        m_lineTable->setItem(r, kLineColumn, lineItem);
        m_lineTable->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(line.text)));
        m_lineTable->setItem(r, 2, numberItem(line.lostCycles));
        m_lineTable->setItem(r, 3, percentItem(line.lostCycles, lost));

        auto *hazard = new QTableWidgetItem(QString::fromLatin1(causeLabel(dominant)));
        QColor tint = causeColor(dominant, colors);
        tint.setAlpha(90);
        hazard->setBackground(QBrush(tint));
        QString breakdown;
        for (std::size_t c = 0; c < kLostCycleCauseCount; ++c)
            if (line.byCause[c] > 0)
                breakdown += QString("%1: %2 cycles\n")
                                 .arg(QString::fromLatin1(causeLabel(LostCycleCause(c))))
                                 .arg(static_cast<qulonglong>(line.byCause[c]));
        breakdown += QString::fromLatin1(causeExplanation(dominant));
        if (!line.spuriousDetail.empty())
            breakdown += "\n(" + QString::fromStdString(line.spuriousDetail) + ")";
        hazard->setToolTip(breakdown);
        m_lineTable->setItem(r, 4, hazard);

        QString producer;
        if (!line.producerLines.empty())
        {
            auto top = line.producerLines.begin();
            for (auto it = line.producerLines.begin(); it != line.producerLines.end(); ++it)
                if (it->second > top->second)
                    top = it;
            producer = QString("L%1 (%2)")
                           .arg(top->first)
                           .arg(QString::fromStdString(displayLine(sourceLines, top->first)));
        }
        auto *producerItem = new QTableWidgetItem(producer);
        if (line.producerLines.size() > 1)
            producerItem->setToolTip(
                QString("+%1 more producer lines").arg(line.producerLines.size() - 1));
        m_lineTable->setItem(r, 5, producerItem);
        m_lineTable->setItem(r, 6, numberItem(line.executionCount));

        uint64_t dataCycles = 0;
        for (std::size_t c = 0; c < kLostCycleCauseCount; ++c)
            if (isDataStallCause(LostCycleCause(c)))
                dataCycles += line.byCause[c];
        m_lineTable->setItem(r, kFixColumn, new QTableWidgetItem());
        if (dataCycles > 0 && p.hazardDetection)
        {
            auto *fix = new QPushButton("Suggest fix");
            fix->setFlat(true);
            fix->setToolTip("Open the Suggestions tab for this line's block");
            const int sourceLine = static_cast<int>(line.sourceLine);
            connect(fix, &QPushButton::clicked, this,
                    [this, sourceLine]() { emit suggestFixRequested(sourceLine); });
            m_lineTable->setCellWidget(r, kFixColumn, fix);
        }
    }
    m_lineTable->setSortingEnabled(true);
    m_lineTable->sortItems(2, Qt::DescendingOrder);
}

void StallProfilerWidget::fillCauseTable()
{
    const StallProfile &p = m_profile;
    const uint64_t lost = p.cycles - p.retired;
    m_causeTable->setSortingEnabled(false);
    m_causeTable->clearContents();
    m_causeTable->setRowCount(0);
    for (std::size_t c = 0; c < kLostCycleCauseCount; ++c)
    {
        if (p.byCause[c] == 0)
            continue;
        const auto cause = static_cast<LostCycleCause>(c);
        const int row = m_causeTable->rowCount();
        m_causeTable->insertRow(row);
        auto *label = new QTableWidgetItem(QString::fromLatin1(causeLabel(cause)));
        label->setToolTip(QString::fromLatin1(causeExplanation(cause)));
        m_causeTable->setItem(row, 0, label);
        m_causeTable->setItem(row, 1, numberItem(p.byCause[c]));
        m_causeTable->setItem(row, 2, percentItem(p.byCause[c], lost));
        m_causeTable->setItem(row, 3, numberItem(p.eventsByCause[c]));
    }
    m_causeTable->setSortingEnabled(true);
    m_causeTable->sortItems(1, Qt::DescendingOrder);

    m_unprotectedTable->setVisible(!p.hazardDetection && !p.unprotected.empty());
    m_unprotectedTable->clearContents();
    m_unprotectedTable->setRowCount(static_cast<int>(p.unprotected.size()));
    for (int r = 0; r < static_cast<int>(p.unprotected.size()); ++r)
    {
        const UnprotectedHazard &h = p.unprotected[static_cast<std::size_t>(r)];
        m_unprotectedTable->setItem(r, 0, numberItem(h.consumerLine));
        m_unprotectedTable->setItem(r, 1, numberItem(h.producerLine));
        m_unprotectedTable->setItem(r, 2,
                                    new QTableWidgetItem(QString::fromStdString(regName(h.reg))));
        m_unprotectedTable->setItem(r, 3, numberItem(static_cast<uint64_t>(h.distance)));
        m_unprotectedTable->setItem(r, 4, numberItem(h.occurrenceCount));
    }
}
} // namespace Kites
