#include "school.h"

#include <toml++/toml.hpp>

namespace {

QString str(const toml::node_view<const toml::node> &node) {
    return QString::fromStdString(node.value_or(std::string()));
}

QDate parseDate(const QString &text) {
    return QDate::fromString(text.trimmed(), Qt::ISODate);
}

QTime parseTime(const QString &text) {
    return QTime::fromString(text.trimmed(), "HH:mm");
}

// "Mon".."Sun" (any case, any length from three letters) to Qt's 1..7.
int weekdayNumber(const QString &name) {
    static const QStringList days{"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    const QString key = name.trimmed().left(3).toLower();
    const int index = days.indexOf(key);
    return index < 0 ? 0 : index + 1;
}

QSet<int> weekdays(const toml::node_view<const toml::node> &node, const QSet<int> &fallback) {
    const auto *array = node.as_array();
    if (!array)
        return fallback;
    QSet<int> out;
    for (const auto &item : *array) {
        const int day = weekdayNumber(QString::fromStdString(item.value_or(std::string())));
        if (day > 0)
            out.insert(day);
    }
    return out;
}

} // namespace

bool Schedule::load(const QString &path, QString *error) {
    toml::table table;
    try {
        table = toml::parse_file(path.toStdString());
    } catch (const toml::parse_error &e) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, QString::fromUtf8(e.description().data(), int(e.description().size())));
        return false;
    }
    const toml::table &root = table;

    m_firstDay = parseDate(str(root["first_day"]));
    m_lastDay = parseDate(str(root["last_day"]));
    m_aWeekdays = weekdays(root["a_days"], {1, 3});
    m_bWeekdays = weekdays(root["b_days"], {2, 4});

    m_fridays.clear();
    if (const auto *fridays = root["fridays"].as_table()) {
        for (const auto &[key, value] : *fridays) {
            const QDate date = parseDate(QString::fromStdString(std::string(key.str())));
            const QString type = QString::fromStdString(value.value_or(std::string())).trimmed().toUpper();
            if (date.isValid() && (type == "A" || type == "B"))
                m_fridays.insert(date, type);
        }
    }

    m_off.clear();
    if (const auto *off = root["off"].as_array()) {
        for (const auto &item : *off) {
            const auto *entry = item.as_table();
            if (!entry)
                continue;
            const toml::table &row = *entry;
            OffRange range{parseDate(str(row["from"])), parseDate(str(row["to"])), str(row["label"])};
            if (!range.to.isValid())
                range.to = range.from;
            if (range.from.isValid())
                m_off.append(range);
        }
    }

    m_blocks.clear();
    if (const auto *blocks = root["blocks"].as_array()) {
        for (const auto &item : *blocks) {
            const auto *entry = item.as_table();
            if (!entry)
                continue;
            const toml::table &row = *entry;
            Block block{int(row["number"].value_or(int64_t(0))), parseTime(str(row["start"])), parseTime(str(row["end"]))};
            if (block.number > 0 && block.start.isValid() && block.end.isValid() && block.start < block.end)
                m_blocks.append(block);
        }
    }
    std::sort(m_blocks.begin(), m_blocks.end(), [](const Block &a, const Block &b) { return a.start < b.start; });
    return true;
}

DayInfo Schedule::day(const QDate &date) const {
    DayInfo info;
    const int weekday = date.dayOfWeek();
    if (weekday >= 6) {
        info.kind = DayInfo::Weekend;
        return info;
    }
    if ((m_firstDay.isValid() && date < m_firstDay) || (m_lastDay.isValid() && date > m_lastDay)) {
        info.kind = DayInfo::OutOfYear;
        return info;
    }
    for (const auto &range : m_off) {
        if (date >= range.from && date <= range.to) {
            info.kind = DayInfo::Off;
            info.label = range.label;
            return info;
        }
    }
    if (m_aWeekdays.contains(weekday))
        info.type = "A";
    else if (m_bWeekdays.contains(weekday))
        info.type = "B";
    else
        info.type = m_fridays.value(date);
    // A Friday missing from the list has no known rotation: treat it as a
    // school day without a type rather than guessing.
    info.kind = DayInfo::School;
    return info;
}

int Schedule::blockAt(const QTime &time) const {
    for (const auto &block : m_blocks)
        if (time >= block.start && time < block.end)
            return block.number;
    return 0;
}

int Schedule::nextBlockAfter(const QTime &time) const {
    for (const auto &block : m_blocks)
        if (block.start > time)
            return block.number;
    return 0;
}

const Block *Schedule::block(int number) const {
    for (const auto &block : m_blocks)
        if (block.number == number)
            return &block;
    return nullptr;
}
