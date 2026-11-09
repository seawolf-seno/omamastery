#include "classbook.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <toml++/toml.hpp>

namespace {

QString str(const toml::node_view<const toml::node> &node) {
    return QString::fromStdString(node.value_or(std::string()));
}

bool parseToml(const QString &path, toml::table *out, QString *error) {
    if (!QFileInfo::exists(path))
        return false;
    try {
        *out = toml::parse_file(path.toStdString());
        return true;
    } catch (const toml::parse_error &e) {
        const auto &source = e.source().begin;
        *error = QStringLiteral("%1, line %2: %3")
                     .arg(QFileInfo(path).fileName())
                     .arg(source.line)
                     .arg(QString::fromUtf8(e.description().data(), int(e.description().size())));
        return false;
    }
}

} // namespace

Classbook::Classbook(const QString &root) : m_root(root.isEmpty() ? defaultRoot() : root) {}

QString Classbook::defaultRoot() {
    return QDir::homePath() + "/Classes";
}

bool Classbook::exists() const {
    return QFileInfo::exists(schedulePath());
}

const QStringList &Classbook::levels() {
    static const QStringList names{"Not yet", "Beginning", "Developing", "Proficient", "Extending"};
    return names;
}

QVector<ClassInfo> Classbook::loadClasses() const {
    QVector<ClassInfo> classes;
    const QDir root(m_root);
    const auto entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &entry : entries) {
        if (!QFileInfo::exists(entry.filePath() + "/class.toml"))
            continue;
        classes.append(loadClass(entry.filePath()));
    }
    // A days before B days, then by block, so the list reads like the week.
    std::stable_sort(classes.begin(), classes.end(), [](const ClassInfo &a, const ClassInfo &b) {
        if (a.day != b.day)
            return a.day < b.day;
        return a.block < b.block;
    });
    return classes;
}

ClassInfo Classbook::loadClass(const QString &dir) {
    ClassInfo info;
    info.dir = dir;
    info.id = QFileInfo(dir).fileName();
    info.name = info.id;

    toml::table table;
    QString error;
    if (parseToml(dir + "/class.toml", &table, &error)) {
        const toml::table &root = table;
        info.name = str(root["name"]).trimmed();
        if (info.name.isEmpty())
            info.name = info.id;
        info.course = str(root["course"]);
        info.day = str(root["day"]).trimmed().toUpper();
        info.block = int(root["block"].value_or(int64_t(0)));
        info.room = str(root["room"]);
        info.sample = root["sample"].value_or(false);
        if (const auto *students = root["students"].as_array()) {
            for (const auto &student : *students) {
                const QString name = QString::fromStdString(student.value_or(std::string())).trimmed();
                if (!name.isEmpty() && !info.students.contains(name))
                    info.students.append(name);
            }
        }
    } else if (!error.isEmpty()) {
        info.error = error;
    }

    // A real class keeps its students in roster.toml, apart from the files an
    // agent may read.
    toml::table roster;
    if (parseToml(dir + "/roster.toml", &roster, &error)) {
        if (const auto *students = roster["student"].as_array()) {
            for (const auto &item : *students) {
                const auto *entry = item.as_table();
                if (!entry)
                    continue;
                const QString name = str((*entry)["name"]).trimmed();
                if (!name.isEmpty() && !info.students.contains(name))
                    info.students.append(name);
            }
        }
    } else if (!error.isEmpty() && info.error.isEmpty()) {
        info.error = error;
    }

    toml::table plan;
    if (parseToml(dir + "/plan.toml", &plan, &error)) {
        if (const auto *objectives = plan["objective"].as_array()) {
            for (const auto &item : *objectives) {
                const auto *entry = item.as_table();
                if (!entry)
                    continue;
                const toml::table &row = *entry;
                Objective objective{str(row["id"]).trimmed(), str(row["title"]).trimmed(), str(row["idea"]).trimmed(),
                                    weekStart(QDate::fromString(str(row["week"]).trimmed(), Qt::ISODate))};
                if (objective.id.isEmpty() || objective.title.isEmpty())
                    continue;
                info.objectives.append(objective);
            }
        }
        // Undated objectives sort last: the plan is still being drafted.
        std::stable_sort(info.objectives.begin(), info.objectives.end(), [](const Objective &a, const Objective &b) {
            if (a.week.isValid() != b.week.isValid())
                return a.week.isValid();
            return a.week < b.week;
        });
    } else if (!error.isEmpty() && info.error.isEmpty()) {
        info.error = error;
    }

    toml::table ideas;
    if (parseToml(dir + "/ideas.toml", &ideas, &error)) {
        if (const auto *list = ideas["idea"].as_array()) {
            for (const auto &item : *list) {
                const auto *entry = item.as_table();
                if (!entry)
                    continue;
                const toml::table &row = *entry;
                CoreIdea idea{str(row["name"]).trimmed(), str(row["description"]).trimmed(), str(row["joint"]).trimmed()};
                if (!idea.name.isEmpty())
                    info.ideas.append(idea);
            }
        }
    } else if (!error.isEmpty() && info.error.isEmpty()) {
        info.error = error;
    }
    return info;
}

QString Classbook::masteryKey(const QString &student, const QString &objective) {
    return student + QChar(0x1f) + objective;
}

QHash<QString, QString> Classbook::loadMastery(const QString &csvPath) {
    QHash<QString, QString> levels;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly))
        return levels;
    const auto rows = parseCsv(QString::fromUtf8(file.readAll()));
    for (int i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        if (row.size() < 4 || (i == 0 && row[0] == "date"))
            continue;
        const QString level = row[3].trimmed();
        if (!Classbook::levels().contains(level))
            continue;
        levels.insert(masteryKey(row[1].trimmed(), row[2].trimmed()), level);
    }
    return levels;
}

