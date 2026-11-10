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

#include "../classbook.h"

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

QVector<ClassDraft> draftsFromCanvasCourses(const QJsonArray &courses, const QDate &today) {
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
        const QJsonObject term = c.value("term").toObject();
        d.term = text(term, {"name"});
        d.termEnd = localDate(text(term, {"end_at"}));
        if (d.selected && d.termEnd.isValid() && d.termEnd < today) {
            d.selected = false;
            d.note = QStringLiteral("term over");
        }
        // A TA period or advisory isn't a class to grade for mastery.
        static const QRegularExpression assistant(R"(\bteacher'?s?\s+assistant\b|\bteaching\s+assistant\b)",
                                                  QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression advisory(R"(\badvisory\b)", QRegularExpression::CaseInsensitiveOption);
        if (assistant.match(d.name).hasMatch()) {
            d.selected = false;
            d.note = QStringLiteral("Teacher's Assistant");
        } else if (advisory.match(d.name).hasMatch()) {
            d.selected = false;
            d.note = QStringLiteral("Advisory");
        }
        drafts.append(d);
    }
    // One course per semester: of the same-named ticked courses, keep the one
    // whose term ends first (the one under way) and untick the later ones.
    QHash<QString, int> current;
    for (int i = 0; i < drafts.size(); ++i) {
        const auto &d = drafts[i];
        if (!d.selected || !d.termEnd.isValid())
            continue;
        const QString key = d.name.simplified().toLower();
        const auto it = current.find(key);
        if (it == current.end() || d.termEnd < drafts[*it].termEnd)
            current.insert(key, i);
    }
    for (int i = 0; i < drafts.size(); ++i) {
        auto &d = drafts[i];
        const auto it = current.constFind(d.name.simplified().toLower());
        if (!d.selected || !d.termEnd.isValid() || it == current.constEnd() || *it == i || d.termEnd <= drafts[*it].termEnd)
            continue;
        d.selected = false;
        static const QRegularExpression semester(R"(\b(\d\w*\s+(?:semester|trimester|quarter|term))\b)", QRegularExpression::CaseInsensitiveOption);
        const auto m = semester.match(d.term);
        d.note = m.hasMatch() ? m.captured(1) : QStringLiteral("later term");
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

bool isNonMasteryGroup(const QString &name) {
    static const QRegularExpression nonMastery(R"(^\s*non[\s_-]*mastery\b)", QRegularExpression::CaseInsensitiveOption);
    return nonMastery.match(name).hasMatch();
}

QJsonArray assignmentsIn(const QJsonArray &groups) {
    QJsonArray all;
    for (const auto &group : groups)
        for (const auto &a : group.toObject().value("assignments").toArray())
            all.append(a);
    return all;
}

void objectivesFromAssignmentGroups(const QJsonArray &groups, ClassDraft *draft) {
    struct Row {
        DraftObjective objective;
        int group = 0;
        int position = 0;
    };
    QVector<Row> rows;
    for (const auto &groupValue : groups) {
        const QJsonObject group = groupValue.toObject();
        if (isNonMasteryGroup(text(group, {"name"})))
            continue;
        const int groupPosition = group.value("position").toInt();
        for (const auto &value : group.value("assignments").toArray()) {
            const QJsonObject a = value.toObject();
            const QString name = text(a, {"name"});
            if (name.isEmpty())
                continue;
            QDate when = localDate(text(a, {"due_at"}));
            if (!when.isValid())
                when = localDate(text(a, {"unlock_at"}));
            rows.append({{QString(), name, QString(), monday(when), text(a, {"id"}), a.value("points_possible").toDouble(),
                          text(group, {"name"}).simplified()},
                         groupPosition, a.value("position").toInt()});
        }
    }
    // In teaching order: by the week it's due, undated ones last.
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.objective.week.isValid() != b.objective.week.isValid())
            return a.objective.week.isValid();
        if (a.objective.week != b.objective.week)
            return a.objective.week < b.objective.week;
        return a.group != b.group ? a.group < b.group : a.position < b.position;
    });
    QSet<QString> taken;
    for (auto &row : rows) {
        row.objective.id = uniqueId(slug(row.objective.title), &taken);
        draft->objectives.append(row.objective);
    }
    if (!rows.isEmpty())
        draft->planSource = QStringLiteral("Canvas assignments");
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
        draft->objectives.append({uniqueId(slug(title.isEmpty() ? shown : title), &taken), shown, group, QDate(), QString(), 0, QString()});
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
            draft->objectives.append({uniqueId(slug(name), &taken), name, QString(), week, QString(), 0, QString()});
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

