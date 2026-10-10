#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QTime>
#include <QStringList>

namespace virgil {

struct Semester {
    QString id;
    QString name;
    QString season; // "winter" or "summer", never inferred from the name.
    QDate start;
    QDate end;
    QString path; // All stored paths are relative to the library root.
    QDate teachingStart = {}; // Both invalid means the explicit season default.
    QDate teachingEnd = {};
};

struct Course {
    QString id;
    QString semesterId;
    QString name;
    QString code;
    QString color;
    QString path;
};

struct Material {
    QString id;
    QString courseId; // Empty means not yet associated with a course.
    QString title;    // Metadata title is independent of the filename in path.
    QString path;
    qint64 size = 0;
    QDateTime modified;
    bool available = true;
    QString identity; // Device/inode/birth time, empty when unavailable.
    QString category = "unclassified";
    QDate examDate = {};
    bool operator==(const Material&) const = default;
};

struct Folder { QString path; QString identity; bool available = true; bool operator==(const Folder&) const = default; };
struct Recovery { QString id; QString source; QString destination; QString state; };

struct Resource {
    QString id;
    QString courseId;
    QString kind; // book, lecture, bookmark
    QString title;
    QString materialId;
    QDate date;
    QString notes;
    QString url;
    QString folder;
    QString category = "unclassified";
    QDate examDate;
};
inline const QStringList materialCategories = {"unclassified", "past-exam", "single-question", "study-guide", "other", "lecture"};
inline QString categoryTitle(const QString& category)
{
    const QStringList titles{"Unclassified", "Past exam", "Single question", "Study guide", "Other", "Lecture material"};
    const qsizetype index = materialCategories.indexOf(category);
    return index < 0 ? category : titles[index];
}

// All-day dates and UTC instants are deliberately separate representations.
struct CalendarEvent {
    QString id;
    QString semesterId; // Empty means visible in every semester filter.
    QString courseId;
    QString title;
    QString kind = "exam";
    bool allDay = true;
    QDate date;
    QDateTime startsAt;
    QDateTime endsAt;
    QString timeZone = "Europe/Skopje";
    bool completed = false; // Assignment completion only.
};
struct WeeklyClass {
    QString id;
    QString semesterId;
    QString courseId;
    QString title;
    QString kind = "lecture";
    int weekday = 1; // ISO Monday=1 ... Sunday=7.
    QTime start = QTime(9, 0);
    QTime end = QTime(10, 30);
    QString timeZone = "Europe/Skopje";
    QString ambiguity = "reject"; // reject / earlier / later; gaps are never shifted.
    QString room; // Optional room for this weekday slot.
};
struct ExamRange { QString id; QString semesterId; QDate start; QDate end; };
inline const QStringList weeklyClassKinds = {"lecture", "lab", "auditory"};
inline const QStringList eventKinds = {"exam", "assignment", "review_session", "lecture", "lab"};
inline QString eventKindTitle(const QString& kind)
{
    if (kind == "auditory") return "Auditory Exercises";
    const QStringList titles{"Exam", "Assignment", "Review session", "Lecture", "Lab"};
    const qsizetype index = eventKinds.indexOf(kind);
    return index < 0 ? kind : titles[index];
}

struct Snapshot {
    QString root;
    QString libraryId;
    QList<Semester> semesters;
    QList<Course> courses;
    QList<Material> materials;
    QStringList notices;
    QList<Folder> folders;
    QList<Recovery> operations;
    QList<Recovery> trash;
    QList<Resource> resources;
    QStringList backups;
    QList<CalendarEvent> events;
    QList<WeeklyClass> weeklyClasses;
    QList<ExamRange> examRanges;
    QString calendarTimeZone = "Europe/Skopje";
};

struct DirectoryPreview {
    QString root;
    QStringList entries;
    bool truncated = false;
};

inline const QStringList courseColors = {
    "#2764C5", "#E63946", "#7C3DBB", "#1B8B5A", "#795548",
    "#ED7A22", "#D8B400", "#6D163A", "#D35A9B", "#A97816"
};

inline const QStringList courseColorNames = {
    "Blue", "Red", "Purple", "Green", "Brown", "Orange", "Yellow", "Bordeaux", "Pink", "Gold"
};
inline QString courseColorName(const QString& color)
{
    const qsizetype index = courseColors.indexOf(color);
    return index < 0 ? color : courseColorNames[index];
}

} // namespace virgil

Q_DECLARE_METATYPE(virgil::Snapshot)
Q_DECLARE_METATYPE(virgil::DirectoryPreview)