QHash<QString, QString> Classbook::loadMasteryNotes(const QString &csvPath) {
    QHash<QString, QString> notes;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly))
        return notes;
    const auto rows = parseCsv(QString::fromUtf8(file.readAll()));
    for (int i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        if (row.size() < 4 || (i == 0 && row[0] == "date") || !levels().contains(row[3].trimmed()))
            continue;
        const QDate date = QDate::fromString(row[0].trimmed(), Qt::ISODate);
        const QString evidence = row.value(4).trimmed();
        QStringList parts;
        if (!evidence.isEmpty())
            parts << evidence;
        if (date.isValid())
            parts << date.toString("MMM d");
        notes.insert(masteryKey(row[1].trimmed(), row[2].trimmed()), parts.join(" · "));
    }
    return notes;
}

bool Classbook::appendMastery(const QString &csvPath, const QDate &date, const QString &student,
                              const QString &objective, const QString &level, const QString &evidence,
                              QString *error) {
    return appendMasteryRows(csvPath, date, {{student, objective, level, evidence}}, error);
}

bool Classbook::appendMasteryRows(const QString &csvPath, const QDate &date, const QVector<MasteryRow> &rows,
                                  QString *error) {
    for (const auto &row : rows) {
        if (!levels().contains(row.level)) {
            if (error)
                *error = QStringLiteral("Unknown level: %1").arg(row.level);
            return false;
        }
    }
    QByteArray contents;
    QFile existing(csvPath);
    if (existing.open(QIODevice::ReadOnly)) {
        contents = existing.readAll();
        existing.close();
    }
    if (contents.isEmpty())
        contents = "date,student,objective,level,evidence\n";
    if (!contents.endsWith('\n'))
        contents.append('\n');
    for (const auto &row : rows) {
        const QStringList fields{date.toString(Qt::ISODate), row.student, row.objective, row.level, row.evidence};
        QStringList quoted;
        for (const auto &field : fields)
            quoted.append(csvField(field));
        contents.append(quoted.join(',').toUtf8());
        contents.append('\n');
    }

    // Written beside the target and renamed, so a crash never leaves half a log.
    QSaveFile file(csvPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Couldn't save %1").arg(csvPath);
        return false;
    }
    return true;
}

QDate Classbook::weekStart(const QDate &date) {
    return date.isValid() ? date.addDays(1 - date.dayOfWeek()) : QDate();
}

NowInfo Classbook::now(const QVector<ClassInfo> &classes, const Schedule &schedule, const QDateTime &when) {
    NowInfo info;
    const DayInfo day = schedule.day(when.date());
    if (day.kind == DayInfo::Weekend) {
        info.text = QStringLiteral("Weekend");
        return info;
    }
    if (day.kind == DayInfo::OutOfYear) {
        info.text = QStringLiteral("No classes");
        return info;
    }
    if (day.kind == DayInfo::Off) {
        info.text = day.label.isEmpty() ? QStringLiteral("No classes today") : day.label;
        return info;
    }
    if (day.type.isEmpty()) {
        info.text = QStringLiteral("Not sure if today is an A or B day");
        return info;
    }

    auto classInBlock = [&](int block) {
        for (int i = 0; i < classes.size(); ++i)
            if (classes[i].day == day.type && classes[i].block == block)
                return i;
        return -1;
    };

    const QTime time = when.time();
    const int current = schedule.blockAt(time);
    if (current > 0 && classInBlock(current) >= 0) {
        info.status = NowInfo::Now;
        info.index = classInBlock(current);
        info.text = QStringLiteral("%1 day · Block %2 · now").arg(day.type).arg(current);
        return info;
    }
    // Walk forward through today's remaining blocks to the next one this
    // teacher actually teaches; a free block is skipped.
    QTime cursor = time;
    for (int next = schedule.nextBlockAfter(cursor); next > 0; next = schedule.nextBlockAfter(cursor)) {
        const int index = classInBlock(next);
        if (index >= 0) {
            info.status = NowInfo::Next;
            info.index = index;
            const Block *block = schedule.block(next);
            info.text = QStringLiteral("%1 day · Block %2 · next at %3")
                            .arg(day.type)
                            .arg(next)
                            .arg(block ? block->start.toString("h:mm") : QString());
            return info;
        }
        const Block *block = schedule.block(next);
        if (!block)
            break;
        cursor = block->start;
    }
    info.text = QStringLiteral("%1 day · done for today").arg(day.type);
    return info;
}

QVector<QStringList> Classbook::parseCsv(const QString &text) {
    QVector<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool fieldStarted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field.append('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.append(c);
            }
            continue;
        }
        if (c == '"' && field.isEmpty()) {
            quoted = true;
            fieldStarted = true;
        } else if (c == ',') {
            row.append(field);
            field.clear();
            fieldStarted = true;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            if (fieldStarted || !field.isEmpty() || !row.isEmpty()) {
                row.append(field);
                rows.append(row);
            }
            row.clear();
            field.clear();
            fieldStarted = false;
        } else {
            field.append(c);
            fieldStarted = true;
        }
    }
    if (fieldStarted || !field.isEmpty() || !row.isEmpty()) {
        row.append(field);
        rows.append(row);
    }
    return rows;
}

QString Classbook::csvField(const QString &value) {
    QString clean = value;
    clean.replace('\r', ' ').replace('\n', ' ');
    if (clean.contains(',') || clean.contains('"')) {
        clean.replace("\"", "\"\"");
        return '"' + clean + '"';
    }
    return clean;
}
