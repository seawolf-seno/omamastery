#include "backend.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include "documents.h"
#include "samples.h"

Backend::Backend(const QString &root, QObject *parent) : QObject(parent), m_book(root) {
    // Agents often save several files in a row; one reload covers them all.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Backend::reload);

    // The class that's "now" moves with the clock, so check once a minute.
    m_minute.setInterval(60 * 1000);
    connect(&m_minute, &QTimer::timeout, this, [this] {
        const int before = m_now.index;
        const QString text = m_now.text;
        m_now = Classbook::now(m_classes, m_schedule, clock());
        if (m_now.index != before || m_now.text != text)
            emit changed();
    });
    m_minute.start();
    reload();
}

QDateTime Backend::clock() const {
    return m_fixedClock.isValid() ? m_fixedClock : QDateTime::currentDateTime();
}

void Backend::setClock(const QDateTime &when) {
    m_fixedClock = when;
    // A new "now" means opening the class happening then, as a fresh start would.
    m_currentId.clear();
    reload();
}

void Backend::reload() {
    m_error.clear();
    m_schedule = Schedule();
    if (m_book.exists()) {
        QString scheduleError;
        if (!m_schedule.load(m_book.schedulePath(), &scheduleError))
            m_error = scheduleError;
    }
    m_classes = m_book.loadClasses();
    m_today = clock().date();
    m_now = Classbook::now(m_classes, m_schedule, clock());

    // Stay on the class the teacher was looking at; otherwise open the one
    // that's happening now (or next), then the first.
    int index = -1;
    for (int i = 0; i < m_classes.size(); ++i)
        if (m_classes[i].id == m_currentId)
            index = i;
    if (index < 0)
        index = m_now.index >= 0 ? m_now.index : (m_classes.isEmpty() ? -1 : 0);
    m_current = index;
    m_currentId = index >= 0 ? m_classes[index].id : QString();
    if (m_error.isEmpty() && index >= 0)
        m_error = m_classes[index].error;

    loadCurrentFiles();
    rearmWatcher();
    emit changed();
}

void Backend::loadCurrentFiles() {
    m_mastery.clear();
    m_levelNotes.clear();
    m_notes.clear();
    if (m_current < 0)
        return;
    const QString dir = m_classes[m_current].dir;
    m_mastery = Classbook::loadMastery(dir + "/mastery.csv");
    m_levelNotes = Classbook::loadMasteryNotes(dir + "/mastery.csv");
    QFile notes(dir + "/notes.md");
    if (notes.open(QIODevice::ReadOnly))
        m_notes = QString::fromUtf8(notes.readAll().left(200 * 1024));
}

void Backend::rearmWatcher() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);
    // Editors and agents replace files rather than writing in place, which
    // drops a file watch, so watch the folders too and re-arm on every reload.
    QStringList watch{QFileInfo(m_book.root()).absolutePath()};
    if (QFileInfo::exists(m_book.root()))
        watch << m_book.root() << m_book.schedulePath();
    for (const auto &info : m_classes) {
        watch << info.dir;
        for (const char *name : {"class.toml", "plan.toml", "ideas.toml", "mastery.csv", "notes.md"}) {
            const QString path = info.dir + "/" + name;
            if (QFileInfo::exists(path))
                watch << path;
        }
    }
    watch.removeDuplicates();
    watch.removeIf([](const QString &path) { return !QFileInfo::exists(path); });
    if (!watch.isEmpty())
        m_watcher.addPaths(watch);
}

QVariantList Backend::classes() const {
    QVariantList list;
    for (int i = 0; i < m_classes.size(); ++i) {
        const auto &info = m_classes[i];
        list.append(QVariantMap{{"id", info.id},
                                {"name", info.name},
                                {"day", info.day},
                                {"block", info.block},
                                {"sample", info.sample},
                                {"isNow", i == m_now.index && m_now.status == NowInfo::Now},
                                {"isNext", i == m_now.index && m_now.status == NowInfo::Next}});
    }
    return list;
}

QVariantMap Backend::current() const {
    if (m_current < 0)
        return {};
    const auto &info = m_classes[m_current];
    QVariantList ideas;
    for (const auto &idea : info.ideas)
        ideas.append(QVariantMap{{"name", idea.name}, {"description", idea.description}, {"joint", idea.joint}});
    return QVariantMap{{"id", info.id},
                       {"name", info.name},
                       {"day", info.day},
                       {"block", info.block},
                       {"room", info.room},
                       {"sample", info.sample},
                       {"dir", info.dir},
                       {"ideas", ideas},
                       {"hasLongRangePlan", !QDir(info.dir).entryList({"long-range-plan.*"}, QDir::Files).isEmpty()}};
}

