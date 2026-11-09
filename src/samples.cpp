#include "samples.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

#include "classbook.h"

namespace {

bool writeNew(const QString &path, const QString &contents, QStringList *created, QString *error) {
    if (QFileInfo::exists(path))
        return true;
    QSaveFile file(path);
    const QByteArray bytes = contents.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QStringLiteral("Couldn't write %1").arg(path);
        return false;
    }
    created->append(path);
    return true;
}

QString tomlString(const QString &value) {
    QString escaped = value;
    escaped.replace("\\", "\\\\").replace("\"", "\\\"");
    return '"' + escaped + '"';
}

struct SampleObjective {
    const char *id;
    const char *week;
    const char *idea;
    const char *title;
};

struct SampleClass {
    const char *id;
    const char *name;
    const char *day;
    int block;
    const char *room;
    QStringList students;
    QVector<SampleObjective> objectives;
    const char *longRangePlan;
};

// The five core ideas of Computer Science in "One Body, Many Members", and the
// joint each one shares with other subjects.
const char *kComputerScienceIdeas = R"(# Core ideas of the course, from "One Body, Many Members".
# Each idea names the joint it shares with other subjects, so objectives can be
# connected across the school. Edit these with your department.

[[idea]]
name = "Variables"
description = "Named places that hold state, a value that can change."
joint = "Systems & Models"

[[idea]]
name = "Functions"
description = "Named, reusable procedures that hide detail behind inputs and outputs."
joint = "Pattern & Structure"

[[idea]]
name = "Conditionals"
description = "Decisions: do this if that is true. The if-then logic of every argument."
joint = "Evidence & Argument"

[[idea]]
name = "Lists"
description = "Collections of data you can store, search, and transform together."
joint = "Measurement & Uncertainty"

[[idea]]
name = "Loops"
description = "Repetition: do something many times, or until a condition is met."
joint = "Draft, Test, Revise"
)";

QVector<SampleClass> sampleClasses() {
    return {
        {"ap-csp", "AP Computer Science Principles", "A", 1, "Home",
         {"Ava", "Ben", "Chloe", "Diego", "Ellie", "Finn", "Grace", "Hugo", "Isla", "Jonah", "Kai", "Lena"},
         {{"csp-variables", "2026-08-24", "Variables", "Store and update program state in variables"},
          {"csp-if-else", "2026-09-07", "Conditionals", "Choose between actions with if and else"},
          {"csp-boolean", "2026-09-14", "Conditionals", "Combine conditions with and, or and not"},
          {"csp-loops", "2026-09-21", "Loops", "Repeat a step until a condition is met"},
          {"csp-lists", "2026-09-28", "Lists", "Store a collection of data in a list and traverse it"},
          {"csp-filter", "2026-10-05", "Lists", "Filter and summarize a list to answer a question"},
          {"csp-functions", "2026-10-19", "Functions", "Write a function with parameters and a return value"},
          {"csp-abstraction", "2026-10-26", "Functions", "Break a problem into smaller functions"},
          {"csp-search", "2026-11-02", "Lists", "Search a data set to answer a real question"},
          {"csp-nested", "2026-11-09", "Loops", "Trace nested loops and predict their output"},
          {"csp-api", "2026-11-30", "Functions", "Use a library from its documentation"},
          {"csp-internet", "2026-12-07", "Functions", "Explain how a request travels across the Internet"}},
         R"(# AP Computer Science Principles: long-range plan (sample)

Made-up plan for trying OmaMastery. Replace it with your own, or drop your
department's long-range plan into this folder and ask your agent to turn it
into objectives in plan.toml.

- Unit 1 (Aug-Sep): Programs and state. Variables, conditionals, Boolean logic.
- Unit 2 (Sep-Oct): Data. Lists, traversal, filtering, simple data sets.
- Unit 3 (Oct-Nov): Abstraction. Functions, parameters, procedural abstraction, nested loops.
- Unit 4 (Nov-Dec): Libraries and the Internet. APIs, packets, protocols.
- Spring: the Create performance task, algorithms, impact of computing, AP review.
)"},
        {"foundations", "Foundations of Technology", "B", 2, "",
         {"Maya", "Noah", "Olive", "Parker", "Quinn", "Rosa", "Sam", "Theo", "Uma", "Violet"},
         {{"fot-files", "2026-08-25", "Variables", "Find, name and organize files in the terminal"},
          {"fot-commands", "2026-09-08", "Functions", "Run and combine commands like mkdir, ls and cd"},
          {"fot-git", "2026-09-29", "Loops", "Save work with Git: add, commit, repeat"},
          {"fot-python", "2026-10-20", "Variables", "Write a Python program that stores input in variables"},
          {"fot-decisions", "2026-11-03", "Conditionals", "Make a program decide with if and else"},
          {"fot-lists", "2026-11-17", "Lists", "Keep a list of items and print each one"}},
         R"(# Foundations of Technology: long-range plan (sample)

Made-up plan for trying OmaMastery.

- Unit 1: The machine. Files, the terminal, the operating system.
- Unit 2: Version control. Git and GitHub.
- Unit 3: First programs. Python variables, input, decisions, lists.
)"},
        {"advanced-cs", "Advanced CS w/ AI Agents", "A", 3, "",
         {"Wes", "Xena", "Yara", "Zane", "Abby", "Cole", "Dana", "Eli"},
         {{"acs-api", "2026-08-26", "Functions", "Design an API endpoint with clear inputs and outputs"},
          {"acs-state", "2026-09-16", "Variables", "Keep app state in a database table"},
          {"acs-auth", "2026-10-07", "Conditionals", "Protect a route with authentication"},
          {"acs-render", "2026-10-28", "Loops", "Render a list of records in the UI"},
          {"acs-tools", "2026-11-04", "Functions", "Give an agent a tool it can call"},
          {"acs-loop", "2026-11-18", "Loops", "Explain the agent loop: think, act, observe"}},
         R"(# Advanced CS w/ AI Agents: long-range plan (sample)

Made-up plan for trying OmaMastery.

- Unit 1: Full-stack apps. APIs, databases, authentication, front-end lists.
- Unit 2: AI agents. Tools, memory, routing and guardrails, the agent loop.
)"},
    };
}

