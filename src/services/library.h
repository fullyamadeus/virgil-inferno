#pragma once

#include "domain/records.h"
#include <QLockFile>
#include <atomic>
#include <functional>
#include <memory>

namespace virgil {
class Database;

struct Work {
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    std::function<void(const QString&, int)> progress;
    void checkpoint() const;
    void report(const QString& message, int percent = -1) const;
};

// Synchronous application operations. The GUI runs these on its worker thread;
// tests can use them directly without creating any windows.
class Library {
public:
    static DirectoryPreview inspect(const QString& directory, const Work& work = {});
    static std::unique_ptr<Library> create(const QString& directory, bool adoptExisting = false);
    static std::unique_ptr<Library> open(const QString& directory);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    const QString& root() const { return root_; }
    Snapshot snapshot() const;
    Snapshot refresh(const Work& work = {}, const QStringList& changedDirectories = {});
    QString createSemester(const QString& name, const QString& season, QDate start, QDate end);
    QString createCourse(const QString& semesterId, const QString& name, const QString& code, const QString& color);
    QString importFile(const QString& source, const QString& destinationDirectory,
                       const QString& filename, const Work& work = {});
    void createFolder(const QString& parent, const QString& name);
    void movePath(const QString& source, const QString& destination);
    void trashPath(const QString& source);
    void restoreTrash(const QString& id);
    void removeCourse(const QString& id);
    void editSemester(const Semester& semester);
    void editCourse(const Course& course);
    void editMaterial(const QString& id, const QString& title, const QString& category, QDate examDate = {});
    QString saveResource(Resource resource);
    QString saveResourceWithFile(Resource resource, const QString& source, const Work& work = {});
    void removeResource(const QString& id);
    void backupMetadata();
    QString saveCalendarEvent(CalendarEvent event);
    void removeCalendarEvent(const QString& id);
    void completeCalendarAssignment(const QString& id, bool completed);
    void saveWeeklyClasses(QList<WeeklyClass> classes);
    void removeWeeklyClass(const QString& id);
    QString saveExamRange(ExamRange range);
    void removeExamRange(const QString& id);
    void setCalendarTimeZone(const QString& zone);
    void relink(const QString& missingId, const QString& replacementId);
    void reviewOperation(const QString& id, bool retry);
    QString materialPath(const QString& id) const;
    QString containingFolder(const QString& id) const;

private:
    Library() = default;
    void initialize(const QString& directory, bool create, bool adoptExisting);
    void ensureAvailable() const;
    void recoverImports();
    void recoverOperations();
    void ensureNoPendingMove() const;
    void validateResource(const Resource& resource, bool pendingAttachment = false) const;
    void automaticBackup(bool changed = false);
    void finishOperation(const QString& id, const QString& source, const QString& destination, const QString& identity, const QString& kind);
    Material describe(const QString& relativePath, const QList<Course>& courses) const;

    QString root_;
    QString identity_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<Database> database_; // Destroyed before the lock (reverse member order).
    QStringList notices_;
};

} // namespace virgil
