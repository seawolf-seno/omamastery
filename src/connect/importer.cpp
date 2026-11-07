#include "importer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <toml++/toml.hpp>

namespace Importer {

namespace {

QString text(const QJsonObject &o, std::initializer_list<const char *> keys) {
    for (const char *key : keys) {
        const QJsonValue v = o.value(key);
        if (v.isString() && !v.toString().trimmed().isEmpty())
            return v.toString().trimmed();
        if (v.isDouble())
            return QString::number(qint64(v.toDouble()));
    }
    return {};
}

QString tomlString(const QString &value) {
    QString escaped = value;
    escaped.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", " ").replace("\r", " ");
    return '"' + escaped + '"';
}

QDate localDate(const QString &iso) {
    if (iso.isEmpty())
        return {};
    const QDateTime when = QDateTime::fromString(iso, Qt::ISODate);
    return when.isValid() ? when.toLocalTime().date() : QDate::fromString(iso.left(10), Qt::ISODate);
}

QDate monday(const QDate &date) {
    return date.isValid() ? date.addDays(1 - date.dayOfWeek()) : QDate();
}

QString plain(const QString &html) {
    QString s = html;
    static const QRegularExpression breaks(R"(<\s*(br|/p|/li|/h[1-6]|/div|/tr)\s*/?>)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression items(R"(<\s*li[^>]*>)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression heads(R"(<\s*h[1-6][^>]*>)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(R"(<[^>]*>)");
    static const QRegularExpression blank(R"(\n[ \t]*\n(\s*\n)+)");
    s.replace(breaks, "\n").replace(items, "- ").replace(heads, "\n## ").replace(tags, "");
    s.replace("&nbsp;", " ").replace("&quot;", "\"").replace("&#39;", "'").replace("&rsquo;", "'")
        .replace("&ldquo;", "\"").replace("&rdquo;", "\"").replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
    s.replace(blank, "\n\n");
    return s.trimmed();
}

// Folded name for matching a Blackbaud section to a Canvas course.
QString fold(const QString &name) {
    QString s = name.toLower();
    static const QRegularExpression noise(R"(\b(block|blk|period|per|section|sec|semester|sem|fall|spring|20\d\d)\b|\b[1-8][ab]\b|\b[ab][1-8]\b|\([^)]*\)|[^a-z0-9]+)");
    s.replace(noise, " ");
    return s.simplified();
}

bool isHousekeeping(const QString &moduleName) {
    static const QRegularExpression words(
        R"(^\s*(start here|welcome|course (info|information|overview)|syllabus|resources|getting started|class (info|resources)|teacher info)\b)",
        QRegularExpression::CaseInsensitiveOption);
    return words.match(moduleName).hasMatch();
}

QString uniqueId(const QString &base, QSet<QString> *taken) {
    QString id = base.isEmpty() ? QStringLiteral("objective") : base;
    for (int n = 2; taken->contains(id); ++n)
        id = base + "-" + QString::number(n);
    taken->insert(id);
    return id;
}

bool writeFile(const QString &path, const QString &contents, QString *error, bool privateFile = false) {
    QSaveFile file(path);
    const QByteArray bytes = contents.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Couldn't write %1").arg(path);
        return false;
    }
    if (privateFile)
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QString readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

// ---------------------------------------------------------------- Canvas

void guessDayBlock(const QString &text, QString *day, int *block) {
    static const QRegularExpression named(R"(\b(?:block|blk|period|per\.?)\s*([1-8])\s*([AB])?\b)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression numberLetter(R"(\b([1-8])([AB])\b)");
    static const QRegularExpression letterNumber(R"(\b([AB])[- ]?([1-8])\b)");
    static const QRegularExpression dayOnly(R"(\b([AB])[- ]day\b|\(([AB])\))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m;
    if ((m = named.match(text)).hasMatch()) {
        *block = m.captured(1).toInt();
        if (!m.captured(2).isEmpty())
            *day = m.captured(2).toUpper();
    } else if ((m = numberLetter.match(text)).hasMatch()) {
        *block = m.captured(1).toInt();
        *day = m.captured(2);
    } else if ((m = letterNumber.match(text)).hasMatch()) {
        *day = m.captured(1);
        *block = m.captured(2).toInt();
    }
    if (day->isEmpty() && (m = dayOnly.match(text)).hasMatch())
        *day = (m.captured(1) + m.captured(2)).toUpper();
}

QVector<ClassDraft> draftsFromCanvasCourses(const QJsonArray &courses) {
    QVector<ClassDraft> drafts;
    for (const auto &value : courses) {
        const QJsonObject c = value.toObject();
        ClassDraft d;
        d.canvasCourse = text(c, {"id"});
        d.name = text(c, {"name", "course_code"});
        d.course = text(c, {"course_code"});
        if (d.canvasCourse.isEmpty() || d.name.isEmpty())
            continue;
        QStringList sis{text(c, {"sis_course_id"})};
        QString hints = d.name + " " + d.course;
        for (const auto &s : c.value("sections").toArray()) {
            hints += " " + text(s.toObject(), {"name"});
            sis << text(s.toObject(), {"sis_section_id"});
        }
        sis.removeAll(QString());
        d.canvasSis = sis.join(' ');
        guessDayBlock(hints, &d.day, &d.block);
        // A course with no students yet (a sandbox, template or next term's
        // shell) is listed but not ticked.
        if (c.contains("total_students")) {
            d.studentCount = c.value("total_students").toInt();
            d.selected = d.studentCount > 0;
        }
        drafts.append(d);
    }
    return drafts;
}

static QVector<Student> disambiguate(QVector<Student> students, const QStringList &lastNames) {
    // Mastery is logged by name, so two students can't share one. Add a last
    // initial (then more letters) only where first names collide.
    QHash<QString, int> count;
    for (const auto &s : students)
        ++count[s.name];
    QHash<QString, int> initials;
    for (int i = 0; i < students.size(); ++i)
        if (count[students[i].name] > 1)
            ++initials[students[i].name + " " + lastNames.value(i).left(1)];
    for (int i = 0; i < students.size(); ++i) {
        if (count[students[i].name] < 2 || lastNames.value(i).isEmpty())
            continue;
        const QString initial = students[i].name + " " + lastNames[i].left(1);
        students[i].name = initials[initial] > 1 ? students[i].name + " " + lastNames[i] : initial + ".";
    }
    QSet<QString> seen;
    for (auto &s : students) {
        const QString base = s.name;
        for (int n = 2; seen.contains(s.name); ++n)
            s.name = base + " " + QString::number(n);
        seen.insert(s.name);
    }
    return students;
}

QVector<Student> studentsFromCanvas(const QJsonArray &users) {
    QVector<Student> students;
    QStringList lastNames;
    QSet<QString> ids;
    for (const auto &value : users) {
        const QJsonObject u = value.toObject();
        const QString id = text(u, {"id"});
        if (id.isEmpty() || ids.contains(id))
            continue;
        ids.insert(id);
        const QString sortable = text(u, {"sortable_name"});
        students.append({text(u, {"short_name", "name"}), id, QString()});
        lastNames.append(sortable.contains(',') ? sortable.section(',', 0, 0).trimmed() : QString());
    }
    return disambiguate(students, lastNames);
}

void objectivesFromOutcomes(const QJsonArray &links, ClassDraft *draft) {
    QSet<QString> taken;
    for (const auto &o : draft->objectives)
        taken.insert(o.id);
    for (const auto &value : links) {
        const QJsonObject link = value.toObject();
        const QJsonObject outcome = link.value("outcome").toObject();
        QString group = text(link.value("outcome_group").toObject(), {"title"});
        if (group == draft->name || group == draft->course)
            group.clear(); // the course's root group, not a core idea
        const QString title = text(outcome, {"title"});
        const QString statement = plain(text(outcome, {"description"})).simplified();
        QString shown = text(outcome, {"display_name"});
        if (shown.isEmpty())
            shown = !statement.isEmpty() && statement.size() <= 160 ? statement : title;
        if (shown.isEmpty())
            continue;
        draft->objectives.append({uniqueId(slug(title.isEmpty() ? shown : title), &taken), shown, group, QDate()});
        if (!group.isEmpty() && !draft->ideas.contains(group))
            draft->ideas.append(group);
    }
    if (!draft->objectives.isEmpty())
        draft->planSource = QStringLiteral("Canvas Outcomes");
}

void objectivesFromModules(const QJsonArray &modules, const QJsonArray &assignments, ClassDraft *draft) {
    QHash<QString, QDate> dueByAssignment;
    QHash<QString, QDate> dueByQuiz;
    for (const auto &value : assignments) {
        const QJsonObject a = value.toObject();
        const QDate due = localDate(text(a, {"due_at"}));
        dueByAssignment.insert(text(a, {"id"}), due);
        if (!text(a, {"quiz_id"}).isEmpty())
            dueByQuiz.insert(text(a, {"quiz_id"}), due);
    }

    const bool addObjectives = draft->objectives.isEmpty();
    QSet<QString> taken;
    QString md = QStringLiteral("# %1: Canvas modules\n\nRead from Canvas at setup, for planning. No student data.\n").arg(draft->name);
    for (const auto &value : modules) {
        const QJsonObject m = value.toObject();
        const QString name = text(m, {"name"});
        if (name.isEmpty())
            continue;
        QDate week = monday(localDate(text(m, {"unlock_at"})));
        QStringList lines;
        QDate earliestDue;
        for (const auto &itemValue : m.value("items").toArray()) {
            const QJsonObject item = itemValue.toObject();
            const QString type = text(item, {"type"});
            if (type == "SubHeader") {
                lines << QStringLiteral("- **%1**").arg(text(item, {"title"}));
                continue;
            }
            const QString content = text(item, {"content_id"});
            const QDate due = type == "Quiz" ? dueByQuiz.value(content) : dueByAssignment.value(content);
            if (due.isValid() && (!earliestDue.isValid() || due < earliestDue))
                earliestDue = due;
            lines << QStringLiteral("- %1 (%2%3)")
                         .arg(text(item, {"title"}), type,
                              due.isValid() ? QStringLiteral(", due ") + due.toString(Qt::ISODate) : QString());
        }
        if (!week.isValid())
            week = monday(earliestDue);
        md += QStringLiteral("\n## %1%2\n\n").arg(name, week.isValid() ? QStringLiteral(" (week of %1)").arg(week.toString(Qt::ISODate)) : QString());
        md += lines.isEmpty() ? QStringLiteral("(no items)\n") : lines.join('\n') + '\n';
        if (addObjectives && !isHousekeeping(name))
            draft->objectives.append({uniqueId(slug(name), &taken), name, QString(), week});
    }
    draft->canvasPlan = modules.isEmpty() ? QString() : md;
    if (addObjectives && !draft->objectives.isEmpty())
        draft->planSource = QStringLiteral("Canvas Modules");
}

QString syllabusText(const QString &html) {
    return plain(html);
}

// ---------------------------------------------------------------- Blackbaud

QVector<ClassDraft> draftsFromBlackbaudSections(const QJsonArray &sections) {
    QVector<ClassDraft> drafts;
    for (const auto &value : sections) {
        const QJsonObject s = value.toObject();
        ClassDraft d;
        d.blackbaudSection = text(s, {"id", "section_id"});
        d.name = text(s, {"course_title", "name", "section_name", "title"});
        d.course = text(s, {"course_code", "code"});
        if (d.blackbaudSection.isEmpty() || d.name.isEmpty())
            continue;
        const QString identifier = text(s, {"section_identifier", "identifier"});
        guessDayBlock(d.name + " " + identifier + " " + text(s, {"block", "block_name"}), &d.day, &d.block);
        const QString room = text(s, {"room", "room_name"});
        if (!room.isEmpty())
            d.room = room;
        drafts.append(d);
    }
    return drafts;
}

QVector<Student> studentsFromBlackbaud(const QJsonArray &students) {
    QVector<Student> out;
    QStringList lastNames;
    QSet<QString> ids;
    for (const auto &value : students) {
        const QJsonObject s = value.toObject();
        const QString id = text(s, {"id", "user_id"});
        if (id.isEmpty() || ids.contains(id))
            continue;
        ids.insert(id);
        const QString first = text(s, {"preferred_name", "nick_name", "first_name"});
        const QString last = text(s, {"last_name"});
        const QString name = first.isEmpty() ? text(s, {"name", "display_name"}) : first + (last.isEmpty() ? QString() : " " + last);
        if (name.isEmpty())
            continue;
        out.append({name, QString(), id});
        lastNames.append(last);
    }
    return disambiguate(out, lastNames);
}

QVector<ClassDraft> matchSections(QVector<ClassDraft> *canvas, const QVector<ClassDraft> &blackbaud) {
    QVector<ClassDraft> leftover;
    QSet<int> used;
    for (const auto &section : blackbaud) {
        int match = -1;
        // 1. Blackbaud's Canvas sync writes the section id into Canvas's SIS ids.
        static const QRegularExpression splitter(R"([^A-Za-z0-9]+)");
        for (int i = 0; i < canvas->size() && match < 0; ++i) {
            if (used.contains(i))
                continue;
            if ((*canvas)[i].canvasSis.split(splitter, Qt::SkipEmptyParts).contains(section.blackbaudSection))
                match = i;
        }
        // 2. Same course name; with several sections of one course, the same block.
        const QString folded = fold(section.name);
        int byName = -1;
        for (int i = 0; i < canvas->size() && match < 0; ++i) {
            if (used.contains(i))
                continue;
            const QString other = fold((*canvas)[i].name);
            if (folded.isEmpty() || other.isEmpty() || !(other == folded || other.contains(folded) || folded.contains(other)))
                continue;
            const auto &c = (*canvas)[i];
            if (section.block > 0 && c.block > 0 && section.block != c.block)
                continue;
            if (!section.day.isEmpty() && !c.day.isEmpty() && section.day != c.day)
                continue;
            if (section.block > 0 && c.block == section.block)
                match = i;
            else if (byName < 0)
                byName = i;
        }
        if (match < 0)
            match = byName;
        if (match < 0) {
            leftover.append(section);
            continue;
        }
        used.insert(match);
        auto &c = (*canvas)[match];
        c.blackbaudSection = section.blackbaudSection;
        if (c.day.isEmpty())
            c.day = section.day;
        if (c.block == 0)
            c.block = section.block;
        if (c.room.isEmpty())
            c.room = section.room;
        c.selected = true; // on the teacher's real schedule
    }
    return leftover;
}

QTime clockTime(const QString &value) {
    // "08:00", "08:00:00", "1900-01-01T08:00:00-05:00", "8:00 AM". Blackbaud's
    // time zones are unreliable, so the clock time is taken as written.
    static const QRegularExpression clock(R"((?:T|^|\s)(\d{1,2}):(\d{2})(?::\d{2})?(?:\.\d+)?\s*([AaPp][Mm])?)");
    const auto m = clock.match(value.trimmed());
    if (!m.hasMatch())
        return {};
    int hour = m.captured(1).toInt();
    const QString half = m.captured(3).toUpper();
    if (half == "PM" && hour < 12)
        hour += 12;
    if (half == "AM" && hour == 12)
        hour = 0;
    return QTime(hour, m.captured(2).toInt());
}

QVector<MeetingBlock> applyMeetings(const QJsonArray &meetings, const Schedule &schedule, QVector<ClassDraft> *drafts) {
    struct Tally {
        QHash<QString, int> days, rooms;
        QHash<int, int> blocks;
    };
    QHash<QString, Tally> bySection;
    QHash<int, QHash<QString, int>> times; // block → "08:00-09:25" → count

    for (const auto &value : meetings) {
        const QJsonObject m = value.toObject();
        const QString section = text(m, {"section_id", "id"});
        if (section.isEmpty())
            continue;
        Tally &t = bySection[section];
        QString letter;
        int block = 0;
        guessDayBlock(text(m, {"block_name", "block"}), &letter, &block);
        if (block == 0) {
            static const QRegularExpression digit(R"(\d)");
            const auto d = digit.match(text(m, {"block_name", "block"}));
            if (d.hasMatch())
                block = d.captured(0).toInt();
        }
        const QDate date = localDate(text(m, {"meeting_date", "date"}));
        QString day = letter;
        if (date.isValid()) {
            const DayInfo info = schedule.day(date);
            if (!info.type.isEmpty())
                day = info.type;
        }
        if (!day.isEmpty())
            ++t.days[day];
        if (block > 0)
            ++t.blocks[block];
        const QString room = text(m, {"room_name", "room"});
        if (!room.isEmpty())
            ++t.rooms[room];
        const QTime start = clockTime(text(m, {"start_time"}));
        const QTime end = clockTime(text(m, {"end_time"}));
        if (block > 0 && start.isValid() && end.isValid() && start < end)
            ++times[block][start.toString("HH:mm") + "-" + end.toString("HH:mm")];
    }

    auto most = [](const auto &hash) {
        auto best = hash.constBegin();
        for (auto it = hash.constBegin(); it != hash.constEnd(); ++it)
            if (it.value() > best.value())
                best = it;
        return best;
    };

    for (auto &d : *drafts) {
        if (d.blackbaudSection.isEmpty() || !bySection.contains(d.blackbaudSection))
            continue;
        const Tally &t = bySection[d.blackbaudSection];
        if (!t.days.isEmpty()) {
            const auto best = most(t.days);
            int total = 0;
            for (int n : t.days)
                total += n;
            // A class that meets on both kinds of day has no single letter.
            if (best.value() * 4 >= total * 3)
                d.day = best.key();
        }
        if (!t.blocks.isEmpty())
            d.block = most(t.blocks).key();
        if (!t.rooms.isEmpty())
            d.room = most(t.rooms).key();
    }

    QVector<MeetingBlock> bells;
    for (auto it = times.constBegin(); it != times.constEnd(); ++it) {
        const QString pair = most(it.value()).key();
        bells.append({it.key(), QTime::fromString(pair.section('-', 0, 0), "HH:mm"), QTime::fromString(pair.section('-', 1, 1), "HH:mm")});
    }
    std::sort(bells.begin(), bells.end(), [](const MeetingBlock &a, const MeetingBlock &b) { return a.number < b.number; });
    return bells;
}

// ---------------------------------------------------------------- Files

QString slug(const QString &text) {
    QString s = text.toLower();
    static const QRegularExpression other(R"([^a-z0-9]+)");
    s.replace(other, "-");
    s = s.mid(s.indexOf(QRegularExpression("[a-z0-9]")));
    while (s.endsWith('-'))
        s.chop(1);
    if (s.size() > 40) {
        s = s.left(40);
        const int cut = s.lastIndexOf('-');
        if (cut > 20)
            s = s.left(cut);
    }
    return s;
}

QString existingFolder(const QString &root, const ClassDraft &draft) {
    const QDir dir(root);
    for (const auto &entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString path = entry.filePath() + "/class.toml";
        if (!QFileInfo::exists(path))
            continue;
        try {
            const toml::table t = toml::parse_file(path.toStdString());
            const QString canvas = QString::fromStdString(t["canvas_course"].value_or(std::string()));
            const QString section = QString::fromStdString(t["blackbaud_section"].value_or(std::string()));
            if ((!draft.canvasCourse.isEmpty() && canvas == draft.canvasCourse) ||
                (!draft.blackbaudSection.isEmpty() && section == draft.blackbaudSection))
                return entry.fileName();
        } catch (const toml::parse_error &) {
        }
    }
    return {};
}

void assignIds(const QString &root, QVector<ClassDraft> *drafts) {
    QSet<QString> taken;
    // Folders already set up from these classes keep their ids.
    for (auto &d : *drafts) {
        d.id = existingFolder(root, d);
        if (!d.id.isEmpty())
            taken.insert(d.id);
    }
    const QDir dir(root);
    for (const auto &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        taken.insert(entry);
    for (auto &d : *drafts) {
        if (!d.id.isEmpty())
            continue;
        QString base = slug(d.name);
        if (base.isEmpty())
            base = slug(d.course);
        if (base.isEmpty())
            base = QStringLiteral("class");
        QString id = base;
        if (taken.contains(id) && d.block > 0)
            id = base + "-" + d.day.toLower() + QString::number(d.block);
        for (int n = 2; taken.contains(id); ++n)
            id = base + "-" + QString::number(n);
        d.id = id;
        taken.insert(id);
    }
}

QString classToml(const ClassDraft &d) {
    QStringList sources;
    if (!d.canvasCourse.isEmpty())
        sources << "Canvas";
    if (!d.blackbaudSection.isEmpty())
        sources << "Blackbaud";
    return QStringLiteral(
               "# Set up from %1 on %2. A real class: student names are in roster.toml,\n"
               "# which stays on this computer. Fix day, block or room here if they're wrong;\n"
               "# room is the Veyon room OmaClass shows for this class.\n"
               "name = %3\n"
               "course = %4\n"
               "day = %5\n"
               "block = %6\n"
               "room = %7\n"
               "sample = false\n"
               "canvas_course = %8\n"
               "blackbaud_section = %9\n")
        .arg(sources.join(" and "), QDate::currentDate().toString(Qt::ISODate), tomlString(d.name), tomlString(d.course),
             tomlString(d.day))
        .arg(d.block)
        .arg(tomlString(d.room), tomlString(d.canvasCourse), tomlString(d.blackbaudSection));
}

QString rosterToml(const ClassDraft &d) {
    QString out = QStringLiteral(
        "# PRIVATE: real students (FERPA). This file stays on this computer: don't\n"
        "# commit, upload or share it. AI agents never open it. Refreshed by\n"
        "# `omamastery setup`; edit names freely, they're how mastery.csv knows students.\n");
    for (const auto &s : d.students) {
        out += QStringLiteral("\n[[student]]\nname = %1\n").arg(tomlString(s.name));
        if (!s.canvasId.isEmpty())
            out += QStringLiteral("canvas_id = %1\n").arg(tomlString(s.canvasId));
        if (!s.blackbaudId.isEmpty())
            out += QStringLiteral("blackbaud_id = %1\n").arg(tomlString(s.blackbaudId));
    }
    return out;
}

QString planToml(const ClassDraft &d) {
    QString out = QStringLiteral(
        "# Learning objectives, in the order taught. Each is tied to a core idea in\n"
        "# ideas.toml and scheduled for the week (its Monday) it's taught.\n");
    if (d.planSource == "Canvas Modules")
        out += QStringLiteral("#\n# First draft from your Canvas modules: one per module. Your agent can turn\n"
                              "# them into objectives a student can do (\"Draft objectives\").\n");
    else if (d.planSource == "Canvas Outcomes")
        out += QStringLiteral("#\n# From your Canvas Outcomes. Undated ones wait for \"Space across the semester\".\n");
    else
        out += QStringLiteral("#\n# Empty for now: add your long-range plan to this folder, then press\n# \"Draft objectives\" and your agent will fill this in.\n");
    for (const auto &o : d.objectives) {
        out += QStringLiteral("\n[[objective]]\nid = %1\ntitle = %2\nidea = %3\n")
                   .arg(tomlString(o.id), tomlString(o.title), tomlString(o.idea));
        if (o.week.isValid())
            out += QStringLiteral("week = %1\n").arg(tomlString(o.week.toString(Qt::ISODate)));
    }
    return out;
}

QString ideasToml(const ClassDraft &d) {
    QString out = QStringLiteral(
        "# Core ideas of the course, from \"One Body, Many Members\", each with the\n"
        "# shared joint it connects to across subjects.\n");
    if (d.ideas.isEmpty())
        out += QStringLiteral("#\n# Empty for now: your agent drafts these from your long-range plan.\n");
    else
        out += QStringLiteral("#\n# From your Canvas Outcome groups. Add a description and joint to each.\n");
    for (const auto &idea : d.ideas)
        out += QStringLiteral("\n[[idea]]\nname = %1\ndescription = \"\"\njoint = \"\"\n").arg(tomlString(idea));
    return out;
}

// Names the teacher already uses (roster.toml, by Canvas or Blackbaud id), so
// a refresh never renames a student and detaches their mastery history.
static void keepNames(const QString &rosterPath, ClassDraft *d) {
    if (!QFileInfo::exists(rosterPath))
        return;
    QHash<QString, QString> byId;
    try {
        const toml::table t = toml::parse_file(rosterPath.toStdString());
        if (const auto *list = t["student"].as_array()) {
            for (const auto &item : *list) {
                const auto *row = item.as_table();
                if (!row)
                    continue;
                const QString name = QString::fromStdString((*row)["name"].value_or(std::string()));
                for (const char *key : {"canvas_id", "blackbaud_id"}) {
                    const QString id = QString::fromStdString((*row)[key].value_or(std::string()));
                    if (!id.isEmpty() && !name.isEmpty())
                        byId.insert(QString(key) + ":" + id, name);
                }
            }
        }
    } catch (const toml::parse_error &) {
        return;
    }
    for (auto &s : d->students) {
        if (!s.canvasId.isEmpty() && byId.contains("canvas_id:" + s.canvasId))
            s.name = byId.value("canvas_id:" + s.canvasId);
        else if (!s.blackbaudId.isEmpty() && byId.contains("blackbaud_id:" + s.blackbaudId))
            s.name = byId.value("blackbaud_id:" + s.blackbaudId);
    }
}

bool writeClass(const QString &root, const ClassDraft &draft, QString *report, QString *error) {
    ClassDraft d = draft;
    const QString dir = root + "/" + d.id;
    const QString classPath = dir + "/class.toml";
    const bool update = QFileInfo::exists(classPath);
    keepNames(dir + "/roster.toml", &d);
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("Couldn't create %1").arg(dir);
        return false;
    }
    if (!writeFile(dir + "/roster.toml", rosterToml(d), error, true))
        return false;

    if (update) {
        // Only fill in what's still blank: the teacher may have fixed the rest.
        QString text = readFile(classPath);
        const QString before = text;
        auto fill = [&](const QString &blank, const QString &line) {
            QRegularExpression re("^" + QRegularExpression::escape(blank) + "$", QRegularExpression::MultilineOption);
            text.replace(re, line);
        };
        if (!d.day.isEmpty())
            fill("day = \"\"", "day = " + tomlString(d.day));
        if (d.block > 0)
            fill("block = 0", "block = " + QString::number(d.block));
        if (!d.room.isEmpty())
            fill("room = \"\"", "room = " + tomlString(d.room));
        if (!d.blackbaudSection.isEmpty())
            fill("blackbaud_section = \"\"", "blackbaud_section = " + tomlString(d.blackbaudSection));
        if (text != before && !writeFile(classPath, text, error))
            return false;
        *report = QStringLiteral("%1: roster refreshed, %2 students (plan and mastery kept)").arg(d.id).arg(d.students.size());
        return true;
    }

    const QString today = QDate::currentDate().toString(Qt::ISODate);
    QString notes = QStringLiteral("# %1: planning notes\n\nNewest first. You and your agent both write here.\n\n## %2\n\n").arg(d.name, today);
    notes += QStringLiteral("- Set up from %1.\n").arg(d.canvasCourse.isEmpty() ? "Blackbaud" : d.blackbaudSection.isEmpty() ? "Canvas" : "Canvas and Blackbaud");
    if (!d.planSource.isEmpty())
        notes += QStringLiteral("- %1 draft objectives from %2.\n").arg(d.objectives.size()).arg(d.planSource);
    notes += QStringLiteral("- Next: put your long-range plan in this folder (long-range-plan.pdf, .docx or .md), "
                            "then press \"Draft objectives\".\n");

    if (!writeFile(classPath, classToml(d), error) || !writeFile(dir + "/plan.toml", planToml(d), error) ||
        !writeFile(dir + "/ideas.toml", ideasToml(d), error) || !writeFile(dir + "/notes.md", notes, error) ||
        !writeFile(dir + "/mastery.csv", "date,student,objective,level,evidence\n", error, true))
        return false;
    if (!d.syllabus.isEmpty() &&
        !writeFile(dir + "/syllabus.md", QStringLiteral("# %1: syllabus (from Canvas)\n\n%2\n").arg(d.name, d.syllabus), error))
        return false;
    if (!d.canvasPlan.isEmpty() && !writeFile(dir + "/canvas-plan.md", d.canvasPlan, error))
        return false;

    QStringList where;
    if (!d.day.isEmpty() || d.block > 0)
        where << QStringLiteral("%1%2").arg(d.day.isEmpty() ? "?" : d.day, d.block > 0 ? QString::number(d.block) : "?");
    if (!d.room.isEmpty())
        where << QStringLiteral("room %1").arg(d.room);
    *report = QStringLiteral("%1: %2 students, %3 objectives%4")
                  .arg(d.id)
                  .arg(d.students.size())
                  .arg(d.objectives.size())
                  .arg(where.isEmpty() ? QStringLiteral(", day and block not set") : ", " + where.join(", "));
    return true;
}

bool writeBellTimes(const QString &schedulePath, const QVector<MeetingBlock> &blocks, QString *report) {
    QString text = readFile(schedulePath);
    if (blocks.isEmpty() || text.isEmpty() || !text.contains("PLACEHOLDER"))
        return false;
    static const QRegularExpression array(R"(blocks\s*=\s*\[[\s\S]*?\n\])");
    const auto found = array.match(text);
    if (!found.hasMatch())
        return false;
    // Blackbaud only knows the blocks this teacher teaches: update those, keep
    // the rest of the day as it was.
    QMap<int, QPair<QString, QString>> times;
    static const QRegularExpression row(R"re(number\s*=\s*(\d+)\s*,\s*start\s*=\s*"([^"]*)"\s*,\s*end\s*=\s*"([^"]*)")re");
    for (auto it = row.globalMatch(found.captured(0)); it.hasNext();) {
        const auto m = it.next();
        times.insert(m.captured(1).toInt(), {m.captured(2), m.captured(3)});
    }
    const int before = times.size();
    QStringList known;
    for (const auto &b : blocks) {
        times.insert(b.number, {b.start.toString("HH:mm"), b.end.toString("HH:mm")});
        known << QString::number(b.number);
    }
    QString rows;
    for (auto it = times.constBegin(); it != times.constEnd(); ++it)
        rows += QStringLiteral("  { number = %1, start = \"%2\", end = \"%3\" },\n").arg(it.key()).arg(it.value().first, it.value().second);
    text.replace(found.capturedStart(), found.capturedLength(), "blocks = [\n" + rows + "]");
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const bool allKnown = blocks.size() >= qMax(before, 1) && times.size() == blocks.size();
    static const QRegularExpression note(R"(# Bell schedule\.[^\n]*PLACEHOLDER[^\n]*\n(?:# times: replace[^\n]*\n)?)");
    text.replace(note, allKnown ? QStringLiteral("# Bell schedule, from Blackbaud on %1.\n").arg(today)
                                : QStringLiteral("# Bell schedule. Blocks %1 from Blackbaud on %2; the others are PLACEHOLDER\n"
                                                 "# times: replace them with your school's real ones.\n")
                                      .arg(known.join(", "), today));
    QString error;
    if (!writeFile(schedulePath, text, &error))
        return false;
    *report = QStringLiteral("Bell times for %1 blocks set from Blackbaud").arg(blocks.size());
    return true;
}

QString gitRepoAbove(const QString &path) {
    QDir dir(path);
    while (!dir.exists() && dir.cdUp()) {
    }
    for (;;) {
        if (QFileInfo::exists(dir.filePath(".git")))
            return dir.absolutePath();
        if (!dir.cdUp())
            return {};
    }
}

} // namespace Importer