// A believable spread of levels for the made-up students, so the gradebook
// looks like a real class: most students developing or proficient, a few
// beginning or extending. Deterministic, so every sample looks the same.
QString sampleLevel(const QString &student, const QString &objective) {
    const uint hash = qHash(student + "|" + objective, 7u);
    const int roll = int(hash % 100);
    if (roll < 8) return "Beginning";
    if (roll < 38) return "Developing";
    if (roll < 86) return "Proficient";
    return "Extending";
}

} // namespace

QString Samples::scheduleToml() {
    return R"(# When classes meet. OmaMastery and OmaClass read this to open the class
# that's happening now. Edit it freely, or ask your agent to.
#
# From the 2026-27 Teacher Planning Calendar: Monday and Wednesday are A days,
# Tuesday and Thursday are B days, and Fridays alternate (listed below).

first_day = "2026-08-19"
last_day = "2027-05-20"
a_days = ["Mon", "Wed"]
b_days = ["Tue", "Thu"]

# Bell schedule. PLACEHOLDER TIMES: replace them with your school's real ones.
blocks = [
  { number = 1, start = "08:00", end = "09:25" },
  { number = 2, start = "09:35", end = "11:00" },
  { number = 3, start = "11:40", end = "13:05" },
  { number = 4, start = "13:15", end = "14:40" },
]

[fridays]
"2026-08-21" = "A"
"2026-08-28" = "B"
"2026-09-11" = "A"
"2026-09-18" = "A"
"2026-09-25" = "B"
"2026-10-02" = "A"
"2026-10-09" = "B"
"2026-10-16" = "A"
"2026-10-23" = "A"
"2026-10-30" = "B"
"2026-11-06" = "A"
"2026-11-13" = "A"
"2026-11-20" = "B"
"2026-12-04" = "A"
"2026-12-11" = "B"
"2027-01-08" = "A"
"2027-01-15" = "B"
"2027-01-22" = "A"
"2027-01-29" = "B"
"2027-02-05" = "A"
"2027-02-12" = "B"
"2027-02-19" = "A"
"2027-02-26" = "B"
"2027-03-05" = "A"
"2027-03-12" = "B"
"2027-03-19" = "A"
"2027-04-16" = "A"
"2027-04-23" = "B"
"2027-04-30" = "A"
"2027-05-07" = "B"
"2027-05-14" = "A"

[[off]]
from = "2026-08-31"
to = "2026-09-02"
label = "All-School Retreat"

[[off]]
from = "2026-09-03"
to = "2026-09-04"
label = "No school"

[[off]]
from = "2026-09-07"
label = "Labor Day"

[[off]]
from = "2026-10-12"
label = "No school: Columbus/Indigenous Peoples' Day"

[[off]]
from = "2026-10-21"
label = "PSAT, no classes"

[[off]]
from = "2026-11-11"
label = "No school: Veterans Day"

[[off]]
from = "2026-11-23"
to = "2026-11-27"
label = "Thanksgiving Break"

[[off]]
from = "2026-12-14"
to = "2026-12-17"
label = "Finals"

[[off]]
from = "2026-12-18"
to = "2027-01-03"
label = "Christmas Break"

[[off]]
from = "2027-01-18"
label = "No school: MLK Day"

[[off]]
from = "2027-02-15"
label = "No school: Presidents' Day"

[[off]]
from = "2027-03-26"
label = "Good Friday, no school"

[[off]]
from = "2027-03-29"
to = "2027-04-11"
label = "Spring Break"

[[off]]
from = "2027-05-17"
to = "2027-05-20"
label = "Finals"
)" + scoreLevelsToml();
}

