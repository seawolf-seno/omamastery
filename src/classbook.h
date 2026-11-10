#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "school.h"

// A core idea of the course, from "One Body, Many Members", and the shared
// joint across subjects it belongs to.
struct CoreIdea {
    QString name;
    QString description;
    QString joint;
};

// One learning objective in the class plan, scheduled for a week.
struct Objective {
    QString id;
    QString title;
    QString idea;
    QDate week; // the Monday of the week it is taught
};

struct ClassInfo {
    QString id;   // folder name under ~/Classes
    QString dir;
    QString name;
    QString course;
    QString day;  // "A" or "B"
    int block = 0;
    QString room; // the Veyon room OmaClass uses for this class
    bool sample = false;
    QStringList students;
    QVector<Objective> objectives;
    QVector<CoreIdea> ideas;
    QString error; // why a file in the folder could not be read
};

// Which class is happening now, or next today.
struct NowInfo {
    enum Status { Now, Next, None };
    Status status = None;
    int index = -1;  // into the class list
    QString text;    // one line for people: "Block 1 · now", "No school: Veterans Day"
};

// Plain files under one folder (normally ~/Classes) that the teacher and their
// agent both edit. The app never keeps its own copy: every read goes back to
// the files, so an agent's change shows up as soon as it is saved.
class Classbook {
public:
    explicit Classbook(const QString &root);

    static QString defaultRoot();
    QString root() const { return m_root; }
    QString schedulePath() const { return m_root + "/schedule.toml"; }
    bool exists() const;

    QVector<ClassInfo> loadClasses() const;

    // Cleaning up. Both take a class's folder name and touch nothing else.
    // Archive moves the folder, every file in it, to archive/<school year>/
    // (e.g. archive/2026-27/ap-csp), so it leaves OmaMastery and OmaClass but
    // nothing is lost. Delete moves it to the desktop Trash, where it can be
    // restored until the Trash is emptied.
    QString schoolYear() const; // "2026-27", from schedule.toml
    bool archiveClass(const QString &id, QString *movedTo, QString *error) const;
    bool deleteClass(const QString &id, QString *error) const;
    // Puts an archived class back: archive/<year>/<id> → <id>.
    bool restoreClass(const QString &archivedPath, QString *error) const;
    static ClassInfo loadClass(const QString &dir);

    // The levels, lowest first. Words, not numbers, and none of them a failure.
    static const QStringList &levels();

    // mastery.csv is an append-only log: date,student,objective,level,evidence.
    // The latest row for a student and objective is their current level.
    static QHash<QString, QString> loadMastery(const QString &csvPath);
    // The same, with where each current level came from: "From Canvas: 9 of 10 · Oct 10".
    static QHash<QString, QString> loadMasteryNotes(const QString &csvPath);
    static QString masteryKey(const QString &student, const QString &objective);
    static bool appendMastery(const QString &csvPath, const QDate &date, const QString &student,
                              const QString &objective, const QString &level, const QString &evidence,
                              QString *error = nullptr);

    // Several rows in one write (filling a whole objective). Same rules as appendMastery.
    struct MasteryRow {
        QString student;
        QString objective;
        QString level;
        QString evidence;
    };
    static bool appendMasteryRows(const QString &csvPath, const QDate &date, const QVector<MasteryRow> &rows,
                                  QString *error = nullptr);

    static QDate weekStart(const QDate &date);
    static NowInfo now(const QVector<ClassInfo> &classes, const Schedule &schedule, const QDateTime &when);

    // Minimal RFC 4180 CSV, enough for the mastery log.
    static QVector<QStringList> parseCsv(const QString &text);
    static QString csvField(const QString &value);

private:
    QString m_root;
};