QStringList Backend::students() const {
    return m_current < 0 ? QStringList() : m_classes[m_current].students;
}

QVariantList Backend::objectives() const {
    QVariantList list;
    if (m_current < 0)
        return list;
    const QDate week = Classbook::weekStart(m_today);
    const auto &info = m_classes[m_current];
    QHash<QString, QString> joints;
    for (const auto &idea : info.ideas)
        joints.insert(idea.name, idea.joint);
    for (const auto &objective : info.objectives) {
        list.append(QVariantMap{
            {"id", objective.id},
            {"title", objective.title},
            {"idea", objective.idea},
            {"joint", joints.value(objective.idea)},
            {"week", objective.week.isValid() ? objective.week.toString(Qt::ISODate) : QString()},
            {"weekLabel", objective.week.isValid() ? objective.week.toString("MMM d") : QStringLiteral("Not scheduled")},
            {"taught", objective.week.isValid() && objective.week <= week},
            {"thisWeek", objective.week == week}});
    }
    return list;
}

QVariantMap Backend::levelNotes() const {
    QVariantMap map;
    for (auto it = m_levelNotes.begin(); it != m_levelNotes.end(); ++it) {
        QString key = it.key();
        key.replace(QChar(0x1f), '|');
        map.insert(key, it.value());
    }
    return map;
}

int Backend::fillObjective(const QString &objective, const QString &level) {
    if (m_current < 0 || !Classbook::levels().contains(level))
        return 0;
    QVector<Classbook::MasteryRow> rows;
    for (const auto &student : m_classes[m_current].students)
        if (!m_mastery.contains(Classbook::masteryKey(student, objective)))
            rows.append({student, objective, level, QStringLiteral("Set for the whole objective in OmaMastery")});
    if (rows.isEmpty()) {
        say(tr("Everyone already has a level for this objective."));
        return 0;
    }
    QString error;
    if (!Classbook::appendMasteryRows(m_classes[m_current].dir + "/mastery.csv", clock().date(), rows, &error)) {
        say(error);
        return 0;
    }
    for (const auto &row : rows) {
        m_mastery.insert(Classbook::masteryKey(row.student, objective), level);
        m_levelNotes.insert(Classbook::masteryKey(row.student, objective),
                            row.evidence + " · " + clock().date().toString("MMM d"));
    }
    say(tr("Set %n empty cells to %1", nullptr, int(rows.size())).arg(tr(level.toUtf8().constData())));
    emit changed();
    return int(rows.size());
}

QVariantMap Backend::levels() const {
    QVariantMap map;
    for (auto it = m_mastery.begin(); it != m_mastery.end(); ++it) {
        // QML can't use the 0x1f separator comfortably; it reads "student|objective".
        QString key = it.key();
        key.replace(QChar(0x1f), '|');
        map.insert(key, it.value());
    }
    return map;
}

void Backend::selectClass(int index) {
    if (index < 0 || index >= m_classes.size() || index == m_current)
        return;
    m_current = index;
    m_currentId = m_classes[index].id;
    m_error = m_classes[index].error;
    loadCurrentFiles();
    emit changed();
}

void Backend::selectClassId(const QString &id) {
    for (int i = 0; i < m_classes.size(); ++i)
        if (m_classes[i].id == id)
            selectClass(i);
}

void Backend::goToNow() {
    if (m_now.index >= 0)
        selectClass(m_now.index);
}

void Backend::setLevel(const QString &student, const QString &objective, const QString &level) {
    if (m_current < 0)
        return;
    QString error;
    if (!Classbook::appendMastery(m_classes[m_current].dir + "/mastery.csv", clock().date(), student, objective,
                                  level, QStringLiteral("Set in OmaMastery"), &error)) {
        say(error);
        return;
    }
    // Shown right away; the file watcher confirms it a moment later.
    m_mastery.insert(Classbook::masteryKey(student, objective), level);
    m_levelNotes.insert(Classbook::masteryKey(student, objective),
                        QStringLiteral("Set in OmaMastery · ") + clock().date().toString("MMM d"));
    emit changed();
}

void Backend::cycleLevel(const QString &student, const QString &objective, int step) {
    const auto &names = Classbook::levels();
    const QString now = m_mastery.value(Classbook::masteryKey(student, objective));
    int index = names.indexOf(now);
    // An empty cell steps to the first real judgment, not past it.
    index = index < 0 ? (step > 0 ? 1 : 0) : qBound(0, index + step, int(names.size()) - 1);
    setLevel(student, objective, names[index]);
}

