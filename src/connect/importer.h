#pragma once

#include <QDate>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVector>

#include "../school.h"

// The one-time setup: what Canvas and Blackbaud say about a teacher's classes,
// turned into OmaMastery's plain files. Everything here is pure (JSON in,
// drafts or files out), so it is tested without a network.
//
// Privacy: real student names only ever go to <class>/roster.toml on this
// computer. class.toml, plan.toml, ideas.toml, syllabus.md and canvas-plan.md
// hold no student data, so an agent can draft objectives from them.
namespace Importer {

struct Student {
    QString name;
    QString canvasId;
    QString blackbaudId;
};

struct DraftObjective {
    QString id;
    QString title;
    QString idea;
    QDate week; // Monday, or invalid when not yet scheduled
};

struct ClassDraft {
    QString id; // folder name under ~/Classes
    QString name;
    QString course; // course code, e.g. "CS-AP-1"
    QString canvasCourse;
    QString canvasSis; // Canvas SIS ids, joined; Blackbaud's LMS sync puts the section id here
    QString blackbaudSection;
    QString day; // "A", "B" or empty when unknown
    int block = 0;
    QString room;
    int studentCount = -1;      // from the course list, before any roster is read
    QVector<Student> students;
    QVector<DraftObjective> objectives;
    QStringList ideas;          // core-idea names found in Canvas (Outcome groups)
    QString planSource;         // "Canvas Outcomes", "Canvas Modules" or empty
    QString canvasPlan;         // canvas-plan.md: modules, items and dates
    QString syllabus;           // syllabus.md, from the Canvas syllabus page
    bool selected = true;
};

struct MeetingBlock { // one block's bell times, read from Blackbaud meetings
    int number = 0;
    QTime start;
    QTime end;
};

// ---- Canvas ----
// GET /api/v1/courses?enrollment_type=teacher → one draft per course.
QVector<ClassDraft> draftsFromCanvasCourses(const QJsonArray &courses);
// GET /api/v1/courses/:id/users?enrollment_type[]=student
QVector<Student> studentsFromCanvas(const QJsonArray &users);
// GET /api/v1/courses/:id/outcome_group_links?outcome_style=full
void objectivesFromOutcomes(const QJsonArray &links, ClassDraft *draft);
// GET /api/v1/courses/:id/modules?include[]=items and /assignments: one
// draft objective per module, in the week it opens or its work is due.
void objectivesFromModules(const QJsonArray &modules, const QJsonArray &assignments, ClassDraft *draft);
QString syllabusText(const QString &html);

// "AP CS Principles - Block 3A", "B2", "Period 4": day and block, if the name says.
void guessDayBlock(const QString &text, QString *day, int *block);

// ---- Blackbaud ----
// Teacher's sections → drafts for classes that aren't matched to Canvas.
QVector<ClassDraft> draftsFromBlackbaudSections(const QJsonArray &sections);
// GET /school/v1/academics/sections/:id/students
QVector<Student> studentsFromBlackbaud(const QJsonArray &students);
// Matches each Blackbaud section to a Canvas draft (SIS id first, then name),
// copies its section id onto the draft, and returns the sections left over.
QVector<ClassDraft> matchSections(QVector<ClassDraft> *canvas, const QVector<ClassDraft> &blackbaud);
// GET /school/v1/schedules/meetings: day (from the school calendar), block,
// room and bell times for every draft with a Blackbaud section.
QVector<MeetingBlock> applyMeetings(const QJsonArray &meetings, const Schedule &schedule, QVector<ClassDraft> *drafts);
QTime clockTime(const QString &value);

// ---- Files ----
QString slug(const QString &text);
// Folder ids that don't collide with each other or with folders on disk that
// belong to a different class. A folder already set up for the same Canvas
// course or Blackbaud section keeps its id, so running setup again updates it.
void assignIds(const QString &root, QVector<ClassDraft> *drafts);
// The folder (if any) under root that was set up from this draft's source.
QString existingFolder(const QString &root, const ClassDraft &draft);

QString classToml(const ClassDraft &draft);
QString rosterToml(const ClassDraft &draft);
QString planToml(const ClassDraft &draft);
QString ideasToml(const ClassDraft &draft);

// Writes one class. A new class gets every file. A class set up before keeps
// its plan, ideas, notes and mastery log: only the roster (and an empty day,
// block or room) is refreshed. Returns a one-line report with counts, never names.
bool writeClass(const QString &root, const ClassDraft &draft, QString *report, QString *error);
// Replaces schedule.toml's placeholder bell times with the ones from Blackbaud.
// Leaves bell times the teacher already set alone.
bool writeBellTimes(const QString &schedulePath, const QVector<MeetingBlock> &blocks, QString *report);

// The folder (or a parent) is a git work tree: refuse, so real names can't be committed.
QString gitRepoAbove(const QString &path);

} // namespace Importer
