#include "services/library.h"
#include "services/calendar.h"
#include "storage/database.h"
#include "domain/error.h"
#include <QUuid>
#include <QSet>

namespace virgil {
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QVariant optional(const QString& text) { return text.isEmpty() ? QVariant() : QVariant(text); }
void titleRequired(const QString& title)
{
    if (title.trimmed().isEmpty() || title.contains(QChar::Null)) throw Error("Enter a nonempty calendar title.");
}
const Semester& semesterFor(const Snapshot& saved, const QString& id)
{
    for (const auto& semester : saved.semesters) if (semester.id == id) return semester;
    throw Error("Choose a saved semester.");
}
void checkAssociation(const Snapshot& saved, const QString& semesterId, const QString& courseId, bool requireSemester)
{
    if (requireSemester || !semesterId.isEmpty()) (void)semesterFor(saved, semesterId);
    if (courseId.isEmpty()) return;
    for (const auto& course : saved.courses) if (course.id == courseId) {
        if (course.semesterId != semesterId) throw Error("Choose a course in the selected semester.");
        return;
    }
    throw Error("Choose a saved course, or No course.");
}
}

QString Library::saveCalendarEvent(CalendarEvent event)
{
    ensureAvailable(); ensureNoPendingMove(); titleRequired(event.title);
    const auto saved = database_->records();
    checkAssociation(saved, event.semesterId, event.courseId, false);
    if (!eventKinds.contains(event.kind)) throw Error("Choose a supported event type.");
    (void)calendar::timeZone(event.timeZone);
    if (event.completed && event.kind != "assignment") throw Error("Only assignments can be marked complete.");
    if (event.allDay) {
        if (!event.date.isValid()) throw Error("An all-day event needs a valid date.");
        event.startsAt = {}; event.endsAt = {};
    } else {
        if (!event.startsAt.isValid() || !event.endsAt.isValid() || event.endsAt <= event.startsAt)
            throw Error("A timed event must end after it starts.");
        // UTC or explicit fixed offsets are unambiguous. Reject implicit local/timezone inputs.
        for (const auto& instant : {event.startsAt, event.endsAt})
            if (instant.timeSpec() != Qt::UTC && instant.timeSpec() != Qt::OffsetFromUTC)
                throw Error("Resolve local times explicitly before saving a timed event.");
        event.startsAt = event.startsAt.toUTC(); event.endsAt = event.endsAt.toUTC(); event.date = {};
    }
    if (event.id.isEmpty()) event.id = newId();
    else {
        bool exists = false;
        for (const auto& previous : saved.events) if (previous.id == event.id) exists = true;
        if (!exists) throw Error("The event no longer exists.");
    }
    database_->query("INSERT INTO calendar_events VALUES (?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
        "semester_id=excluded.semester_id,course_id=excluded.course_id,title=excluded.title,kind=excluded.kind,"
        "all_day=excluded.all_day,on_date=excluded.on_date,starts_utc=excluded.starts_utc,ends_utc=excluded.ends_utc,"
        "timezone=excluded.timezone,completed=excluded.completed",
        {event.id, optional(event.semesterId), optional(event.courseId), event.title.trimmed(), event.kind, event.allDay,
         event.allDay ? QVariant(event.date.toString(Qt::ISODate)) : QVariant(),
         event.allDay ? QVariant() : QVariant(event.startsAt.toString(Qt::ISODateWithMs)),
         event.allDay ? QVariant() : QVariant(event.endsAt.toString(Qt::ISODateWithMs)), event.timeZone, event.completed});
    automaticBackup(true);
    return event.id;
}

void Library::removeCalendarEvent(const QString& id)
{
    ensureAvailable(); ensureNoPendingMove();
    database_->query("DELETE FROM calendar_events WHERE id=?", {id}); automaticBackup(true);
}

void Library::completeCalendarAssignment(const QString& id, bool completed)
{
    ensureAvailable(); ensureNoPendingMove();
    auto event = database_->query("SELECT kind FROM calendar_events WHERE id=?", {id});
    if (!event.next() || event.value(0).toString() != "assignment") throw Error("Select a saved assignment.");
    event.finish();
    database_->query("UPDATE calendar_events SET completed=? WHERE id=?", {completed, id}); automaticBackup(true);
}

