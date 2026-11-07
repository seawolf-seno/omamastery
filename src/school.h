#pragma once

#include <QDate>
#include <QMap>
#include <QSet>
#include <QString>
#include <QTime>
#include <QVector>

// One teaching block in the bell schedule.
struct Block {
    int number = 0;
    QTime start;
    QTime end;
};

// A stretch of days with no regular classes (a break, a holiday, finals).
struct OffRange {
    QDate from;
    QDate to;
    QString label;
};

// What kind of day a date is at school.
struct DayInfo {
    enum Kind { School, Weekend, Off, OutOfYear };
    Kind kind = OutOfYear;
    QString type;  // "A" or "B" on a school day, empty otherwise
    QString label; // why there are no classes, on an off day
};

// The school's A/B rotation and bell schedule, read from ~/Classes/schedule.toml.
// Monday and Wednesday are A days and Tuesday and Thursday B days by default;
// Fridays alternate and are listed one by one.
class Schedule {
public:
    bool load(const QString &path, QString *error = nullptr);

    DayInfo day(const QDate &date) const;
    // The block running at this time, or 0 between blocks.
    int blockAt(const QTime &time) const;
    // The next block to start after this time today, or 0 when the day is done.
    int nextBlockAfter(const QTime &time) const;
    const Block *block(int number) const;
    const QVector<Block> &blocks() const { return m_blocks; }

    QDate firstDay() const { return m_firstDay; }
    QDate lastDay() const { return m_lastDay; }

private:
    QDate m_firstDay;
    QDate m_lastDay;
    QSet<int> m_aWeekdays{1, 3};
    QSet<int> m_bWeekdays{2, 4};
    QMap<QDate, QString> m_fridays;
    QVector<OffRange> m_off;
    QVector<Block> m_blocks;
};
