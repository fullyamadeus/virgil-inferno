#pragma once

#include "services/library.h"
#include <QObject>
#include <QThread>
#include <QFileSystemWatcher>
#include <QTimer>
#include <functional>
#include <optional>

namespace virgil {
class LibraryWorker;

class LibraryController : public QObject {
    Q_OBJECT
public:
    explicit LibraryController(QObject* parent = nullptr);
    ~LibraryController() override;
    bool busy() const { return busy_; }

    void inspectDirectory(const QString& directory);
    void createLibrary(const QString& directory, bool adoptExisting);
    void openLibrary(const QString& directory);
    void refresh();
    void createSemester(const QString& name, const QString& season, QDate start, QDate end);
    void createCourse(const QString& semesterId, const QString& name, const QString& code, const QString& color);
    void importFile(const QString& source, const QString& folder, const QString& filename);
    void createFolder(const QString& parent, const QString& name);
    void movePath(const QString& source, const QString& destination);
    void trashPath(const QString& source);
    void restoreTrash(const QString& id);
    void removeCourse(const QString& id);
    void editSemester(const Semester& semester);
    void editCourse(const Course& course);
    void editMaterial(const QString& id, const QString& title, const QString& category, QDate examDate = {});
    void saveResource(const Resource& resource, const QString& source = {});
    void removeResource(const QString& id);
    void backupMetadata();
    void saveCalendarEvent(const CalendarEvent& event);
    void removeCalendarEvent(const QString& id);
    void completeCalendarAssignment(const QString& id, bool completed);
    void saveWeeklyClasses(const QList<WeeklyClass>& classes);
    void removeWeeklyClass(const QString& id);
    void saveExamRange(const ExamRange& range);
    void removeExamRange(const QString& id);
    void setCalendarTimeZone(const QString& zone);
    void relink(const QString& missing, const QString& replacement);
    void reviewOperation(const QString& id, bool retry = true);
    void importFiles(const QStringList& sources, const QString& folder);
    void resolveMaterial(const QString& id, bool containingFolder = false);
    void cancel();

signals:
    void snapshotReady(const virgil::Snapshot& snapshot, const QString& message);
    void directoryInspected(const virgil::DirectoryPreview& preview);
    void localPathReady(const QString& path);
    void failed(const QString& message);
    void busyChanged(bool busy);
    void progress(const QString& message, int percent);

private:
    struct Outcome {
        std::optional<Snapshot> snapshot;
        std::optional<DirectoryPreview> preview;
        QString path;
        QString message;
    };
    void run(std::function<Outcome(LibraryWorker&, const Work&)> operation);
    void configureWatches(Snapshot& snapshot);
    void scheduleRefresh(const QString& path = {});
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    QTimer audit_;
    QString watchedRoot_;
    bool pendingRefresh_ = false;
    bool fullAudit_ = false;
    QStringList changedDirectories_;
    QThread thread_;
    LibraryWorker* worker_ = nullptr; // Deleted in its own thread when it finishes.
    std::shared_ptr<std::atomic_bool> cancellation_;
    bool busy_ = false;
};
} // namespace virgil
