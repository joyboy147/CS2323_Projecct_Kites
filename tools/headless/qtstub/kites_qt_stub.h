// Minimal headless stand-in for the parts of Qt6 used by Kites' non-UI core.
// NOT a Qt implementation: signals are no-ops, there is no event loop and no moc.
#pragma once
#include <chrono>
#include <climits>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <memory>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <type_traits>
#include <string>
#include <utility>
#include <vector>

// ---- moc keywords --------------------------------------------------------
// Signals are declared as ordinary member functions; trivial (empty) bodies are
// supplied by headless/qt_signal_stubs.cpp, so `emit foo(x)` is a real,
// harmless no-op call (no unresolved symbols, nothing to crash at runtime).
#define Q_OBJECT
#define Q_GADGET
#define Q_INVOKABLE
#define Q_SIGNALS public
#define Q_SLOTS
#define Q_EMIT
#define signals public
#define slots
#define emit
#define Q_UNUSED(x) (void)(x)
#define Q_DECLARE_METATYPE(T)
#define Q_ENUM(x)
#define Q_PROPERTY(...)

using qsizetype = std::ptrdiff_t;

namespace Qt
{
enum ConnectionType
{
    AutoConnection,
    DirectConnection,
    QueuedConnection,
    BlockingQueuedConnection
};
}

namespace qtstub_detail
{
template <typename F, typename Tuple, typename... Leading, std::size_t... I>
constexpr bool invocableWithFirst(std::index_sequence<I...>)
{
    return std::is_invocable_v<F, Leading..., std::tuple_element_t<I, Tuple>...>;
}
/** Qt accepts slots taking a prefix of the signal's arguments. */
template <typename F, typename Tuple, typename... Leading, std::size_t... N>
constexpr bool anyPrefix(std::index_sequence<N...>)
{
    return (invocableWithFirst<F, Tuple, Leading...>(std::make_index_sequence<N>{}) || ...);
}
template <typename F, typename R, typename... Args> constexpr bool slotAccepts()
{
    using T = std::tuple<Args...>;
    using Prefixes = std::make_index_sequence<sizeof...(Args) + 1>;
    if constexpr (std::is_member_function_pointer_v<F>)
        return anyPrefix<F, T, R *>(Prefixes{});
    else
        return anyPrefix<F, T>(Prefixes{});
}
} // namespace qtstub_detail

class QObject
{
  public:
    // Type-checked like Qt's pointer-to-member connect(); no dispatch happens headlessly.
    template <typename S, typename SC, typename SR, typename... SA, typename R, typename F>
    static bool connect(const S *, SR (SC::*)(SA...), const R *, F)
    {
        static_assert(std::is_base_of_v<SC, S>, "signal is not a member of the sender");
        static_assert(qtstub_detail::slotAccepts<F, R, SA...>(),
                      "slot is not callable with (a prefix of) the signal's arguments");
        return true;
    }
    explicit QObject(QObject *parent = nullptr) : parent_(parent) {}
    virtual ~QObject() = default;
    QObject(const QObject &)            = delete;
    QObject &operator=(const QObject &) = delete;
    QObject *parent() const { return parent_; }
    // connect() is accepted but does nothing (no signal dispatch headlessly).
    template <typename... A> static bool disconnect(A &&...) { return true; }

  private:
    QObject *parent_;
};

#ifndef KITES_QTSTUB_WIDGETS // the syntax-check widget stub defines a fuller QWidget
class QWidget : public QObject
{
  public:
    explicit QWidget(QWidget *parent = nullptr) : QObject(parent) {}
};
#endif

class QString : public std::string
{
  public:
    using std::string::string;
    QString() = default;
    QString(const std::string &s) : std::string(s) {}
    QString(std::string &&s) : std::string(std::move(s)) {}
    static QString fromStdString(const std::string &s) { return QString(s); }
    static QString fromUtf8(const char *s) { return QString(s); }
    static QString fromLatin1(const char *s) { return QString(s); }
    std::string toStdString() const { return *this; }
    bool isEmpty() const { return empty(); }
    QString toHtmlEscaped() const { return *this; }
    QString arg(const QString &a, int fieldWidth = 0, char fill = ' ') const;
    QString arg(int a, int fieldWidth = 0, int base = 10, char fill = ' ') const;
    QString arg(unsigned a, int fieldWidth = 0, int base = 10, char fill = ' ') const;
    QString arg(long a, int fieldWidth = 0, int base = 10, char fill = ' ') const;
    QString arg(unsigned long a, int fieldWidth = 0, int base = 10, char fill = ' ') const;
    QString arg(long long a, int fieldWidth = 0, int base = 10, char fill = ' ') const;
    QString arg(unsigned long long a, int fieldWidth = 0, int base = 10, char fill = ' ') const;
    QString arg(double a, int fieldWidth = 0, char format = 'g', int precision = -1,
                char fill = ' ') const;
    static QString number(int n, int base = 10);
    static QString number(unsigned n, int base = 10);
    static QString number(long n, int base = 10);
    static QString number(unsigned long n, int base = 10);
    static QString number(double n, char format = 'g', int precision = 6);
};
template <> struct std::hash<QString>
{
    size_t operator()(const QString &s) const noexcept { return std::hash<std::string>{}(s); }
};

