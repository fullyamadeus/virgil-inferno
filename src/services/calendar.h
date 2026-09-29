#pragma once
#include "domain/records.h"
#include <QTimeZone>

namespace virgil::calendar {
// Pure calendar calculations: no widgets, files or SQL.
QTimeZone timeZone(const QString& id);
QDateTime localToUtc(QDate date, QTime time, const QString& zone, const QString& ambiguity = "reject");
QDate mondayOf(QDate date);
bool recurringClassVisible(QDate date, const Semester& semester, const QList<ExamRange>& ranges);
QList<ExamRange> semesterRanges(const Snapshot& snapshot, const QString& semesterId);
struct Occurrence {
    QString eventId;
    QString classId;
    QDate date;
    QString title;
    QString kind;
    QString courseId;
    bool allDay = false;
    QDateTime startsAt;
    QDateTime endsAt;
    bool completed = false;
    QString warning;
};
QList<Occurrence> occurrences(const Snapshot& snapshot, QDate first, QDate last,
                             const QString& semesterId = {}, const QString& displayZone = {});
QList<CalendarEvent> upcoming(const Snapshot& snapshot, const QString& semesterId, QDateTime now, int limit = 6);
} // namespace virgil::calendar
