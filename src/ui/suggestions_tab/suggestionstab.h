/**
 * @file suggestionstab.h
 * @brief Suggestions tab: stall-saving reorderings, explanations, per-block selection and Apply.
 */
#pragma once

#include "processor/processor_types.h"
#include "scheduler/pipeline_trace.h"
#include "scheduler/runtime/kites_trace_runner.h"
#include "scheduler/selection_evaluator.h"
#include "scheduler/suggestion_engine.h"
#include "ui/common/kitestab.h"

#include <QString>
#include <functional>
#include <string>

class QComboBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class QListWidgetItem;
class QVBoxLayout;

namespace Kites
{
/**
 * Analyzes the editor program for stall-saving reorderings, explains each move,
 * compares the original and suggested instruction listings and can apply the
 * suggestion to the editor. All analysis is done by the Qt-free scheduler.
 */
class SuggestionsTab : public KitesTab
{
    Q_OBJECT

  public:
    explicit SuggestionsTab(QWidget *parent = nullptr);

    /** @brief Where Analyze gets the editor text from. */
    void setSourceProvider(std::function<std::string()> provider);
    /** @brief Where "Match current processor" gets the processor type from. */
    void setProcessorTypeProvider(std::function<ProcessorType()> provider);

  public slots:
    /** @brief Analyze the editor program, then select `line` (1-based) and its block's moves. */
    void analyzeAndFocusLine(int line);
    /** @brief Marks the result stale when the model follows the current processor. */
    void onProcessorChanged();

  signals:
    /** @brief Apply / Revert: replace the editor text with `newSource`. */
    void applyToEditorRequested(const QString &newSource);

  private slots:
    void onAnalyze();
    void onApply();
    void onRevert();
    void onThemeChanged();

  private:
    struct AnalysisContext
    {
        SchedulingOptions options;
        ProcessorType runProcessor{ProcessorType::RV5Stage_H_F};
        QString modelNote;
    };

    struct RunSummary
    {
        bool available{};
        bool equivalent{};
        bool suggestedRan{}; ///< False when Apply is blocked: only the original order was run.
        bool pending{};      ///< The block selection changed; the Kites re-run is debounced.
        unsigned originalCycles{};
        unsigned suggestedCycles{};
        bool hitCycleLimit{};
        QString error;
    };

    void buildUi();
    void buildControls(QVBoxLayout *layout);
    void buildListingsAndExplanations(QVBoxLayout *layout);
    QTableWidget *createListingTable(const QStringList &headers);
    bool assembleForAnalysis(const std::string &expanded, AssembledProgram &program);
    void verifySuggestion(AssembledProgram &suggestedProgram, std::string &suggestedSource);
    void runOnKites(const AssembledProgram &suggestedProgram, const std::string &suggestedSource);
    PipelineDiagram buildDiagram(const TraceRunResult &run, const AssembledProgram &program,
                                 const std::string &source) const;
    void refreshSuggestedRun();
    void setSelection(const std::vector<bool> &selection);
    void onExplanationChanged(QListWidgetItem *item);
    void rebuildSuggestedListing();
    AnalysisContext resolveContext() const;
    void showError(const QString &message);
    void render();
    void renderBanner();
    void fillListing(QTableWidget *table, const std::vector<ListingRow> &rows, bool suggested);
    void fillExplanations();
    void selectUnits(const std::vector<std::size_t> &unitIds);
    void syncSelection(QTableWidget *from, QTableWidget *to);
    void linkScrollBars(QTableWidget *first, QTableWidget *second, bool vertical);
    QString processorName(ProcessorType type) const;

    std::function<std::string()> m_sourceProvider;
    std::function<ProcessorType()> m_processorTypeProvider;

    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_latencyCombo = nullptr;
    QPushButton *m_analyzeButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_revertButton = nullptr;
    QWidget *m_latencyPanel = nullptr;
    QSpinBox *m_loadUseSpin = nullptr;
    QSpinBox *m_aluSpin = nullptr;
    QSpinBox *m_mulSpin = nullptr;
    QSpinBox *m_divSpin = nullptr;
    QSpinBox *m_noForwardingSpin = nullptr;
    QLabel *m_banner = nullptr;
    QTableWidget *m_origTable = nullptr;
    QTableWidget *m_suggTable = nullptr;
    QListWidget *m_explanations = nullptr;
    QPushButton *m_selectAllButton = nullptr;
    QPushButton *m_selectNoneButton = nullptr;
    QLabel *m_selectionStatus = nullptr;
    QTimer *m_refreshTimer = nullptr;

    KitesTraceRunner m_runner;
    AssembledProgram m_program;    ///< The analyzed (expanded) program.
    std::string m_expandedSource;  ///< Its source text.
    ProgramSuggestion m_full;      ///< All accepted blocks; m_last is the selected subset.
    std::vector<bool> m_selection; ///< Per block; only accepted blocks can be true.
    bool m_customPseudo = false;
    bool m_filling = false; ///< Guards itemChanged while the explanations are rebuilt.
    TraceRunResult m_originalRun;
    bool m_hasResult = false;
    bool m_stale = false;
    bool m_syncing = false;
    AnalysisContext m_context;
    ProgramSuggestion m_last;
    RunSummary m_run;
    PipelineDiagram m_origDiag;
    PipelineDiagram m_suggDiag;
    std::string m_analyzedSource;
    std::string m_preApplyText;
};
} // namespace Kites