template <typename T> class QList : public std::vector<T>
{
  public:
    using std::vector<T>::vector;
    void append(const T &v) { this->push_back(v); }
    void prepend(const T &v) { this->insert(this->begin(), v); }
    bool contains(const T &v) const
    {
        for (const auto &x : *this)
            if (x == v) return true;
        return false;
    }
    bool isEmpty() const { return this->empty(); }
    qsizetype count() const { return static_cast<qsizetype>(this->size()); }
};
class QStringList : public QList<QString>
{
  public:
    using QList<QString>::QList;
    QStringList &operator<<(const QString &s)
    {
        append(s);
        return *this;
    }
    QString join(const QString &separator) const;
    qsizetype size() const { return static_cast<qsizetype>(QList<QString>::size()); }
};

template <typename T> class QSet : public std::set<T>
{
  public:
    using std::set<T>::set;
    bool contains(const T &v) const { return this->find(v) != this->end(); }
};

template <typename K, typename V> class QMap : public std::map<K, V>
{
  public:
    using std::map<K, V>::map;
    bool contains(const K &k) const { return this->find(k) != this->end(); }
    V value(const K &k, const V &d = V()) const
    {
        auto it = this->find(k);
        return it == this->end() ? d : it->second;
    }
    void insert(const K &k, const V &v) { (*this)[k] = v; }
};

using qulonglong = unsigned long long;
class QVariant
{
  public:
    QVariant() = default;
    QVariant(int) {}
    QVariant(bool) {}
    QVariant(double) {}
    bool toBool() const;
    QVariant(qulonglong) {}
    QVariant(const QString &) {}
    int toInt() const;
    qulonglong toULongLong() const;
};

class QMutex
{
  public:
    void lock() { m_.lock(); }
    void unlock() { m_.unlock(); }
    bool tryLock() { return m_.try_lock(); }
    std::mutex &native() { return m_; }

  private:
    std::mutex m_;
};

class QMutexLocker
{
  public:
    explicit QMutexLocker(QMutex *m) : m_(m) { m_->lock(); }
    ~QMutexLocker()
    {
        if (m_) m_->unlock();
    }
    void unlock()
    {
        if (m_) m_->unlock();
        m_ = nullptr;
    }
    QMutexLocker(const QMutexLocker &)            = delete;
    QMutexLocker &operator=(const QMutexLocker &) = delete;

  private:
    QMutex *m_;
};

class QWaitCondition
{
  public:
    // Caller holds *m (as with Qt). Returns false on timeout.
    bool wait(QMutex *m, unsigned long ms = ULONG_MAX)
    {
        std::unique_lock<std::mutex> lk(m->native(), std::adopt_lock);
        bool ok = true;
        if (ms == ULONG_MAX)
            cv_.wait(lk);
        else
            ok = cv_.wait_for(lk, std::chrono::milliseconds(ms)) == std::cv_status::no_timeout;
        lk.release(); // keep the mutex locked, ownership stays with caller
        return ok;
    }
    void wakeAll() { cv_.notify_all(); }
    void wakeOne() { cv_.notify_one(); }

  private:
    std::condition_variable cv_;
};

// ---- qDebug & friends ------------------------------------------------------
// Swallowed by default; set KITES_QDEBUG=1 to see them on stderr.
class QDebug
{
  public:
    QDebug() : on_(enabled()) {}
    ~QDebug()
    {
        if (on_) std::cerr << std::endl;
    }
    template <typename T> QDebug &operator<<(const T &v)
    {
        if (on_) std::cerr << v << ' ';
        return *this;
    }
    static bool enabled()
    {
        static const bool e = [] {
            const char *v = std::getenv("KITES_QDEBUG");
            return v && *v && *v != '0';
        }();
        return e;
    }

  private:
    bool on_;
};
inline QDebug qDebug() { return {}; }
inline QDebug qInfo() { return {}; }
inline QDebug qWarning() { return {}; }
inline QDebug qCritical() { return {}; }