QString Backend::agentPrompt(const QString &kind, const ClassInfo &info) {
    const QString intro = QStringLiteral(
        "I'm a teacher. You're in the folder for my class \"%1\" inside ~/Classes. "
        "Read ../AGENTS.md first and follow its rules. ").arg(info.name);
    // A real class: the agent works from the course plan only, never from
    // the roster, the mastery log or the teacher's notes about students.
    const QString plans = QStringLiteral(
        "the long-range plan (long-range-plan.*), the syllabus (syllabus*) and canvas-plan.md if they're here; "
        "for a .docx, read its .md copy");
    if (!info.sample) {
        const QString fence = QStringLiteral("This is a real class: never open roster.toml, mastery.csv or notes.md. ");
        if (kind == "space")
            return intro + fence + QStringLiteral(
                "Read %1, ideas.toml, plan.toml and ../schedule.toml. Space the learning objectives across the semester "
                "in plan.toml: one week per objective unless a topic needs more, skip days off, leave the week before "
                "finals for review, and tie every objective to a core idea. Show me the proposed schedule as a short "
                "list before you save it.").arg(plans);
        if (kind == "objectives")
            return intro + fence + QStringLiteral(
                "Read %1, ideas.toml and plan.toml. If ideas.toml is empty, draft the course's core ideas first. Then "
                "draft clear learning objectives for the units plan.toml doesn't cover yet (or turn its draft rows from "
                "Canvas modules into objectives), each one a thing a student can do, tied to a core idea. Keep existing "
                "ids. Show them to me before saving.").arg(plans);
        return intro + fence + QStringLiteral(
            "Read plan.toml and ideas.toml. Look at this week's and last week's objectives. Suggest two or three "
            "small, concrete next steps for this week: what to reteach, what practice or extension to give, and "
            "connections to other subjects through the core ideas' joints. Tell me them in a few lines; I'll decide "
            "which students they're for.");
    }
    if (kind == "space")
        return intro + QStringLiteral(
            "Then read the long-range plan in this folder (long-range-plan.*), ideas.toml, plan.toml and "
            "../schedule.toml. Space the learning objectives across the semester in plan.toml: one week per "
            "objective unless a topic needs more, skip days off, leave the week before finals for review, and "
            "tie every objective to a core idea. Show me the proposed schedule as a short list before you save it.");
    if (kind == "objectives")
        return intro + QStringLiteral(
            "Then read the long-range plan (long-range-plan.*) and ideas.toml. Draft clear learning objectives "
            "for the units that plan.toml doesn't cover yet, each one a thing a student can do, tied to a core "
            "idea. Show them to me before adding them to plan.toml.");
    return intro + QStringLiteral(
        "Then read plan.toml, mastery.csv and notes.md. Look at this week's and last week's objectives and "
        "where students are. Suggest two or three small, concrete next steps for this week: what to reteach, "
        "which groups to pull together, what practice or extension to give, and connections to other "
        "subjects through the core ideas' joints. Add them to the top of notes.md under today's date, "
        "then tell me in a few lines what you added.");
}

void Backend::askAgent(const QString &kind) {
    if (m_current < 0)
        return;
    const auto &info = m_classes[m_current];
    const QString omarchy = QStandardPaths::findExecutable("omarchy");
    if (omarchy.isEmpty()) {
        say(tr("Omarchy's agent launcher isn't installed, so there's no agent to open."));
        return;
    }
    // Word plans get a Markdown copy first, so any agent can read them.
    Documents::convertForAgents(info.dir);
    // Started in the class folder, so the agent sees this class's files first.
    if (!QProcess::startDetached(omarchy, {"agent", "prompt", agentPrompt(kind, info)}, info.dir))
        say(tr("Couldn't start your agent."));
    else
        say(tr("Opened your agent in %1").arg(QDir::home().relativeFilePath(info.dir).prepend("~/")));
}

void Backend::openFolder() {
    const QString dir = m_current >= 0 ? m_classes[m_current].dir : m_book.root();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void Backend::setUpSample() {
    QString report;
    QString error;
    if (!Samples::init(m_book.root(), true, &report, &error)) {
        say(error);
        return;
    }
    reload();
    say(tr("Set up sample classes in %1").arg(m_book.root()));
}

void Backend::say(const QString &text) {
    m_status = text;
    emit statusChanged();
}