QString Samples::scoreLevelsToml() {
    return R"(
# Starting mastery levels from Canvas grades, as a percent of the points.
# Mastery work is out of 10: 10 Extending, 9 Proficient, 8 Developing,
# 7 Beginning, below that Not yet. Setup only fills cells that are still empty.
[score_levels]
Extending = 100
Proficient = 90
Developing = 80
Beginning = 70
)";
}

QString Samples::agentsGuide() {
    return R"(# Classes: guide for AI agents

This folder is a teacher's mastery gradebook and planning hub. The teacher
works in the OmaMastery app; you work in these files. The app reloads as soon
as you save, so the teacher sees your changes immediately.

## Files

- `schedule.toml`: A/B days, Friday rotation, days off, bell times.
- `<class>/class.toml`: name, course, `day` ("A" or "B"), `block`, `room`
  (the Veyon room in OmaClass), `sample` (true = made-up students), `students`
  (sample classes only; a real class keeps its students in `roster.toml`).
- `<class>/ideas.toml`: the course's core ideas from "One Body, Many Members",
  each with the shared `joint` it connects to across subjects.
- `<class>/plan.toml`: learning objectives as `[[objective]]` tables with
  `id` (stable, lowercase-with-dashes), `title` (what a student can do),
  `idea` (one of the core ideas), `week` (the Monday it's taught, YYYY-MM-DD).
- `<class>/mastery.csv`: append-only log `date,student,objective,level,evidence`.
  Levels, lowest first: Not yet, Beginning, Developing, Proficient, Extending.
  The latest row for a student and objective is their current level.
- `<class>/notes.md`: the planning hub. Newest entry on top, under a dated
  `## ` heading.
- `<class>/long-range-plan.*`, `<class>/syllabus*`: the teacher's long-range
  plan and syllabus, if they added them. A `.docx` gets a Markdown copy
  (`.md`, or `-document.md`) for you to read.
- `<class>/syllabus.md`, `<class>/canvas-plan.md`: the course's Canvas syllabus
  and modules, read at setup. No student data.
- `<class>/roster.toml`: a real class's students. Private; see below.

## Rules

- Ask before changing more than one class, or before deleting anything.
- Never write to `mastery.csv`. Levels are the teacher's judgment; suggest
  changes in `notes.md` instead.
- Keep `plan.toml` valid TOML. Keep existing objective `id`s; new ones get new ids.
- When spacing objectives, skip days off in `schedule.toml`, leave room before
  breaks and finals, and keep each objective tied to a core idea.
- Next steps should be concrete and small: what to reteach, who to pull aside,
  which practice to give. Group students; don't rank them.
- Words, not scores. No points, badges, streaks, or failure language.
- Student records are private (FERPA). Real student names stay on this
  computer unless the school's AI policy allows sharing them with you. Until
  then, classes with `sample = true` are the only ones to work with fully.
)" + realClassRules();
}

QString Samples::realClassRules() {
    return R"(
## Real classes (`sample = false`)

Their course plans hold no student data, so you may help with them: read and
edit `class.toml`, `plan.toml` and `ideas.toml`, and read `long-range-plan.*`,
`syllabus*`, `canvas-plan.md` and `schedule.toml`.

Never open `roster.toml`, `mastery.csv` or `notes.md` in a real class: they
hold real students' names and the teacher's notes about them. Never write a
student's name anywhere.
)";
}

