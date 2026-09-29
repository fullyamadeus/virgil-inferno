#include "services/calendar.h"
#include "services/library.h"
#include "storage/database.h"
#include "domain/error.h"
#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace virgil;
namespace {
QDate day(const char* value) { return QDate::fromString(QString::fromUtf8(value), Qt::ISODate); }
QDateTime utc(const char* value) { return QDateTime::fromString(QString::fromUtf8(value), Qt::ISODate); }
Semester winter() { return {"winter", "A summer-sounding label", "winter", day("2026-09-01"), day("2027-02-28"), "Winter"}; }
QList<ExamRange> periods() { return {{"third", "winter", day("2027-01-10"), day("2027-01-20")}, {"first", "winter", day("2026-11-12"), day("2026-11-23")}, {"second", "winter", day("2026-12-10"), day("2026-12-17")}}; }
QString addSemester(Library& library) { return library.createSemester("Winter", "winter", day("2026-09-01"), day("2027-02-28")); }
CalendarEvent eventOn(const QString& semester, const QString& course = {})
{
    CalendarEvent event; event.title = "Saved exam"; event.semesterId = semester; event.courseId = course; event.date = day("2026-12-18"); return event;
}
WeeklyClass weekly(const QString& semester, const QString& course = {})
{
    WeeklyClass result; result.title = "Weekly class"; result.semesterId = semester; result.courseId = course; return result;
}
}

class CalendarTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void recurrenceBoundaries_data()
    {
        QTest::addColumn<QDate>("date"); QTest::addColumn<bool>("visible");
        const QList<QPair<const char*, bool>> cases = {
            {"2026-08-31", false}, {"2026-09-30", false}, {"2026-10-01", true}, {"2026-11-11", true},
            {"2026-11-12", false}, {"2026-11-23", false}, {"2026-11-24", true}, {"2026-12-09", true},
            {"2026-12-10", false}, {"2026-12-17", false}, {"2026-12-18", false}, {"2027-01-11", false}, {"2027-03-01", false}};
        for (const auto& [date, visible] : cases) QTest::newRow(date) << day(date) << visible;
    }
    void recurrenceBoundaries()
    {
        QFETCH(QDate, date); QFETCH(bool, visible);
        QCOMPARE(calendar::recurringClassVisible(date, winter(), periods()), visible);
    }
    void seasonDefaultsLeapYearsClippingAndCustomPolicy()
    {
        auto semester = winter();
        QVERIFY(calendar::recurringClassVisible(day("2026-12-31"), semester, {}));
        QVERIFY(!calendar::recurringClassVisible(day("2027-01-01"), semester, {}));
        semester.start = day("2026-10-12"); semester.end = day("2026-12-20");
        QVERIFY(!calendar::recurringClassVisible(day("2026-10-11"), semester, {}));
        QVERIFY(calendar::recurringClassVisible(semester.start, semester, {}));
        QVERIFY(!calendar::recurringClassVisible(day("2026-12-21"), semester, {}));
        semester = {"summer", "Winter-looking label", "summer", day("2028-02-01"), day("2028-08-31"), "Summer"};
        QVERIFY(!calendar::recurringClassVisible(day("2028-02-17"), semester, {}));
        QVERIFY(calendar::recurringClassVisible(day("2028-02-18"), semester, {}));
        QVERIFY(calendar::recurringClassVisible(day("2028-02-29"), semester, {}));
        QVERIFY(calendar::recurringClassVisible(day("2028-05-31"), semester, {}));
        QVERIFY(!calendar::recurringClassVisible(day("2028-06-01"), semester, {}));
        semester.teachingStart = day("2028-06-01"); semester.teachingEnd = day("2028-06-30");
        QVERIFY(calendar::recurringClassVisible(day("2028-06-01"), semester, {}));
        QVERIFY(!calendar::recurringClassVisible(day("2028-05-31"), semester, {}));
        semester = winter();
        const QList<ExamRange> adjacent{{"one", semester.id, day("2026-10-07"), day("2026-10-13")}, {"two", semester.id, day("2026-10-14"), day("2026-10-20")}};
        QVERIFY(calendar::recurringClassVisible(day("2026-10-06"), semester, adjacent));
        QVERIFY(!calendar::recurringClassVisible(day("2026-10-21"), semester, adjacent));
        QVERIFY(calendar::recurringClassVisible(day("2026-10-14"), semester, {adjacent.first()}));
        QCOMPARE(calendar::mondayOf(day("2026-11-01")), day("2026-10-26"));
        QCOMPARE(calendar::mondayOf(day("2028-01-01")), day("2027-12-27"));
    }
    void localTimesRejectGapsAndRequireExplicitFolds()
    {
        const QString zone = "Europe/Skopje";
        QCOMPARE(calendar::localToUtc(day("2026-01-10"), QTime(9, 0), zone), utc("2026-01-10T08:00:00Z"));
        QCOMPARE(calendar::localToUtc(day("2026-07-10"), QTime(9, 0), zone), utc("2026-07-10T07:00:00Z"));
        for (const auto& choice : {"reject", "earlier", "later"})
            QVERIFY_EXCEPTION_THROWN(calendar::localToUtc(day("2026-03-29"), QTime(2, 30), zone, choice), Error);
        QVERIFY_EXCEPTION_THROWN(calendar::localToUtc(day("2026-10-25"), QTime(2, 30), zone), Error);
        QCOMPARE(calendar::localToUtc(day("2026-10-25"), QTime(2, 30), zone, "earlier"), utc("2026-10-25T00:30:00Z"));
        QCOMPARE(calendar::localToUtc(day("2026-10-25"), QTime(2, 30), zone, "later"), utc("2026-10-25T01:30:00Z"));
        QVERIFY_EXCEPTION_THROWN(calendar::localToUtc({}, QTime(12, 0), zone), Error);
        QVERIFY_EXCEPTION_THROWN(calendar::localToUtc(day("2026-01-01"), {}, zone), Error);
        QVERIFY_EXCEPTION_THROWN(calendar::localToUtc(day("2026-01-01"), QTime(9, 0), "Not/AZone"), Error);
        QCOMPARE(calendar::localToUtc(day("2026-10-25"), QTime(2, 30), "UTC"), utc("2026-10-25T02:30:00Z"));
        // A half-hour DST transition must also be detected, rather than assuming a one-hour change.
        QVERIFY_EXCEPTION_THROWN(calendar::localToUtc(day("2026-10-04"), QTime(2, 15), "Australia/Lord_Howe"), Error);
        QVERIFY_EXCEPTION_THROWN(calendar::localToUtc(day("2026-04-05"), QTime(1, 45), "Australia/Lord_Howe"), Error);
    }
    void weeklyClockChangesAreFlaggedAndChosenExplicitly()
    {
        Snapshot snapshot; snapshot.semesters = {winter()}; auto slot = weekly("winter"); slot.id = "class";
        slot.weekday = 7; slot.start = QTime(2, 30); slot.end = QTime(3, 30); snapshot.weeklyClasses = {slot};
        auto result = calendar::occurrences(snapshot, day("2026-10-25"), day("2026-10-25"));
        QCOMPARE(result.size(), 1); QVERIFY(result.first().warning.contains("occurs twice")); QVERIFY(!result.first().startsAt.isValid());
        snapshot.weeklyClasses.first().ambiguity = "later";
        result = calendar::occurrences(snapshot, day("2026-10-25"), day("2026-10-25"));
        QCOMPARE(result.size(), 1); QVERIFY(result.first().warning.isEmpty()); QCOMPARE(result.first().startsAt, utc("2026-10-25T01:30:00Z"));
        snapshot.semesters.first().season = "summer"; snapshot.semesters.first().start = day("2026-02-01"); snapshot.semesters.first().end = day("2026-08-31");
        result = calendar::occurrences(snapshot, day("2026-03-29"), day("2026-03-29"));
        QCOMPARE(result.size(), 1); QVERIFY(result.first().warning.contains("does not exist"));
    }
    void calendarProjectionKeepsSavedEventsIndependentAndDatesStable()
    {
        Snapshot snapshot; snapshot.semesters = {winter()}; snapshot.examRanges = periods();
        auto exam = eventOn("winter"); exam.id = "event"; snapshot.events = {exam};
        auto slot = weekly("winter"); slot.id = "slot"; slot.weekday = 5; snapshot.weeklyClasses = {slot};
        auto result = calendar::occurrences(snapshot, day("2026-12-18"), day("2026-12-18"), "winter");
        QCOMPARE(result.size(), 1); QCOMPARE(result.first().eventId, exam.id); // Second period suppresses only the recurrence.
        QCOMPARE(calendar::occurrences(snapshot, day("2026-12-18"), day("2026-12-18"), "other").size(), 0);
        snapshot.events.first().semesterId.clear();
        QCOMPARE(calendar::occurrences(snapshot, day("2026-12-18"), day("2026-12-18"), "other").size(), 1);
        auto timed = eventOn("winter"); timed.id = "timed"; timed.allDay = false;
        timed.startsAt = utc("2026-12-18T00:30:00Z"); timed.endsAt = utc("2026-12-18T02:00:00Z"); snapshot.events.append(timed);
        result = calendar::occurrences(snapshot, day("2026-12-17"), day("2026-12-18"), {}, "America/New_York");
        QCOMPARE(result.size(), 2); QCOMPARE(result.first().eventId, QString("timed")); QCOMPARE(result.first().date, day("2026-12-17"));
        QCOMPARE(result.last().date, exam.date); // All-day date unchanged in another timezone.
        snapshot.events = {timed}; snapshot.events.first().startsAt = utc("2026-12-17T23:00:00Z"); snapshot.events.first().endsAt = utc("2026-12-19T00:00:00Z");
        result = calendar::occurrences(snapshot, day("2026-12-17"), day("2026-12-19"), {}, "UTC");
        QCOMPARE(result.size(), 2); QCOMPARE(result.last().date, day("2026-12-18")); // End exactly at midnight is exclusive.
        snapshot.events.clear(); snapshot.examRanges.clear(); snapshot.weeklyClasses.first().weekday = 1;
        snapshot.weeklyClasses.first().timeZone = "Pacific/Kiritimati"; snapshot.weeklyClasses.first().start = QTime(0, 30); snapshot.weeklyClasses.first().end = QTime(1, 30);
        result = calendar::occurrences(snapshot, day("2026-10-04"), day("2026-10-04"), {}, "America/Los_Angeles");
        QCOMPARE(result.size(), 1); QCOMPARE(result.first().date, day("2026-10-04")); // Local Monday projects into Sunday.
    }
    void examRangesValidateMaximumOverlapDatesAndSemesterEdits()
    {
        QTemporaryDir temporary; auto library = Library::create(temporary.path()); const auto semester = addSemester(*library);
        ExamRange first{{}, semester, day("2026-11-12"), day("2026-11-23")}; first.id = library->saveExamRange(first);
        QVERIFY_EXCEPTION_THROWN(library->saveExamRange({{}, semester, day("2026-11-23"), day("2026-11-30")}), Error);
        const auto adjacentId = library->saveExamRange({{}, semester, day("2026-11-24"), day("2026-12-01")});
        library->saveExamRange({{}, semester, day("2027-01-10"), day("2027-01-20")});
        QVERIFY_EXCEPTION_THROWN(library->saveExamRange({{}, semester, day("2027-02-01"), day("2027-02-02")}), Error);
        QVERIFY_EXCEPTION_THROWN(library->saveExamRange({{}, semester, day("2026-08-31"), day("2026-09-01")}), Error);
        QVERIFY_EXCEPTION_THROWN(library->saveExamRange({{}, semester, day("2027-02-28"), day("2027-03-01")}), Error);
        auto savedSemester = library->snapshot().semesters.first(); savedSemester.end = day("2026-12-31");
        QVERIFY_EXCEPTION_THROWN(library->editSemester(savedSemester), Error);
        QCOMPARE(library->snapshot().examRanges.size(), 3);
        first.start = day("2026-11-01"); QCOMPARE(library->saveExamRange(first), first.id);
        library->removeExamRange(adjacentId); QCOMPARE(library->snapshot().examRanges.size(), 2);
        savedSemester = library->snapshot().semesters.first(); savedSemester.teachingStart = day("2026-08-01"); savedSemester.teachingEnd = day("2026-12-01");
        QVERIFY_EXCEPTION_THROWN(library->editSemester(savedSemester), Error);
        savedSemester.teachingStart = day("2026-09-01"); library->editSemester(savedSemester);
        QCOMPARE(library->snapshot().semesters.first().teachingStart, savedSemester.teachingStart);
        savedSemester.teachingEnd = {}; QVERIFY_EXCEPTION_THROWN(library->editSemester(savedSemester), Error);
    }
    void eventsClassesCompletionDetachAndReassignmentPersist()
    {
        QTemporaryDir temporary; auto library = Library::create(temporary.path()); const auto semester = addSemester(*library);
        const auto course = library->createCourse(semester, "Math", "MATH", courseColors.first());
        QString assignmentId;
        for (const auto& kind : eventKinds) { auto event = eventOn(semester, course); event.kind = kind; const auto id = library->saveCalendarEvent(event); if (kind == "assignment") assignmentId = id; }
        library->completeCalendarAssignment(assignmentId, true);
        auto classes = QList<WeeklyClass>{weekly(semester, course), weekly(semester, course)}; classes.last().weekday = 3; classes.last().kind = "lab";
        library->saveWeeklyClasses(classes); QCOMPARE(library->snapshot().weeklyClasses.size(), 2);
        classes.last().end = classes.last().start; QVERIFY_EXCEPTION_THROWN(library->saveWeeklyClasses(classes), Error);
        QCOMPARE(library->snapshot().weeklyClasses.size(), 2); // Entire invalid batch rejected.
        const QString laterSemester = library->createSemester("Summer", "summer", day("2027-02-01"), day("2027-08-31"));
        auto editedCourse = library->snapshot().courses.first(); editedCourse.semesterId = laterSemester; library->editCourse(editedCourse);
        auto saved = library->snapshot();
        for (const auto& event : saved.events) { QCOMPARE(event.semesterId, laterSemester); QCOMPARE(event.date, day("2026-12-18")); }
        for (const auto& slot : saved.weeklyClasses) QCOMPARE(slot.semesterId, laterSemester);
        const auto slotId = saved.weeklyClasses.first().id; auto editedSlot = saved.weeklyClasses.first(); editedSlot.weekday = 7; library->saveWeeklyClasses({editedSlot});
        library->removeCourse(course); saved = library->snapshot();
        QCOMPARE(saved.events.size(), 5); QCOMPARE(saved.weeklyClasses.size(), 2);
        for (const auto& event : saved.events) QVERIFY(event.courseId.isEmpty());
        for (const auto& slot : saved.weeklyClasses) QVERIFY(slot.courseId.isEmpty());
        library.reset(); library = Library::open(temporary.path()); saved = library->snapshot();
        QCOMPARE(saved.events.size(), 5); QCOMPARE(saved.weeklyClasses.size(), 2);
        for (const auto& event : saved.events) if (event.id == assignmentId) QVERIFY(event.completed);
        library->completeCalendarAssignment(assignmentId, false); library->removeCalendarEvent(assignmentId); library->removeWeeklyClass(slotId);
        QCOMPARE(library->snapshot().events.size(), 4); QCOMPARE(library->snapshot().weeklyClasses.size(), 1);
    }
    void timedEventValidationAndTimezoneChangesDoNotMoveInstants()
    {
        QTemporaryDir temporary; auto library = Library::create(temporary.path()); const auto semester = addSemester(*library);
        auto event = eventOn(semester); event.allDay = false;
        event.startsAt = calendar::localToUtc(day("2026-10-25"), QTime(2, 30), "Europe/Skopje", "later"); event.endsAt = event.startsAt.addSecs(3600);
        event.id = library->saveCalendarEvent(event);
        library->setCalendarTimeZone("America/New_York"); auto saved = library->snapshot();
        QCOMPARE(saved.events.first().startsAt, utc("2026-10-25T01:30:00Z")); QCOMPARE(saved.calendarTimeZone, QString("America/New_York"));
        event.title = "Edited timed event"; QCOMPARE(library->saveCalendarEvent(event), event.id);
        event.endsAt = event.startsAt; QVERIFY_EXCEPTION_THROWN(library->saveCalendarEvent(event), Error);
        event.endsAt = event.startsAt.addSecs(3600); event.completed = true; QVERIFY_EXCEPTION_THROWN(library->saveCalendarEvent(event), Error);
        event.completed = false; event.startsAt = QDateTime(day("2026-10-25"), QTime(2, 30), calendar::timeZone("Europe/Skopje"));
        QVERIFY_EXCEPTION_THROWN(library->saveCalendarEvent(event), Error);
        QVERIFY_EXCEPTION_THROWN(library->completeCalendarAssignment(event.id, true), Error);
        QVERIFY_EXCEPTION_THROWN(library->setCalendarTimeZone("Not/AZone"), Error);
        library.reset(); library = Library::open(temporary.path()); saved = library->snapshot();
        QCOMPARE(saved.calendarTimeZone, QString("America/New_York")); QCOMPARE(saved.events.first().startsAt, utc("2026-10-25T01:30:00Z"));
    }
    void upcomingSortsDatesAndExcludesCompletedOrFilteredEvents()
    {
        Snapshot snapshot; auto allDay = eventOn("winter"); allDay.id = "all-day"; allDay.kind = "assignment";
        auto timed = allDay; timed.id = "timed"; timed.allDay = false; timed.startsAt = utc("2026-12-18T10:00:00Z"); timed.endsAt = timed.startsAt.addSecs(3600);
        auto past = timed; past.id = "past"; past.startsAt = utc("2026-12-17T23:00:00Z"); past.endsAt = past.startsAt.addSecs(3600);
        snapshot.events = {timed, past, allDay}; auto next = calendar::upcoming(snapshot, "winter", utc("2026-12-18T08:00:00Z"));
        QCOMPARE(next.size(), 2); QCOMPARE(next.first().id, allDay.id); QCOMPARE(next.last().id, timed.id);
        snapshot.events.last().completed = true; QCOMPARE(calendar::upcoming(snapshot, "winter", utc("2026-12-18T08:00:00Z")).size(), 1);
        QCOMPARE(calendar::upcoming(snapshot, "other", utc("2026-12-18T08:00:00Z")).size(), 0);
        snapshot.events.first().semesterId.clear(); QCOMPARE(calendar::upcoming(snapshot, "other", utc("2026-12-18T08:00:00Z"), 1).size(), 1);
    }
    void schemaThreeUpgradePreservesResourcesAndVerifiedSnapshot()
    {
        QTemporaryDir temporary; QString identity, resourceId;
        {
            auto library = Library::create(temporary.path()); const auto semester = addSemester(*library); identity = library->snapshot().libraryId;
            Resource lecture; lecture.kind = "lecture"; lecture.title = "Retained lecture"; lecture.date = day("2026-10-10"); lecture.notes = "Important notes"; resourceId = library->saveResource(lecture);
            Q_UNUSED(semester);
        }
        {
            Database db(temporary.filePath(".virgil/library.sqlite"), false);
            for (const auto& table : {"calendar_events", "weekly_classes", "exam_ranges", "calendar_settings"}) db.query("DROP TABLE " + QString::fromUtf8(table));
            db.query("ALTER TABLE semesters DROP COLUMN teaching_start"); db.query("ALTER TABLE semesters DROP COLUMN teaching_end"); db.query("PRAGMA user_version=3");
        }
        auto library = Library::open(temporary.path()); const auto saved = library->snapshot();
        QCOMPARE(saved.libraryId, identity); QCOMPARE(saved.resources.first().id, resourceId); QCOMPARE(saved.resources.first().notes, QString("Important notes"));
        QCOMPARE(saved.calendarTimeZone, QString("Europe/Skopje")); QVERIFY(saved.events.isEmpty());
        const auto backups = QDir(temporary.filePath(".virgil")).entryList({"*.before-v4-*.sqlite"}, QDir::Files); QCOMPARE(backups.size(), 1);
        Database db(temporary.filePath(".virgil/library.sqlite"), false); db.verifySnapshot(temporary.filePath(".virgil/" + backups.first()));
        const QString connection = "m4-snapshot-read";
        {
            auto raw = QSqlDatabase::addDatabase("QSQLITE", connection); raw.setDatabaseName(temporary.filePath(".virgil/" + backups.first())); raw.setConnectOptions("QSQLITE_OPEN_READONLY"); QVERIFY(raw.open());
            QSqlQuery query(raw); QVERIFY(query.exec("PRAGMA user_version")); QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 3);
            QVERIFY(query.exec("SELECT id,notes FROM resources")); QVERIFY(query.next()); QCOMPARE(query.value(0).toString(), resourceId); QCOMPARE(query.value(1).toString(), QString("Important notes"));
        }
        QSqlDatabase::removeDatabase(connection);
    }
};
QTEST_GUILESS_MAIN(CalendarTest)
#include "calendar_test.moc"
