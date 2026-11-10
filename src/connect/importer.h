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
    QString canvasAssignment; // the Canvas assignment it came from, if any
    double points = 0;        // that assignment's points possible
    QString group;            // its Canvas assignment group (a unit or skill)
};

// One student's graded Canvas submission.
struct Score {
    QString canvasUser;
    QString assignment;
    double score = -1; // -1 when Canvas has no score
    QString grade;     // Canvas's grade text: "9", "90%", or a word like "Proficient"
    QDate graded;
};

// Score → starting level cutoffs, as a percent of the points. With mastery
// work out of 10: 10 Extending, 9 Proficient, 8 Developing, 7 Beginning.
struct ScoreLevels {
    double extending = 100;
    double proficient = 90;
    double developing = 80;
    double beginning = 70;
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
    QVector<Score> scores;      // graded work on the assignment objectives
    QStringList ideas;          // core-idea names found in Canvas (Outcome groups)
    QString planSource;         // "Canvas Outcomes", "Canvas Modules" or empty
    QString canvasPlan;         // canvas-plan.md: modules, items and dates
    QString syllabus;           // syllabus.md, from the Canvas syllabus page
    bool selected = true;
    QString note;               // why it isn't ticked: "2nd Semester", "Teacher's Assistant"
    QString term;               // its Canvas term's name
    QDate termEnd;              // when that term ends, if Canvas says
    QString continues;          // the earlier Canvas course whose folder this one takes over
};

struct MeetingBlock { // one block's bell times, read from Blackbaud meetings
    int number = 0;
    QTime start;
    QTime end;
};

// ---- Canvas ----
// GET /api/v1/courses?enrollment_type=teacher → one draft per course.
// Listed but not ticked: courses with no students, Teacher's Assistant
// and Advisory courses, and a later term's copy of a course (a school that makes one
// Canvas course per semester: only the semester under way is ticked).
QVector<ClassDraft> draftsFromCanvasCourses(const QJsonArray &courses, const QDate &today = QDate::currentDate());
// GET /api/v1/courses/:id/users?enrollment_type[]=student
QVector<Student> studentsFromCanvas(const QJsonArray &users);
// GET /api/v1/courses/:id/assignment_groups?include[]=assignments: every
// assignment outside the "Non-Mastery Assignments" group is an objective, in
// the week it's due. The first choice; Outcomes and then Modules fill in for
// a course where that leaves nothing.
void objectivesFromAssignmentGroups(const QJsonArray &groups, ClassDraft *draft);
bool isNonMasteryGroup(const QString &name);
QJsonArray assignmentsIn(const QJsonArray &groups);
// GET /api/v1/courses/:id/students/submissions?student_ids[]=all: graded ones only.
QVector<Score> scoresFromCanvas(const QJsonArray &submissions);
// [score_levels] in schedule.toml, or the defaults.
ScoreLevels loadScoreLevels(const QString &schedulePath);
// The starting level for a score: a level word Canvas already gave wins;
// otherwise the percent of points against the cutoffs. Empty when ungraded.
QString levelForScore(const Score &score, double points, const ScoreLevels &levels);
// Writes a starting level into mastery.csv for every graded assignment
// objective whose cell is still empty. Never changes a level already there.
// Returns how many it wrote, or -1 on error.
// Adds `group = "..."` to objectives in plan.toml that don't have one yet,
// matched to the draft by Canvas assignment id or by title. Leaves everything
// else in the file as it is. Returns how many it added, or -1 on error.
int addGroups(const QString &classDir, const ClassDraft &draft, QString *error);
int seedLevels(const QString &classDir, const ClassDraft &draft, const ScoreLevels &levels, QString *error);

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
//
// A new semester: where a school makes one Canvas course per semester, the
// next semester's course takes over the folder of the same-named class whose
// course ended earlier (or is no longer listed), and keeps its plan and
// levels. `listed` is every course setup saw, for the terms; empty means drafts.
void assignIds(const QString &root, QVector<ClassDraft> *drafts, const QVector<ClassDraft> &listed = {});
// Adds Canvas assignments that aren't in plan.toml yet (by assignment id or
// title) as new objectives at the end. Never changes or removes one already
// there. Returns how many it added, or -1 on error.
int addNewObjectives(const QString &classDir, const ClassDraft &draft, QString *error);
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