// ---------------------------------------------------------------- starting levels

QVector<Score> scoresFromCanvas(const QJsonArray &submissions) {
    QVector<Score> out;
    for (const auto &value : submissions) {
        const QJsonObject s = value.toObject();
        Score score;
        score.canvasUser = text(s, {"user_id"});
        score.assignment = text(s, {"assignment_id"});
        score.score = s.value("score").isDouble() ? s.value("score").toDouble() : -1;
        score.grade = text(s, {"grade", "entered_grade"});
        score.graded = localDate(text(s, {"graded_at"}));
        if (score.canvasUser.isEmpty() || score.assignment.isEmpty() || (score.score < 0 && score.grade.isEmpty()))
            continue;
        out.append(score);
    }
    return out;
}

ScoreLevels loadScoreLevels(const QString &schedulePath) {
    ScoreLevels levels;
    try {
        const toml::table t = toml::parse_file(schedulePath.toStdString());
        if (const auto *cut = t["score_levels"].as_table()) {
            auto read = [&](const char *key, double *out) {
                if (auto v = (*cut)[key].value<double>())
                    *out = *v;
            };
            read("Extending", &levels.extending);
            read("Proficient", &levels.proficient);
            read("Developing", &levels.developing);
            read("Beginning", &levels.beginning);
        }
    } catch (const toml::parse_error &) {
    }
    return levels;
}

QString levelForScore(const Score &score, double points, const ScoreLevels &levels) {
    for (const QString &level : Classbook::levels())
        if (score.grade.trimmed().compare(level, Qt::CaseInsensitive) == 0)
            return level;
    double percent = -1;
    if (score.score >= 0 && points > 0)
        percent = 100.0 * score.score / points;
    else if (score.grade.trimmed().endsWith('%'))
        percent = score.grade.trimmed().chopped(1).toDouble();
    if (percent < 0)
        return {};
    // A hair of slack for Canvas's rounding (9.99 of 10).
    percent += 1e-6;
    if (percent >= levels.extending)
        return QStringLiteral("Extending");
    if (percent >= levels.proficient)
        return QStringLiteral("Proficient");
    if (percent >= levels.developing)
        return QStringLiteral("Developing");
    if (percent >= levels.beginning)
        return QStringLiteral("Beginning");
    return QStringLiteral("Not yet");
}

