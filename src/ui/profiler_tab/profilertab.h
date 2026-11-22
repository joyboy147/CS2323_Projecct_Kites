#ifndef PROFILERTAB_H
#define PROFILERTAB_H

#include "common/assembled_program.h"
#include "processor/processor_types.h"
#include "ui/common/kitestab.h"
#include <QString>
#include <QWidget>
#include <functional>
#include <map>
#include <string>



namespace Kites
{
  
class Profiler; // Forward declaration to avoid including profiler header
class StallProfilerWidget;

namespace Ui
{
class ProfilerTab;
}

class ProfilerTab : public KitesTab
{
    Q_OBJECT

public:
    explicit ProfilerTab(QWidget *parent = nullptr, const Profiler *profiler = nullptr);
    ~ProfilerTab();

    /** @brief Editor text provider for the stall profiler's Profile button. */
    void setSourceProvider(std::function<std::string()> provider);
    void setProcessorTypeProvider(std::function<ProcessorType()> provider);
    /** @brief The user's Run finished: profile it (if enabled) on a private processor. */
    void onRunFinished(const AssembledProgram &program, const std::string &source,
                       ProcessorType type, uint64_t userCycles);

public slots:
    void setSourceText(const QString &sourceText);
    void updateLineExecutionCount(const std::map<int, int> &lineExecutionCounts);
    void updateLineInstructionType(const std::map<int, std::string> &lineInstructionTypes);
    void updateStatistics(const std::map<std::string, int> &statistics);
    void resetProfilerView();

signals:
    void jumpToEditorLineRequested(int line);
    void suggestFixRequested(int line);

protected:
    void showEvent(QShowEvent *event) override;

private:
    /** True if the profiler editor shows the text the stall profile's line numbers refer to. */
    bool stallProfileMatchesText() const;

    Ui::ProfilerTab *ui;
    const Profiler *m_profiler = nullptr;
    StallProfilerWidget *m_stallView = nullptr;
};
} // namespace Kites
#endif // PROFILERTAB_H