void Library::saveWeeklyClasses(QList<WeeklyClass> classes)
{
    ensureAvailable(); ensureNoPendingMove();
    if (classes.isEmpty()) throw Error("Choose at least one weekday.");
    const auto saved = database_->records();
    QSet<QString> ids;
    for (auto& slot : classes) {
        titleRequired(slot.title); checkAssociation(saved, slot.semesterId, slot.courseId, true);
        (void)calendar::timeZone(slot.timeZone);
        if ((slot.kind != "lecture" && slot.kind != "lab") || slot.weekday < 1 || slot.weekday > 7
            || !slot.start.isValid() || !slot.end.isValid() || slot.end <= slot.start)
            throw Error("Choose a lecture/lab weekday and an end time after its start.");
        if (!QStringList{"reject", "earlier", "later"}.contains(slot.ambiguity)) throw Error("Choose a repeated-time policy.");
        if (slot.id.isEmpty()) slot.id = newId();
        else {
            bool exists = false;
            for (const auto& previous : saved.weeklyClasses) if (previous.id == slot.id) exists = true;
            if (!exists) throw Error("The weekly class no longer exists.");
        }
        if (ids.contains(slot.id)) throw Error("A weekly class was edited twice in the same request.");
        ids.insert(slot.id);
    }
    {
        Transaction transaction(*database_);
        for (const auto& slot : classes)
            database_->query("INSERT INTO weekly_classes VALUES (?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                "semester_id=excluded.semester_id,course_id=excluded.course_id,title=excluded.title,kind=excluded.kind,"
                "weekday=excluded.weekday,start_time=excluded.start_time,end_time=excluded.end_time,"
                "timezone=excluded.timezone,ambiguity=excluded.ambiguity",
                {slot.id, slot.semesterId, optional(slot.courseId), slot.title.trimmed(), slot.kind, slot.weekday,
                 slot.start.toString("HH:mm:ss"), slot.end.toString("HH:mm:ss"), slot.timeZone, slot.ambiguity});
        transaction.commit();
    }
    automaticBackup(true);
}

void Library::removeWeeklyClass(const QString& id)
{
    ensureAvailable(); ensureNoPendingMove();
    database_->query("DELETE FROM weekly_classes WHERE id=?", {id}); automaticBackup(true);
}

QString Library::saveExamRange(ExamRange range)
{
    ensureAvailable(); ensureNoPendingMove();
    const auto saved = database_->records();
    const auto& semester = semesterFor(saved, range.semesterId);
    if (!range.start.isValid() || !range.end.isValid() || range.end < range.start
        || range.start < semester.start || range.end > semester.end)
        throw Error("Choose an inclusive exam range wholly inside this semester.");
    int count = 0; bool exists = range.id.isEmpty();
    for (const auto& previous : saved.examRanges) {
        if (previous.id == range.id) { exists = true; continue; }
        if (previous.semesterId != range.semesterId) continue;
        ++count;
        if (range.start <= previous.end && range.end >= previous.start)
            throw Error("Exam ranges cannot overlap, including their end dates. Adjacent dates are allowed.");
    }
    if (!exists) throw Error("The exam range no longer exists.");
    if (count >= 3) throw Error("A semester can have at most three exam ranges.");
    if (range.id.isEmpty()) range.id = newId();
    database_->query("INSERT INTO exam_ranges VALUES (?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
        "semester_id=excluded.semester_id,start_date=excluded.start_date,end_date=excluded.end_date",
        {range.id, range.semesterId, range.start.toString(Qt::ISODate), range.end.toString(Qt::ISODate)});
    automaticBackup(true); return range.id;
}

void Library::removeExamRange(const QString& id)
{
    ensureAvailable(); ensureNoPendingMove();
    database_->query("DELETE FROM exam_ranges WHERE id=?", {id}); automaticBackup(true);
}

void Library::setCalendarTimeZone(const QString& zone)
{
    ensureAvailable(); ensureNoPendingMove(); (void)calendar::timeZone(zone);
    database_->query("UPDATE calendar_settings SET timezone=? WHERE id=1", {zone}); automaticBackup(true);
}
} // namespace virgil
