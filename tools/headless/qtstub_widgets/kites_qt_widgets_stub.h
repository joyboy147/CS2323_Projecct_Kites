// Syntax-check-only stand-in for the Qt6 Widgets/Gui API used by the Suggestions tab.
// Signatures follow Qt 6 (signals are plain member declarations so connect() can type-check
// them). Nothing here is ever linked or run.
#pragma once
#define KITES_QTSTUB_WIDGETS
#include "kites_qt_stub.h"

#include <array>

namespace Qt
{
enum Orientation
{
    Horizontal = 0x1,
    Vertical = 0x2
};
enum TextFormat
{
    PlainText,
    RichText,
    AutoText
};
enum ItemDataRole
{
    DisplayRole = 0,
    ToolTipRole = 3,
    UserRole = 0x0100
};
enum AlignmentFlag
{
    AlignLeft = 0x1,
    AlignRight = 0x2,
    AlignVCenter = 0x80,
    AlignCenter = 0x84
};
enum CheckState
{
    Unchecked = 0,
    PartiallyChecked = 1,
    Checked = 2
};
enum ItemFlag
{
    NoItemFlags = 0,
    ItemIsSelectable = 1,
    ItemIsUserCheckable = 16,
    ItemIsEnabled = 32
};
using ItemFlags = int;
enum SortOrder
{
    AscendingOrder,
    DescendingOrder
};
using Alignment = int;
enum CursorShape
{
    ArrowCursor,
    WaitCursor = 3
};
enum GlobalColor
{
    black = 2,
    white = 3,
    transparent = 19
};
enum FocusPolicy
{
    NoFocus = 0,
    StrongFocus = 11
};
} // namespace Qt

class QSize
{
  public:
    QSize();
    QSize(int w, int h);
};
class QColor
{
  public:
    QColor();
    QColor(const char *name);
    QColor(Qt::GlobalColor color);
    QColor(int r, int g, int b, int a = 255);
    void setAlpha(int alpha);
    QString name() const;
    QColor darker(int factor = 200) const;
    QColor lighter(int factor = 150) const;
};
class QBrush
{
  public:
    QBrush(const QColor &color);
    QBrush(Qt::GlobalColor color);
};
class QFont
{
  public:
    void setBold(bool enable);
};
class QCursor
{
  public:
    QCursor(Qt::CursorShape shape);
};
class QIcon
{
  public:
    QIcon();
    explicit QIcon(const QString &fileName);
};
class QPixmap
{
};
class QPalette
{
};
class QTextFormat
{
  public:
    enum Property
    {
        FullWidthSelection = 0x06000
    };
    void setProperty(int propertyId, const QVariant &value);
    void setBackground(const QBrush &brush);
    void setForeground(const QBrush &brush);
};
class QTextCharFormat : public QTextFormat
{
};
class QJsonObject
{
};
template <typename K, typename V> class QHash
{
};
template <typename T> using QVector = QList<T>;

class QModelIndex
{
  public:
    int row() const;
    int column() const;
};
using QModelIndexList = QList<QModelIndex>;

class QAbstractItemModel : public QObject
{
  public:
    explicit QAbstractItemModel(QObject *parent = nullptr);
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const = 0;
    virtual QVariant headerData(int section, Qt::Orientation orientation,
                                int role = Qt::DisplayRole) const;
    virtual int rowCount(const QModelIndex &parent = QModelIndex()) const = 0;
    virtual int columnCount(const QModelIndex &parent = QModelIndex()) const = 0;
};
class QAbstractTableModel : public QAbstractItemModel
{
  public:
    using QAbstractItemModel::QAbstractItemModel;
};
class QItemSelection
{
  public:
    void select(const QModelIndex &topLeft, const QModelIndex &bottomRight);
};
class QItemSelectionModel : public QObject
{
  public:
    enum SelectionFlag
    {
        NoUpdate = 0,
        Clear = 1,
        Select = 2,
        Rows = 0x20,
        ClearAndSelect = Clear | Select
    };
    using SelectionFlags = int;
    void select(const QItemSelection &selection, SelectionFlags command);
    QModelIndexList selectedRows(int column = 0) const;
};

