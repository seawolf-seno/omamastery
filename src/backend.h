#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariant>

#include "classbook.h"
#include "school.h"

// The app's state for QML: the class list, the class on screen, its plan and
// mastery levels. Everything comes from the files in ~/Classes and is read
// again whenever they change, whoever changed them.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString root READ root CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList classes READ classes NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
    Q_PROPERTY(int nowIndex READ nowIndex NOTIFY changed)
    Q_PROPERTY(QString nowText READ nowText NOTIFY changed)
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
    Q_PROPERTY(QStringList students READ students NOTIFY changed)
    Q_PROPERTY(QVariantList objectives READ objectives NOTIFY changed)
    Q_PROPERTY(QVariantMap levels READ levels NOTIFY changed)
    // "student|objective" → where the current level came from, and when.
    Q_PROPERTY(QVariantMap levelNotes READ levelNotes NOTIFY changed)
    Q_PROPERTY(QStringList levelNames READ levelNames CONSTANT)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString thisWeek READ thisWeek NOTIFY changed)
    Q_PROPERTY(QString schoolYear READ schoolYear NOTIFY changed)

public:
    explicit Backend(const QString &root = {}, QObject *parent = nullptr);

    bool ready() const { return m_book.exists(); }
    QString root() const { return m_book.root(); }
    QString error() const { return m_error; }
    QString status() const { return m_status; }
    QVariantList classes() const;
    int currentIndex() const { return m_current; }
    int nowIndex() const { return m_now.index; }
    QString nowText() const { return m_now.text; }
    QVariantMap current() const;
    QStringList students() const;
    QVariantList objectives() const;
    QVariantMap levels() const;
    QVariantMap levelNotes() const;
    QStringList levelNames() const { return Classbook::levels(); }
    QString notes() const { return m_notes; }
    QString thisWeek() const { return m_today.toString(Qt::ISODate); }
    QString schoolYear() const { return m_book.schoolYear(); }

    Q_INVOKABLE void selectClass(int index);
    Q_INVOKABLE void selectClassId(const QString &id);
    Q_INVOKABLE void goToNow();
    Q_INVOKABLE void setLevel(const QString &student, const QString &objective, const QString &level);
    Q_INVOKABLE void cycleLevel(const QString &student, const QString &objective, int step);
    // Sets every student with no level yet on this objective; returns how many.
    Q_INVOKABLE int fillObjective(const QString &objective, const QString &level);
    Q_INVOKABLE void askAgent(const QString &kind);
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void setUpSample();
    Q_INVOKABLE void reload();
    // Cleaning up: archive keeps every file in archive/<year>/; delete moves the
    // folder to the Trash. Both by folder name; true when it worked.
    Q_INVOKABLE bool archiveClass(const QString &id);
    Q_INVOKABLE bool deleteClass(const QString &id);

    // For tests: pretend it is this moment instead of now.
    void setClock(const QDateTime &when);

    static QString agentPrompt(const QString &kind, const ClassInfo &info);

signals:
    void changed();
    void statusChanged();

private:
    void rearmWatcher();
    void loadCurrentFiles();
    void say(const QString &text);
    QDateTime clock() const;

    Classbook m_book;
    Schedule m_schedule;
    QVector<ClassInfo> m_classes;
    QHash<QString, QString> m_mastery;
    QHash<QString, QString> m_levelNotes;
    QString m_notes;
    QString m_error;
    QString m_status;
    NowInfo m_now;
    int m_current = -1;
    QString m_currentId;
    QDate m_today;
    QDateTime m_fixedClock;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QTimer m_minute;
};