void Samples::addRealClassRules(const QString &agentsPath) {
    QFile file(agentsPath);
    if (!file.open(QIODevice::ReadWrite))
        return;
    const QByteArray text = file.readAll();
    // Added once, below whatever the teacher already has.
    if (text.contains("roster.toml"))
        return;
    file.seek(file.size());
    file.write(realClassRules().toUtf8());
}

bool Samples::init(const QString &root, bool withSampleClasses, QString *report, QString *error) {
    QStringList created;
    if (!QDir().mkpath(root)) {
        *error = QStringLiteral("Couldn't create %1").arg(root);
        return false;
    }
    if (!writeNew(root + "/schedule.toml", scheduleToml(), &created, error)) return false;
    if (!writeNew(root + "/AGENTS.md", agentsGuide(), &created, error)) return false;
    // Claude Code reads CLAUDE.md; point it at the same guide.
    if (!writeNew(root + "/CLAUDE.md", "@AGENTS.md\n", &created, error)) return false;

    if (withSampleClasses) {
        for (const auto &sample : sampleClasses()) {
            const QString dir = root + "/" + sample.id;
            if (QFileInfo::exists(dir))
                continue;
            QDir().mkpath(dir);

            QStringList students;
            for (const auto &name : sample.students)
                students.append(tomlString(name));
            const QString classToml =
                QStringLiteral("# Made-up sample class for trying OmaMastery. No real students.\n"
                               "name = %1\ncourse = \"cs\"\nday = %2\nblock = %3\nroom = %4\nsample = true\n"
                               "students = [%5]\n")
                    .arg(tomlString(sample.name), tomlString(sample.day))
                    .arg(sample.block)
                    .arg(tomlString(sample.room), students.join(", "));
            if (!writeNew(dir + "/class.toml", classToml, &created, error)) return false;

            QString plan = "# Learning objectives, in the order taught. Each is tied to a core idea in\n"
                           "# ideas.toml and scheduled for the week (its Monday) it's taught.\n";
            for (const auto &objective : sample.objectives)
                plan += QStringLiteral("\n[[objective]]\nid = %1\ntitle = %2\nidea = %3\nweek = %4\n")
                            .arg(tomlString(objective.id), tomlString(objective.title), tomlString(objective.idea),
                                 tomlString(objective.week));
            if (!writeNew(dir + "/plan.toml", plan, &created, error)) return false;
            if (!writeNew(dir + "/ideas.toml", kComputerScienceIdeas, &created, error)) return false;
            if (!writeNew(dir + "/long-range-plan.md", sample.longRangePlan, &created, error)) return false;

            // Evidence for every objective taught before the sample's "this
            // week" (the week of 2026-11-02), recorded on that week's Friday.
            QString csv = "date,student,objective,level,evidence\n";
            static const QStringList evidence{"Quiz", "Project check-in", "Oral check", "Exit ticket", "Lab"};
            for (const auto &objective : sample.objectives) {
                const QDate week = QDate::fromString(objective.week, Qt::ISODate);
                if (!week.isValid() || week >= QDate(2026, 11, 2))
                    continue;
                for (const auto &student : sample.students) {
                    const QString level = sampleLevel(student, objective.id);
                    const QString source = evidence[int(qHash(student + objective.id, 3u) % evidence.size())];
                    csv += QStringList{Classbook::weekStart(week).addDays(4).toString(Qt::ISODate), student,
                                       objective.id, level, source}.join(',') + "\n";
                }
            }
            if (!writeNew(dir + "/mastery.csv", csv, &created, error)) return false;

            const QString notes = QStringLiteral(
                "# Planning notes: %1\n\n"
                "Newest on top. Write here yourself, or press \"Plan next steps\" and let your\n"
                "agent draft the next entry.\n\n"
                "## 2026-11-02\n\n"
                "- Example entry. Lists: about a third of the class is still developing on\n"
                "  filtering. Reteach with a small data set on Wednesday before searching.\n")
                                      .arg(sample.name);
            if (!writeNew(dir + "/notes.md", notes, &created, error)) return false;
        }
    }

    for (auto &path : created)
        path = QDir(root).relativeFilePath(path);
    *report = created.isEmpty() ? QStringLiteral("Nothing to do: %1 is already set up.").arg(root)
                                : QStringLiteral("Created in %1:\n  %2").arg(root, created.join("\n  "));
    return true;
}