class QPaintEvent;
class QShowEvent;
class QFontMetrics;
class QResizeEvent;
class QEvent
{
  public:
    enum Type
    {
        None = 0,
        KeyPress = 6,
        ToolTip = 110
    };
    Type type() const;
};
class QKeyEvent : public QEvent
{
  public:
    int key() const;
    int modifiers() const;
    QString text() const;
};
class QWidget : public QObject
{
  public:
    explicit QWidget(QWidget *parent = nullptr);
    int width() const;
    int height() const;
    void update();
    QFontMetrics fontMetrics() const;
    void setMinimumHeight(int h);
    void setMaximumHeight(int h);
    virtual QSize sizeHint() const;

  protected:
    virtual void paintEvent(QPaintEvent *event);
    virtual void showEvent(QShowEvent *event);
    virtual void resizeEvent(QResizeEvent *event);
    virtual void changeEvent(QEvent *event);
    virtual bool event(QEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);

  public:
    void setVisible(bool visible);
    void setEnabled(bool enabled);
    void setFont(const QFont &font);
    QFont font() const;
    void setToolTip(const QString &tip);
    void setStyleSheet(const QString &sheet);
    void setFocusPolicy(Qt::FocusPolicy policy);
};

class QLayout : public QObject
{
  public:
    void setContentsMargins(int left, int top, int right, int bottom);
    void addWidget(QWidget *widget);
};
class QBoxLayout : public QLayout
{
  public:
    void addWidget(QWidget *widget, int stretch = 0, Qt::Alignment alignment = 0);
    void addLayout(QLayout *layout, int stretch = 0);
    void addStretch(int stretch = 0);
};
class QVBoxLayout : public QBoxLayout
{
  public:
    QVBoxLayout();
    explicit QVBoxLayout(QWidget *parent);
};
class QHBoxLayout : public QBoxLayout
{
  public:
    QHBoxLayout();
    explicit QHBoxLayout(QWidget *parent);
};

class QLabel : public QWidget
{
  public:
    explicit QLabel(QWidget *parent = nullptr);
    explicit QLabel(const QString &text, QWidget *parent = nullptr);
    void setWordWrap(bool on);
    void setTextFormat(Qt::TextFormat format);
    void setText(const QString &text);
    QString text() const;
    void clear();
};

class QAbstractButton : public QWidget
{
  public:
    void clicked(bool checked = false); // signal
};
class QCheckBox : public QAbstractButton
{
  public:
    explicit QCheckBox(const QString &text, QWidget *parent = nullptr);
    bool isChecked() const;
    void setChecked(bool checked);
    void toggled(bool checked); // signal
};
class QPushButton : public QAbstractButton
{
  public:
    void setFlat(bool flat);
    explicit QPushButton(QWidget *parent = nullptr);
    explicit QPushButton(const QString &text, QWidget *parent = nullptr);
    QPushButton(const QIcon &icon, const QString &text, QWidget *parent = nullptr);
};

class QComboBox : public QWidget
{
  public:
    explicit QComboBox(QWidget *parent = nullptr);
    void addItems(const QStringList &texts);
    int currentIndex() const;
    void currentIndexChanged(int index); // signal
};

class QAbstractSlider : public QWidget
{
  public:
    void setValue(int value);
    void valueChanged(int value); // signal
};
class QScrollBar : public QAbstractSlider
{
};
class QSpinBox : public QWidget
{
  public:
    explicit QSpinBox(QWidget *parent = nullptr);
    void setRange(int minimum, int maximum);
    void setValue(int value);
    int value() const;
    void valueChanged(int value); // signal
};

class QSplitter : public QWidget
{
  public:
    explicit QSplitter(QWidget *parent = nullptr);
    explicit QSplitter(Qt::Orientation orientation, QWidget *parent = nullptr);
    void addWidget(QWidget *widget);
    void setStretchFactor(int index, int stretch);
};
class QGroupBox : public QWidget
{
  public:
    explicit QGroupBox(QWidget *parent = nullptr);
    explicit QGroupBox(const QString &title, QWidget *parent = nullptr);
    void setTitle(const QString &title);
};
class QToolBar : public QWidget
{
};

class QHeaderView : public QWidget
{
  public:
    enum ResizeMode
    {
        Interactive,
        Stretch,
        Fixed,
        ResizeToContents
    };
    void setSectionResizeMode(ResizeMode mode);
    void setSectionResizeMode(int logicalIndex, ResizeMode mode);
    void setDefaultSectionSize(int size);
};

