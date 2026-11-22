/**
 * @file stall_profiler_widget.h
 * @brief Stall section of the Profiler tab: re-runs the program on a private Kites
 *        processor in a worker thread and shows where and why cycles were lost.
 */
#pragma once

#include "common/assembled_program.h"
#include "processor/processor_types.h"
#include "scheduler/runtime/kites_trace_runner.h"

#include <QFutureWatcher>
#include <QString>
#include <QWidget>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace Kites
{
class CauseBar;

/**
 * @brief Table, summary bar and controls of the stall profiler.
 *
 * Threading: the private processor is constructed and loaded on the GUI thread
 * (KitesTraceRunner::prepare), then only its Step() loop and the Qt-free StallProfiler run in
 * a QtConcurrent worker. The result comes back through QFutureWatcher::finished on the GUI
 * thread. One job at a time; a request made while a job runs cancels it and is started when
 * it ends. The destructor cancels and waits.
 */
class StallProfilerWidget : public QWidget
{
    Q_OBJECT

  public:
    static constexpr unsigned kMaxCycles = 2000000;

    explicit StallProfilerWidget(QWidget *parent = nullptr);
    ~StallProfilerWidget() override;

    void setSourceProvider(std::function<std::string()> provider);
    void setProcessorTypeProvider(std::function<ProcessorType()> provider);

    /** @brief Called after the user's Run finished; profiles if "Profile after each Run" is on. */
    void onRunFinished(const AssembledProgram &program, const std::string &source,
                       ProcessorType type, uint64_t userCycles);

    /** @brief Lost cycles per source line of the last profile (for the heat gutter). */
    [[nodiscard]] std::map<int, uint64_t> lineStallCycles() const;
    /** @brief The (pseudo-expanded) source text the last profile's line numbers refer to. */
    [[nodiscard]] const std::string &profiledSource() const
    {
        return m_profiled.source;
    }
    [[nodiscard]] bool isProfiling() const;

  public slots:
    void profileEditorProgram();
    void cancelProfile();
    void markStaleIfChanged();

  signals:
    void profileUpdated();
    void lineSelected(int line);
    void jumpToEditorLineRequested(int line);
    void suggestFixRequested(int line);

  private slots:
    void onProfileFinished();
    void onThemeChanged();

  private:
    struct Request
    {
        std::shared_ptr<const AssembledProgram> program;
        std::string source;
        ProcessorType processor{ProcessorType::RV5Stage_H_F};
        std::optional<uint64_t> userCycles;
    };

    void buildUi();
    void start(Request request);
    std::optional<ProcessorType> resolveMode(ProcessorType current) const;
    void render();
    void renderSummary();
    void fillLineTable();
    void fillCauseTable();
    void setRunning(bool running);

    std::function<std::string()> m_sourceProvider;
    std::function<ProcessorType()> m_processorTypeProvider;

    QComboBox *m_modeCombo = nullptr;
    QPushButton *m_profileButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QCheckBox *m_autoCheck = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_summary = nullptr;
    CauseBar *m_bar = nullptr;
    QTableWidget *m_lineTable = nullptr;
    QTableWidget *m_causeTable = nullptr;
    QTableWidget *m_unprotectedTable = nullptr;

    // Worker-owned while a job runs: m_runner, m_cancel and m_active.program.
    std::unique_ptr<KitesTraceRunner> m_runner;
    std::atomic<bool> m_cancel{false};
    QFutureWatcher<ProfileRunResult> m_watcher;
    Request m_active;
    std::optional<Request> m_pending;

    bool m_hasProfile = false;
    bool m_stale = false;
    Request m_profiled;
    StallProfile m_profile;
};
} // namespace Kites