int addNewObjectives(const QString &classDir, const ClassDraft &draft, QString *error) {
    const QString path = classDir + "/plan.toml";
    QString text = readFile(path);
    if (draft.objectives.isEmpty() || text.isEmpty())
        return 0;
    QSet<QString> ids, assignments, titles;
    try {
        const toml::table plan = toml::parse(text.toStdString());
        if (const auto *list = plan["objective"].as_array())
            for (const auto &item : *list)
                if (const auto *row = item.as_table()) {
                    ids.insert(QString::fromStdString((*row)["id"].value_or(std::string())));
                    assignments.insert(QString::fromStdString((*row)["canvas_assignment"].value_or(std::string())));
                    titles.insert(QString::fromStdString((*row)["title"].value_or(std::string())).simplified().toLower());
                }
    } catch (const toml::parse_error &) {
        return 0; // never rewrite a file we can't read
    }
    assignments.remove(QString());
    QString added;
    int count = 0;
    for (const auto &o : draft.objectives) {
        if ((!o.canvasAssignment.isEmpty() && assignments.contains(o.canvasAssignment)) ||
            titles.contains(o.title.simplified().toLower()))
            continue;
        const QString id = uniqueId(o.id.isEmpty() ? slug(o.title) : o.id, &ids);
        titles.insert(o.title.simplified().toLower());
        added += QStringLiteral("\n[[objective]]\nid = %1\ntitle = %2\nidea = %3\n")
                     .arg(tomlString(id), tomlString(o.title), tomlString(o.idea));
        if (!o.group.isEmpty())
            added += QStringLiteral("group = %1\n").arg(tomlString(o.group));
        if (o.week.isValid())
            added += QStringLiteral("week = %1\n").arg(tomlString(o.week.toString(Qt::ISODate)));
        if (!o.canvasAssignment.isEmpty())
            added += QStringLiteral("canvas_assignment = %1\n").arg(tomlString(o.canvasAssignment));
        ++count;
    }
    if (count == 0)
        return 0;
    if (!text.endsWith('\n'))
        text += '\n';
    if (!writeFile(path, text + added, error))
        return -1;
    return count;
}