class QAbstractItemView : public QWidget
{
  public:
    enum EditTrigger
    {
        NoEditTriggers = 0
    };
    using EditTriggers = int;
    enum SelectionBehavior
    {
        SelectItems,
        SelectRows,
        SelectColumns
    };
    enum SelectionMode
    {
        NoSelection,
        SingleSelection,
        MultiSelection,
        ExtendedSelection,
        ContiguousSelection
    };
    enum ScrollHint
    {
        EnsureVisible
    };
    void setEditTriggers(EditTriggers triggers);
    void setSelectionBehavior(SelectionBehavior behavior);
    void setSelectionMode(SelectionMode mode);
    QItemSelectionModel *selectionModel() const;
    QAbstractItemModel *model() const;
    void clearSelection();
    QScrollBar *verticalScrollBar() const;
    QScrollBar *horizontalScrollBar() const;
};

class QTableWidgetItem
{
  public:
    QTableWidgetItem();
    explicit QTableWidgetItem(const QString &text);
    void setText(const QString &text);
    void setTextAlignment(Qt::Alignment alignment);
    void setForeground(const QBrush &brush);
    void setBackground(const QBrush &brush);
    void setToolTip(const QString &toolTip);
    void setData(int role, const QVariant &value);
    QVariant data(int role) const;
};
class QTableWidget : public QAbstractItemView
{
  public:
    explicit QTableWidget(QWidget *parent = nullptr);
    QTableWidget(int rows, int columns, QWidget *parent = nullptr);
    void setRowCount(int rows);
    void setColumnCount(int columns);
    int rowCount() const;
    int columnCount() const;
    void setHorizontalHeaderLabels(const QStringList &labels);
    void setVerticalHeaderLabels(const QStringList &labels);
    QHeaderView *horizontalHeader() const;
    QHeaderView *verticalHeader() const;
    void setWordWrap(bool on);
    void clear();
    void setItem(int row, int column, QTableWidgetItem *item);
    QTableWidgetItem *item(int row, int column) const;
    void scrollToItem(const QTableWidgetItem *item, ScrollHint hint = EnsureVisible);
    void clearContents();
    void insertRow(int row);
    void setCellWidget(int row, int column, QWidget *widget);
    void setSortingEnabled(bool enable);
    void sortItems(int column, Qt::SortOrder order = Qt::AscendingOrder);
    void itemSelectionChanged();          // signal
    void cellClicked(int row, int column);       // signal
    void cellDoubleClicked(int row, int column); // signal
};

class QListWidget;
class QListWidgetItem
{
  public:
    explicit QListWidgetItem(const QString &text, QListWidget *listview = nullptr, int type = 0);
    void setData(int role, const QVariant &value);
    QVariant data(int role) const;
    QFont font() const;
    void setFont(const QFont &font);
    Qt::ItemFlags flags() const;
    void setFlags(Qt::ItemFlags flags);
    Qt::CheckState checkState() const;
    void setCheckState(Qt::CheckState state);
};
class QListWidget : public QAbstractItemView
{
  public:
    explicit QListWidget(QWidget *parent = nullptr);
    void setWordWrap(bool on);
    void clear();
    void addItem(QListWidgetItem *item);
    QListWidgetItem *item(int row) const;
    int count() const;
    void setCurrentRow(int row);
    void currentRowChanged(int currentRow); // signal
    void itemChanged(QListWidgetItem *item); // signal
};

class QApplication
{
  public:
    static void setOverrideCursor(const QCursor &cursor);
    static void restoreOverrideCursor();
};

inline int qRound(double d)
{
    return static_cast<int>(d + 0.5);
}
class QPointF
{
};
class QRectF
{
  public:
    QRectF(double x, double y, double w, double h);
    QRectF translated(const QPointF &offset) const;
    QRectF adjusted(double dx1, double dy1, double dx2, double dy2) const;
    void setWidth(double w);
    double top() const;
    double height() const;
};
class QRect
{
  public:
    int top() const;
    int bottom() const;
};
class QPaintEvent
{
  public:
    QRect rect() const;
};
class QShowEvent
{
};
class QPainter
{
  public:
    explicit QPainter(QWidget *device);
    void fillRect(const QRectF &rect, const QColor &color);
    void fillRect(const QRect &rect, const QColor &color);
    void fillRect(int x, int y, int w, int h, const QColor &color);
    void setPen(const QColor &color);
    void drawText(int x, int y, int w, int h, int flags, const QString &text);
    void drawText(const QRectF &rect, int flags, const QString &text);
    enum RenderHint
    {
        Antialiasing = 1
    };
    void setRenderHint(RenderHint hint, bool on = true);
};
class QSettings
{
  public:
    QSettings(const QString &organization, const QString &application);
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);
};
class QGridLayout : public QLayout
{
  public:
    void removeWidget(QWidget *widget);
    void addWidget(QWidget *widget, int row, int column);
};

