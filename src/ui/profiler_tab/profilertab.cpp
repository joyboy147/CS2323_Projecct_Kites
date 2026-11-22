#include "profilertab.h"
#include "ui_profilertab.h"
#include "profiler/profiler.h"
#include "common/instruction_types.h"
#include "instruction_type_count_model.h"
#include "stall_profiler_widget.h"
#include <QLineEdit>
#include <QShowEvent>
#include <QSplitter>

namespace Kites
{
ProfilerTab::ProfilerTab(QWidget *parent, const Profiler *profiler)
    : KitesTab(parent), ui(new Ui::ProfilerTab), m_profiler(profiler)
{
    ui->setupUi(this);
    ui->splitter->setStretchFactor(0, 1);

    // The stall profiler goes under the existing view, added in code so the .ui file is untouched.
    ui->gridLayout->removeWidget(ui->splitter);
    auto *vertical = new QSplitter(Qt::Vertical, this);
    vertical->addWidget(ui->splitter);
    m_stallView = new StallProfilerWidget(this);
    vertical->addWidget(m_stallView);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 2);
    ui->gridLayout->addWidget(vertical, 0, 0);
    // The profile's line numbers refer to the text it profiled. The Profile button can profile
    // an edited editor text while this editor still shows the last Run's text, so the heat
    // gutter and the row-click scroll are only used when both texts are the same.
    connect(m_stallView, &StallProfilerWidget::profileUpdated, this,
            [this]()
            {
                ui->plainTextEdit->setStallCycles(stallProfileMatchesText()
                                                      ? m_stallView->lineStallCycles()
                                                      : std::map<int, uint64_t>{});
            });
    connect(m_stallView, &StallProfilerWidget::lineSelected, this,
            [this](int line)
            {
                if (stallProfileMatchesText())
                    ui->plainTextEdit->revealLine(line);
            });
    connect(m_stallView, &StallProfilerWidget::jumpToEditorLineRequested, this,
            &ProfilerTab::jumpToEditorLineRequested);
    connect(m_stallView, &StallProfilerWidget::suggestFixRequested, this,
            &ProfilerTab::suggestFixRequested);
    
    ui->instructionTypeCountTableView->horizontalHeader()->
    setSectionResizeMode(QHeaderView::Stretch);
    ui->instructionTypeCountTableView->verticalHeader()->setVisible(false);
    if (m_profiler)
    {   
        InstructionTypeCountModel *model = new InstructionTypeCountModel(this, 
        m_profiler->getInstructionTypeCounts());
        ui->instructionTypeCountTableView->setModel(model);
        // connect(m_profiler, &Profiler::lineExecutionCountsUpdated, this,
        //         &ProfilerTab::updateLineExecutionCounts);
        connect(m_profiler, &Profiler::profilerReset, this,
                &ProfilerTab::resetProfilerView);
        connect(m_profiler, &Profiler::incrementLineExecutionCountSignal, ui->plainTextEdit,
                &ProfilerEditor::incrementLineExecutionCountSlot);
        connect(m_profiler, &Profiler::updateLineInstructionTypeSignal, ui->plainTextEdit,
                &ProfilerEditor::updateLineInstructionTypeSlot);
        connect(m_profiler, &Profiler::updateInstructionTypeCountsSignal, model,
                &InstructionTypeCountModel::updateInstructionTypeCountsSlot);
    }
}

ProfilerTab::~ProfilerTab()
{
    delete ui;
}

void ProfilerTab::setSourceProvider(std::function<std::string()> provider)
{
    m_stallView->setSourceProvider(std::move(provider));
}

void ProfilerTab::setProcessorTypeProvider(std::function<ProcessorType()> provider)
{
    m_stallView->setProcessorTypeProvider(std::move(provider));
}

void ProfilerTab::onRunFinished(const AssembledProgram &program, const std::string &source,
                                ProcessorType type, uint64_t userCycles)
{
    m_stallView->onRunFinished(program, source, type, userCycles);
}

bool ProfilerTab::stallProfileMatchesText() const
{
    return ui->plainTextEdit->toPlainText().toStdString() == m_stallView->profiledSource();
}

void ProfilerTab::showEvent(QShowEvent *event)
{
    KitesTab::showEvent(event);
    m_stallView->markStaleIfChanged();
}

void ProfilerTab::setSourceText(const QString &sourceText)
{
    ui->plainTextEdit->setPlainText(sourceText);
    ui->plainTextEdit->setStallCycles({}); // line numbers refer to the previous text
    ui->plainTextEdit->clearExecutionCount();
}

void ProfilerTab::updateLineExecutionCount(const std::map<int, int> &lineExecutionCounts)
{
    ui->plainTextEdit->setExecutionCount(lineExecutionCounts);
}

void ProfilerTab::updateLineInstructionType(const std::map<int, std::string> &lineInstructionTypes)
{
    ui->plainTextEdit->setInstructionTypes(lineInstructionTypes);
}

void ProfilerTab::updateStatistics(const std::map<std::string, int> &statistics)
{
    
}

void ProfilerTab::resetProfilerView()
{

}
} // namespace Kites