int addGroups(const QString &classDir, const ClassDraft &draft, QString *error) {
    QHash<QString, QString> byAssignment, byTitle;
    for (const auto &o : draft.objectives) {
        if (o.group.isEmpty())
            continue;
        if (!o.canvasAssignment.isEmpty())
            byAssignment.insert(o.canvasAssignment, o.group);
        byTitle.insert(o.title.simplified().toLower(), o.group);
    }
    const QString path = classDir + "/plan.toml";
    const QString text = readFile(path);
    if (byTitle.isEmpty() || text.isEmpty())
        return 0;
    try {
        (void)toml::parse(text.toStdString()); // never rewrite a file we can't read
    } catch (const toml::parse_error &) {
        return 0;
    }
    // Each [[objective]] block, up to the next table header.
    static const QRegularExpression header(R"(^\[\[?[^\n]*$)", QRegularExpression::MultilineOption);
    static const QRegularExpression titleLine(R"re(^title\s*=\s*"((?:[^"\\]|\\.)*)"[ \t]*$)re", QRegularExpression::MultilineOption);
    static const QRegularExpression assignmentLine(R"re(^canvas_assignment\s*=\s*"([^"]*)")re", QRegularExpression::MultilineOption);
    static const QRegularExpression groupLine(R"(^group\s*=)", QRegularExpression::MultilineOption);
    QVector<int> starts;
    for (auto it = header.globalMatch(text); it.hasNext();)
        starts << it.next().capturedStart();
    starts << text.size();
    QString out = text.left(starts.value(0));
    int added = 0;
    for (int i = 0; i + 1 < starts.size(); ++i) {
        QString block = text.mid(starts[i], starts[i + 1] - starts[i]);
        if (block.startsWith("[[objective]]") && !groupLine.match(block).hasMatch()) {
            const auto title = titleLine.match(block);
            QString group = byAssignment.value(assignmentLine.match(block).captured(1));
            if (group.isEmpty() && title.hasMatch())
                group = byTitle.value(title.captured(1).replace("\\\"", "\"").simplified().toLower());
            if (!group.isEmpty() && title.hasMatch()) {
                block.insert(title.capturedEnd(), "\ngroup = " + tomlString(group));
                ++added;
            }
        }
        out += block;
    }
    if (added > 0 && !writeFile(path, out, error))
        return -1;
    return added;
}

int seedLevels(const QString &classDir, const ClassDraft &draft, const ScoreLevels &levels, QString *error) {
    if (draft.scores.isEmpty())
        return 0;
    // Which objective each Canvas assignment is, from plan.toml as it is now
    // (the teacher may have renamed or re-dated objectives since setup).
    // Plans written before objectives kept their assignment id (0.2.0) match
    // by title instead: the title is the assignment's name.
    QHash<QString, QString> assignmentByTitle;
    for (const auto &o : draft.objectives)
        if (!o.canvasAssignment.isEmpty())
            assignmentByTitle.insert(o.title.simplified().toLower(), o.canvasAssignment);
    QHash<QString, QString> objectiveOf;
    try {
        const toml::table plan = toml::parse_file((classDir + "/plan.toml").toStdString());
        if (const auto *list = plan["objective"].as_array())
            for (const auto &item : *list)
                if (const auto *row = item.as_table()) {
                    QString assignment = QString::fromStdString((*row)["canvas_assignment"].value_or(std::string()));
                    if (assignment.isEmpty())
                        assignment = assignmentByTitle.value(
                            QString::fromStdString((*row)["title"].value_or(std::string())).simplified().toLower());
                    if (!assignment.isEmpty() && !objectiveOf.contains(assignment))
                        objectiveOf.insert(assignment, QString::fromStdString((*row)["id"].value_or(std::string())));
                }
    } catch (const toml::parse_error &) {
        return 0;
    }
    QHash<QString, double> points;
    for (const auto &o : draft.objectives)
        if (!o.canvasAssignment.isEmpty())
            points.insert(o.canvasAssignment, o.points);
    // Students by Canvas id, under the names in roster.toml (which keeps renames).
    QHash<QString, QString> studentOf;
    try {
        const toml::table roster = toml::parse_file((classDir + "/roster.toml").toStdString());
        if (const auto *list = roster["student"].as_array())
            for (const auto &item : *list)
                if (const auto *row = item.as_table()) {
                    const QString id = QString::fromStdString((*row)["canvas_id"].value_or(std::string()));
                    if (!id.isEmpty())
                        studentOf.insert(id, QString::fromStdString((*row)["name"].value_or(std::string())));
                }
    } catch (const toml::parse_error &) {
        return 0;
    }

    const QString csvPath = classDir + "/mastery.csv";
    const auto existing = Classbook::loadMastery(csvPath);
    QFile in(csvPath);
    QByteArray text = in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
    in.close();
    if (text.isEmpty())
        text = "date,student,objective,level,evidence\n";
    if (!text.endsWith('\n'))
        text += '\n';
    int written = 0;
    QSet<QString> done;
    for (const auto &score : draft.scores) {
        const QString objective = objectiveOf.value(score.assignment);
        const QString student = studentOf.value(score.canvasUser);
        if (objective.isEmpty() || student.isEmpty())
            continue;
        const QString key = Classbook::masteryKey(student, objective);
        if (existing.contains(key) || done.contains(key))
            continue; // never over the teacher's own level
        const double possible = points.value(score.assignment);
        const QString level = levelForScore(score, possible, levels);
        if (level.isEmpty())
            continue;
        QString evidence = QStringLiteral("From Canvas: ");
        if (score.score >= 0 && possible > 0)
            evidence += QStringLiteral("%1 of %2").arg(score.score).arg(possible);
        else
            evidence += score.grade;
        QStringList fields{(score.graded.isValid() ? score.graded : QDate::currentDate()).toString(Qt::ISODate), student, objective,
                           level, evidence};
        for (auto &f : fields)
            f = Classbook::csvField(f);
        text += fields.join(',').toUtf8() + '\n';
        done.insert(key);
        ++written;
    }
    if (written > 0 && !writeFile(csvPath, QString::fromUtf8(text), error, true))
        return -1;
    return written;
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

void assignIds(const QString &root, QVector<ClassDraft> *drafts, const QVector<ClassDraft> &listed) {
    QSet<QString> taken;
    // Folders already set up from these classes keep their ids.
    for (auto &d : *drafts) {
        d.id = existingFolder(root, d);
        if (!d.id.isEmpty())
            taken.insert(d.id);
    }
    // Next semester's course: take over the folder of the same class from an
    // earlier term. Never another section of the same course this term.
    const QVector<ClassDraft> &all = listed.isEmpty() ? *drafts : listed;
    const QDir dir(root);
    for (auto &d : *drafts) {
        if (!d.id.isEmpty() || d.canvasCourse.isEmpty())
            continue;
        for (const auto &entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (taken.contains(entry.fileName()))
                continue;
            QString name, course;
            try {
                const toml::table t = toml::parse_file((entry.filePath() + "/class.toml").toStdString());
                if (t["sample"].value_or(false))
                    continue;
                name = QString::fromStdString(t["name"].value_or(std::string()));
                course = QString::fromStdString(t["canvas_course"].value_or(std::string()));
            } catch (const toml::parse_error &) {
                continue;
            }
            if (course.isEmpty() || course == d.canvasCourse || name.simplified().toLower() != d.name.simplified().toLower())
                continue;
            const ClassDraft *earlier = nullptr;
            for (const auto &other : all)
                if (other.canvasCourse == course)
                    earlier = &other;
            const bool ended = earlier == nullptr ||
                               (earlier->termEnd.isValid() && d.termEnd.isValid() && earlier->termEnd < d.termEnd);
            if (!ended)
                continue;
            d.id = entry.fileName();
            d.continues = course;
            taken.insert(d.id);
            break;
        }
    }
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
        "# `omamastery setup`, which keeps the names you use. Names are how mastery.csv\n"
        "# knows students, so rename someone before recording levels, not after.\n");
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
    else if (d.planSource == "Canvas assignments")
        out += QStringLiteral("#\n# From your Canvas assignments (all but \"Non-Mastery Assignments\"), in the week\n"
                              "# each is due. Your agent can tie each to a core idea (\"Draft objectives\").\n");
    else if (d.planSource == "Canvas Outcomes")
        out += QStringLiteral("#\n# From your Canvas Outcomes. Undated ones wait for \"Space across the semester\".\n");
    else
        out += QStringLiteral("#\n# Empty for now: add your long-range plan to this folder, then press\n# \"Draft objectives\" and your agent will fill this in.\n");
    for (const auto &o : d.objectives) {
        out += QStringLiteral("\n[[objective]]\nid = %1\ntitle = %2\nidea = %3\n")
                   .arg(tomlString(o.id), tomlString(o.title), tomlString(o.idea));
        if (!o.group.isEmpty())
            out += QStringLiteral("group = %1\n").arg(tomlString(o.group));
        if (o.week.isValid())
            out += QStringLiteral("week = %1\n").arg(tomlString(o.week.toString(Qt::ISODate)));
        if (!o.canvasAssignment.isEmpty())
            out += QStringLiteral("canvas_assignment = %1\n").arg(tomlString(o.canvasAssignment));
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
        // A new semester's course: point the folder at it, and remember the old one.
        if (!d.continues.isEmpty() && !d.canvasCourse.isEmpty()) {
            static const QRegularExpression courseLine(R"(^canvas_course\s*=.*$)", QRegularExpression::MultilineOption);
            static const QRegularExpression earlierLine(R"(^earlier_canvas_courses\s*=\s*\[(.*)\]\s*$)", QRegularExpression::MultilineOption);
            text.replace(courseLine, "canvas_course = " + tomlString(d.canvasCourse));
            const auto m = earlierLine.match(text);
            if (m.hasMatch()) {
                if (!m.captured(1).contains(tomlString(d.continues)))
                    text.replace(m.capturedStart(), m.capturedLength(),
                                 QStringLiteral("earlier_canvas_courses = [%1, %2]").arg(m.captured(1).trimmed(), tomlString(d.continues)));
            } else {
                if (!text.endsWith('\n'))
                    text += '\n';
                text += QStringLiteral("earlier_canvas_courses = [%1] # earlier semesters of this class\n").arg(tomlString(d.continues));
            }
        }
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
        *report = d.continues.isEmpty()
            ? QStringLiteral("%1: roster refreshed, %2 students (plan and mastery kept)").arg(d.id).arg(d.students.size())
            : QStringLiteral("%1: now follows %2 in Canvas, %3 students (plan and mastery kept)")
                  .arg(d.id, d.term.isEmpty() ? QStringLiteral("the new term's course") : d.term)
                  .arg(d.students.size());
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