template <typename T> class QFuture
{
  public:
    T result() const;
};
template <typename T> class QFutureWatcher : public QObject
{
  public:
    void setFuture(const QFuture<T> &future);
    bool isRunning() const;
    void waitForFinished();
    T result() const;
    void finished(); // signal
};
namespace QtConcurrent
{
template <typename F> QFuture<std::invoke_result_t<F>> run(F function)
{
    (void)function;
    return {};
}
}

class QTimer : public QObject
{
  public:
    explicit QTimer(QObject *parent = nullptr);
    void setSingleShot(bool singleShot);
    void setInterval(int msec);
    void start();
    void stop();
    void timeout(); // signal
};

class QLatin1Char
{
  public:
    explicit QLatin1Char(char c);
};
class QLatin1String
{
  public:
    explicit QLatin1String(const char *s);
};
class QFontMetrics
{
  public:
    int horizontalAdvance(QLatin1Char c) const;
    int horizontalAdvance(const QString &text) const;
    int horizontalAdvance(QLatin1String text) const;
    int height() const;
};
class QResizeEvent
{
};
class QFontDatabase
{
};
class QRegularExpression
{
};
class QTextBlock
{
  public:
    bool isValid() const;
    bool isVisible() const;
    int blockNumber() const;
    QTextBlock next() const;
};
class QTextDocument : public QObject
{
  public:
    QTextBlock findBlockByNumber(int blockNumber) const;
};
class QTextCursor
{
  public:
    QTextCursor();
    explicit QTextCursor(const QTextBlock &block);
    void clearSelection();
};
class QSyntaxHighlighter : public QObject
{
  public:
    explicit QSyntaxHighlighter(QTextDocument *parent);

  protected:
    virtual void highlightBlock(const QString &text) = 0;
};
class QTextEdit : public QWidget
{
  public:
    struct ExtraSelection
    {
        QTextCursor cursor;
        QTextCharFormat format;
    };
};
class QPlainTextEdit : public QWidget
{
  public:
    enum LineWrapMode
    {
        NoWrap,
        WidgetWidth
    };
    explicit QPlainTextEdit(QWidget *parent = nullptr);
    void setReadOnly(bool ro);
    void setLineWrapMode(LineWrapMode mode);
    QTextDocument *document() const;
    void setExtraSelections(const QList<QTextEdit::ExtraSelection> &selections);
    QWidget *viewport() const;
    void setTextCursor(const QTextCursor &cursor);
    void centerCursor();
    void setPlainText(const QString &text);
    QString toPlainText() const;
    void setFocus();
    int blockCount() const;

  protected:
    QTextBlock firstVisibleBlock() const;
    QRectF blockBoundingGeometry(const QTextBlock &block) const;
    QRectF blockBoundingRect(const QTextBlock &block) const;
    QPointF contentOffset() const;
};

class QTableView : public QAbstractItemView
{
  public:
    QHeaderView *horizontalHeader() const;
    QHeaderView *verticalHeader() const;
    void setModel(QAbstractItemModel *model);
};

class QLineEdit : public QWidget
{
};

class QRadioButton : public QAbstractButton
{
  public:
    void setChecked(bool checked);
    bool isChecked() const;
    void toggled(bool checked); // signal
};
class QStackedWidget : public QWidget
{
  public:
    QLayout *layout() const;
    QWidget *widget(int index) const;
    void setCurrentIndex(int index);
    int addWidget(QWidget *widget);
};

class QCompleter : public QObject
{
};
class QLinearGradient
{
  public:
    QLinearGradient(double x1, double y1, double x2, double y2);
    void setColorAt(double position, const QColor &color);
};
