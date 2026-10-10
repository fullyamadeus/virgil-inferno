#include "services/calendar.h"
#include "domain/error.h"
#include <algorithm>

namespace virgil::calendar {
QTimeZone timeZone(const QString& id)
{
    const QTimeZone zone(id.toUtf8());
    if (!zone.isValid()) throw Error("Unknown or unavailable timezone: " + id + ". Choose an installed IANA timezone.");
    return zone;
}

QDateTime localToUtc(QDate date, QTime time, const QString& zoneId, const QString& ambiguity)
{
    if (!date.isValid() || !time.isValid()) throw Error("Enter a valid local date and time.");
    if (!QStringList{"reject", "earlier", "later"}.contains(ambiguity)) throw Error("Choose an explicit repeated-time policy.");
    const auto zone = timeZone(zoneId);
    QList<QDateTime> candidates;
    for (const auto resolution : {QDateTime::TransitionResolution::RelativeToBefore, QDateTime::TransitionResolution::RelativeToAfter}) {
        const QDateTime candidate(date, time, zone, resolution);
        if (candidate.isValid() && candidate.date() == date && candidate.time() == time
            && !candidates.contains(candidate.toUTC())) candidates.append(candidate.toUTC());
    }
    if (candidates.isEmpty()) throw Error("This local time does not exist because of a timezone clock change. Choose another time; it was not shifted.");
    std::sort(candidates.begin(), candidates.end());
    if (candidates.size() > 1 && ambiguity == "reject")
        throw Error("This local time occurs twice because of a timezone clock change. Explicitly choose the earlier or later occurrence.");
    return ambiguity == "later" ? candidates.last() : candidates.first();
}

QDate mondayOf(QDate date) { return date.addDays(1 - date.dayOfWeek()); }

QList<ExamRange> semesterRanges(const Snapshot& snapshot, const QString& semesterId)
{
    QList<ExamRange> result;
    for (const auto& range : snapshot.examRanges) if (range.semesterId == semesterId) result.append(range);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return result;
}

bool recurringClassVisible(QDate date, const Semester& semester, const QList<ExamRange>& ranges)
{
    if (!date.isValid() || date < semester.start || date > semester.end) return false;
    if (semester.teachingStart.isValid() && semester.teachingEnd.isValid()) {
        if (date < semester.teachingStart || date > semester.teachingEnd) return false;
    } else {
        const bool summer = semester.season == "summer";
        const QDate start(date.year(), summer ? 2 : 10, summer ? 18 : 1);
        const QDate end(date.year(), summer ? 5 : 12, summer ? 31 : 31);
        if (date < start || date > end) return false;
    }
    QList<ExamRange> ordered;
    for (const auto& range : ranges) if (range.semesterId == semester.id) ordered.append(range);
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    if (ordered.size() >= 2 && date >= ordered[1].start) return false;
    for (const auto& range : ordered) if (date >= range.start && date <= range.end) return false;
    return true;
}

namespace {
bool visible(const QString& selected, const QString& recordSemester) { return selected.isEmpty() || recordSemester.isEmpty() || selected == recordSemester; }
void appendTimed(QList<Occurrence>& output, Occurrence entry, QDate first, QDate last, const QTimeZone& zone)
{
    const QDate begin = qMax(first, entry.startsAt.toTimeZone(zone).date());
    const QDate end = qMin(last, entry.endsAt.addMSecs(-1).toTimeZone(zone).date());
    for (QDate day = begin; day.isValid() && day <= end; day = day.addDays(1)) {
        entry.date = day; output.append(entry);
    }
}
}

QList<Occurrence> occurrences(const Snapshot& snapshot, QDate first, QDate last, const QString& semesterId, const QString& displayZone)
{
    if (!first.isValid() || !last.isValid() || last < first || first.daysTo(last) > 366)
        throw Error("Choose a calendar display range of at most 367 days.");
    const auto zone = timeZone(displayZone.isEmpty() ? snapshot.calendarTimeZone : displayZone);
    QList<Occurrence> result;
    for (const auto& event : snapshot.events) {
        if (!visible(semesterId, event.semesterId)) continue;
        Occurrence entry{event.id, {}, event.date, event.title, event.kind, event.courseId, event.allDay,
                         event.startsAt, event.endsAt, event.completed, {}, {}};
        if (event.allDay) { if (event.date >= first && event.date <= last) result.append(entry); }
        else appendTimed(result, entry, first, last, zone);
    }
    for (const auto& slot : snapshot.weeklyClasses) {
        if (!visible(semesterId, slot.semesterId)) continue;
        const Semester* semester = nullptr;
        for (const auto& candidate : snapshot.semesters) if (candidate.id == slot.semesterId) semester = &candidate;
        if (!semester) continue;
        for (QDate day = first.addDays(-2); day.isValid() && day <= last.addDays(2); day = day.addDays(1)) {
            if (day.dayOfWeek() != slot.weekday || !recurringClassVisible(day, *semester, snapshot.examRanges)) continue;
            Occurrence entry{{}, slot.id, day, slot.title, slot.kind, slot.courseId, false, {}, {}, false, {}, slot.room};
            try {
                entry.startsAt = localToUtc(day, slot.start, slot.timeZone, slot.ambiguity);
                entry.endsAt = localToUtc(day, slot.end, slot.timeZone, slot.ambiguity);
                if (entry.endsAt <= entry.startsAt) throw Error("The class ends before it starts across the clock change.");
                appendTimed(result, entry, first, last, zone);
            } catch (const Error& error) {
                // Keep a visible warning instead of silently shifting or dropping a class.
                if (day >= first && day <= last) { entry.warning = QString::fromUtf8(error.what()); result.append(entry); }
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.date != b.date) return a.date < b.date;
        if (a.allDay != b.allDay) return a.allDay;
        if (a.startsAt != b.startsAt) return a.startsAt < b.startsAt;
        return a.title < b.title;
    });
    return result;
}

QList<CalendarEvent> upcoming(const Snapshot& snapshot, const QString& semesterId, QDateTime now, int limit)
{
    const auto zone = timeZone(snapshot.calendarTimeZone);
    const QDate today = now.toTimeZone(zone).date();
    QList<CalendarEvent> result;
    for (const auto& event : snapshot.events)
        if (!event.completed && visible(semesterId, event.semesterId)
            && (event.allDay ? event.date >= today : event.startsAt >= now)) result.append(event);
    std::sort(result.begin(), result.end(), [&](const auto& a, const auto& b) {
        const QDate ad = a.allDay ? a.date : a.startsAt.toTimeZone(zone).date();
        const QDate bd = b.allDay ? b.date : b.startsAt.toTimeZone(zone).date();
        if (ad != bd) return ad < bd;
        if (a.allDay != b.allDay) return a.allDay;
        if (!a.allDay && a.startsAt != b.startsAt) return a.startsAt < b.startsAt;
        return a.title < b.title;
    });
    if (limit >= 0 && result.size() > limit) result.resize(limit);
    return result;
}
} // namespace virgil::calendar
