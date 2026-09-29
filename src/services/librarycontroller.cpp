#include "services/librarycontroller.h"
#include "domain/error.h"
#include "services/paths.h"

#include <QMetaObject>
#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace virgil {
class LibraryWorker : public QObject {
public:
    std::unique_ptr<Library> library;
    Library& current()
    {
        if (!library)
            throw Error("Create or open a library first.");
        return *library;
    }
    Snapshot openedSnapshot(const Work& work)
    {
        try { return current().refresh(work); }
        catch (const Error& error) {
            auto saved = current().snapshot();
            saved.notices.append("Showing saved information. Refresh was incomplete: " + QString::fromUtf8(error.what()));
            return saved;
        }
    }
};

LibraryController::LibraryController(QObject* parent) : QObject(parent), worker_(new LibraryWorker)
{
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_.setObjectName("Virgil library worker");
    thread_.start();
    debounce_.setSingleShot(true);
    debounce_.setInterval(750);
    audit_.setInterval(30000);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) { scheduleRefresh(path); });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) { scheduleRefresh(path); });
    connect(&debounce_, &QTimer::timeout, this, [this] {
        if (busy_) { pendingRefresh_ = true; return; }
        pendingRefresh_ = false;
        if (!watchedRoot_.isEmpty()) {
            const bool audit = fullAudit_;
            const auto changed = audit ? QStringList{} : changedDirectories_;
            changedDirectories_.clear();
            fullAudit_ = false;
            run([changed, audit](LibraryWorker& worker, const Work& work) {
                const auto before = worker.current().snapshot();
                auto after = worker.current().refresh(work, changed);
                if (audit && (before.materials != after.materials || before.folders != after.folders))
                    after.notices.append("Periodic reconciliation found changes not yet reflected by filesystem watches. F5 is always available.");
                return Outcome{after, {}, {}, audit ? "Periodic reconciliation complete." : "Filesystem changes reconciled."};
            });
        }
    });
    connect(&audit_, &QTimer::timeout, this, [this] { fullAudit_ = true; scheduleRefresh(); });
    audit_.start();
}

LibraryController::~LibraryController()
{
    cancel();
    thread_.quit();
    // Never terminate a thread in the middle of SQLite or a file write.
    thread_.wait();
}

void LibraryController::cancel()
{
    if (cancellation_)
        cancellation_->store(true);
}

void LibraryController::run(std::function<Outcome(LibraryWorker&, const Work&)> operation)
{
    if (busy_)
        return; // The UI disables competing actions; one job owns the library.
    busy_ = true;
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    emit busyChanged(true);
    Work work;
    work.cancelled = cancellation_;
    work.progress = [this](const QString& message, int percent) {
        QMetaObject::invokeMethod(this, [this, message, percent] { emit progress(message, percent); }, Qt::QueuedConnection);
    };
    QMetaObject::invokeMethod(worker_, [this, operation = std::move(operation), work] {
        try {
            work.checkpoint();
            const Outcome result = operation(*worker_, work);
            QMetaObject::invokeMethod(this, [this, result] {
                busy_ = false;
                emit busyChanged(false);
                if (result.snapshot) {
                    auto snapshot = *result.snapshot;
                    configureWatches(snapshot);
                    emit snapshotReady(snapshot, result.message);
                }
                if (pendingRefresh_) debounce_.start();
                if (result.preview) emit directoryInspected(*result.preview);
                if (!result.path.isEmpty()) emit localPathReady(result.path);
            }, Qt::QueuedConnection);
        } catch (const std::exception& error) {
            const QString message = QString::fromUtf8(error.what());
            QMetaObject::invokeMethod(this, [this, message] {
                busy_ = false;
                emit busyChanged(false);
                emit failed(message);
                if (pendingRefresh_) debounce_.start();
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}

void LibraryController::inspectDirectory(const QString& directory)
{
    run([directory](LibraryWorker&, const Work& work) { return Outcome{{}, Library::inspect(directory, work), {}, {}}; });
}

void LibraryController::createLibrary(const QString& directory, bool adoptExisting)
{
    run([directory, adoptExisting](LibraryWorker& worker, const Work& work) {
        worker.library = Library::create(directory, adoptExisting);
        return Outcome{worker.openedSnapshot(work), {}, {}, "Library created. Existing files were kept in place."};
    });
}

void LibraryController::openLibrary(const QString& directory)
{
    run([directory](LibraryWorker& worker, const Work& work) {
        if (!worker.library || worker.library->root() != paths::rootDirectory(directory)) {
            auto next = Library::open(directory); // Failure leaves the old library usable.
            worker.library = std::move(next);
        }
        return Outcome{worker.openedSnapshot(work), {}, {}, "Library opened. Files refreshed from disk."};
    });
}

void LibraryController::refresh()
{
    run([](LibraryWorker& worker, const Work& work) {
        return Outcome{worker.current().refresh(work), {}, {}, "Refreshed from disk. Missing records are retained."};
    });
}

void LibraryController::createSemester(const QString& name, const QString& season, QDate start, QDate end)
{
    run([=](LibraryWorker& worker, const Work&) {
        worker.current().createSemester(name, season, start, end);
        return Outcome{worker.current().snapshot(), {}, {}, "Semester saved and its directory created."};
    });
}

void LibraryController::createCourse(const QString& semesterId, const QString& name, const QString& code, const QString& color)
{
    run([=](LibraryWorker& worker, const Work&) {
        worker.current().createCourse(semesterId, name, code, color);
        return Outcome{worker.current().refresh(), {}, {}, "Course saved with four material folders."};
    });
}

void LibraryController::importFile(const QString& source, const QString& folder, const QString& filename)
{
    run([=](LibraryWorker& worker, const Work& work) {
        const QString message = worker.current().importFile(source, folder, filename, work);
        return Outcome{worker.current().snapshot(), {}, {}, message};
    });
}

void LibraryController::resolveMaterial(const QString& id, bool containingFolder)
{
    run([=](LibraryWorker& worker, const Work&) {
        const QString path = containingFolder ? worker.current().containingFolder(id) : worker.current().materialPath(id);
        return Outcome{{}, {}, path, {}};
    });
}
void LibraryController::scheduleRefresh(const QString& path)
{
    if (watchedRoot_.isEmpty()) return;
    if (!path.isEmpty() && !paths::isWithin(watchedRoot_, path) && watcher_.directories().contains(watchedRoot_)) return;
    pendingRefresh_ = true;
    if (!path.isEmpty()) changedDirectories_.append(QFileInfo(path).isDir() ? path : QFileInfo(path).dir().absolutePath());
    if (!debounce_.isActive()) debounce_.start();
}

void LibraryController::configureWatches(Snapshot& snapshot)
{
    if (watchedRoot_ != snapshot.root) {
        pendingRefresh_ = false;
        debounce_.stop();
        watchedRoot_ = snapshot.root;
        changedDirectories_.clear();
        fullAudit_ = false;
    }
    QStringList wanted{snapshot.root, QFileInfo(snapshot.root).dir().absolutePath()};
    for (const auto& folder : snapshot.folders) {
        try { wanted.append(paths::inside(snapshot.root, folder.path)); }
        catch (const Error&) { /* A changed symlink is excluded on the next scan. */ }
    }
    // Directory watches see names; file watches also see edits to existing bytes.
    for (const auto& material : snapshot.materials) {
        if (!material.available) continue;
        try { wanted.append(paths::inside(snapshot.root, material.path)); }
        catch (const Error&) {}
    }
    wanted.removeDuplicates();
    const QSet<QString> desired(wanted.begin(), wanted.end());
    const auto current = watcher_.directories() + watcher_.files();
    QStringList remove, add;
    for (const auto& path : current) if (!desired.contains(path)) remove.append(path);
    if (!remove.isEmpty()) watcher_.removePaths(remove);
    const QSet<QString> existing(current.begin(), current.end());
    for (const auto& path : wanted) if (!existing.contains(path)) add.append(path);
    // Bound native resource use. Periodic audits cover unwatched paths too.
    constexpr qsizetype watchBudget = 4096;
    const qsizetype room = qMax(qsizetype(0), watchBudget - (current.size() - remove.size()));
    const bool capped = add.size() > room;
    add = add.mid(0, room);
    const auto failed = add.isEmpty() ? QStringList{} : watcher_.addPaths(add);
    if (capped || !failed.isEmpty()) snapshot.notices.append("Some paths could not be watched (watch limit or disappearing paths). A 30-second reconciliation and F5 cover missed events.");
}

void LibraryController::createFolder(const QString& parent, const QString& name)
{
    run([=](LibraryWorker& w, const Work& work) { w.current().createFolder(parent, name); return Outcome{w.current().refresh(work), {}, {}, "Folder created."}; });
}
void LibraryController::movePath(const QString& source, const QString& destination)
{
    run([=](LibraryWorker& w, const Work& work) { w.current().movePath(source, destination); return Outcome{w.current().refresh(work), {}, {}, "Moved. Metadata identity preserved."}; });
}
void LibraryController::trashPath(const QString& source)
{
    run([=](LibraryWorker& w, const Work& work) { w.current().trashPath(source); return Outcome{w.current().refresh(work), {}, {}, "Moved to recoverable library trash."}; });
}
void LibraryController::restoreTrash(const QString& id)
{
    run([=](LibraryWorker& w, const Work& work) { w.current().restoreTrash(id); return Outcome{w.current().refresh(work), {}, {}, "Restored from trash."}; });
}
void LibraryController::removeCourse(const QString& id)
{
    run([=](LibraryWorker& w, const Work&) { w.current().removeCourse(id); return Outcome{w.current().snapshot(), {}, {}, "Course deleted. Files and resource records kept."}; });
}
void LibraryController::relink(const QString& missing, const QString& replacement)
{
    run([=](LibraryWorker& w, const Work& work) { w.current().relink(missing, replacement); return Outcome{w.current().refresh(work), {}, {}, "Relinked. Original record ID and title kept."}; });
}
void LibraryController::reviewOperation(const QString& id, bool retry)
{
    run([=](LibraryWorker& w, const Work& work) { w.current().reviewOperation(id, retry); return Outcome{w.current().refresh(work), {}, {}, "Operation reviewed."}; });
}
void LibraryController::importFiles(const QStringList& sources, const QString& folder)
{
    run([=](LibraryWorker& w, const Work& work) {
        QStringList results;
        for (const auto& source : sources) {
            work.checkpoint();
            try { results.append(QFileInfo(source).fileName() + ": " + w.current().importFile(source, folder, QFileInfo(source).fileName(), work)); }
            catch (const Error& e) { results.append(QFileInfo(source).fileName() + ": " + QString::fromUtf8(e.what())); }
        }
        auto snapshot = w.current().snapshot();
        snapshot.notices.append(results);
        return Outcome{snapshot, {}, {}, "Import batch finished. Review per-file results."};
    });
}

void LibraryController::editSemester(const Semester& semester)
{
    run([=](LibraryWorker& w, const Work&) { w.current().editSemester(semester); return Outcome{w.current().snapshot(), {}, {}, "Semester updated. ID and folder kept."}; });
}
void LibraryController::editCourse(const Course& course)
{
    run([=](LibraryWorker& w, const Work&) { w.current().editCourse(course); return Outcome{w.current().snapshot(), {}, {}, "Course updated. ID and folder kept."}; });
}
void LibraryController::editMaterial(const QString& id, const QString& title, const QString& category, QDate examDate)
{
    run([=](LibraryWorker& w, const Work&) { w.current().editMaterial(id, title, category, examDate); return Outcome{w.current().snapshot(), {}, {}, "Material metadata saved."}; });
}
void LibraryController::saveResource(const Resource& resource, const QString& source)
{
    run([=](LibraryWorker& w, const Work& work) {
        if (source.isEmpty()) w.current().saveResource(resource);
        else w.current().saveResourceWithFile(resource, source, work);
        return Outcome{w.current().snapshot(), {}, {}, "Resource saved locally. Attachment originals kept."};
    });
}
void LibraryController::removeResource(const QString& id)
{
    run([=](LibraryWorker& w, const Work&) { w.current().removeResource(id); return Outcome{w.current().snapshot(), {}, {}, "Resource record deleted. Attachment kept."}; });
}
void LibraryController::backupMetadata()
{
    run([](LibraryWorker& w, const Work&) { w.current().backupMetadata(); return Outcome{w.current().snapshot(), {}, {}, "Verified metadata-only snapshot saved. Materials are not backed up."}; });
}

void LibraryController::saveCalendarEvent(const CalendarEvent& event)
{
    run([=](LibraryWorker& w, const Work&) { w.current().saveCalendarEvent(event); return Outcome{w.current().snapshot(), {}, {}, "Event saved."}; });
}

void LibraryController::removeCalendarEvent(const QString& id)
{
    run([=](LibraryWorker& w, const Work&) { w.current().removeCalendarEvent(id); return Outcome{w.current().snapshot(), {}, {}, "Event deleted."}; });
}

void LibraryController::completeCalendarAssignment(const QString& id, bool completed)
{
    run([=](LibraryWorker& w, const Work&) { w.current().completeCalendarAssignment(id, completed); return Outcome{w.current().snapshot(), {}, {}, "Assignment updated."}; });
}

void LibraryController::saveWeeklyClasses(const QList<WeeklyClass>& classes)
{
    run([=](LibraryWorker& w, const Work&) { w.current().saveWeeklyClasses(classes); return Outcome{w.current().snapshot(), {}, {}, "Weekly classes saved."}; });
}

void LibraryController::removeWeeklyClass(const QString& id)
{
    run([=](LibraryWorker& w, const Work&) { w.current().removeWeeklyClass(id); return Outcome{w.current().snapshot(), {}, {}, "Weekly class deleted."}; });
}

void LibraryController::saveExamRange(const ExamRange& range)
{
    run([=](LibraryWorker& w, const Work&) { w.current().saveExamRange(range); return Outcome{w.current().snapshot(), {}, {}, "Exam range saved."}; });
}

void LibraryController::removeExamRange(const QString& id)
{
    run([=](LibraryWorker& w, const Work&) { w.current().removeExamRange(id); return Outcome{w.current().snapshot(), {}, {}, "Exam range deleted."}; });
}

void LibraryController::setCalendarTimeZone(const QString& zone)
{
    run([=](LibraryWorker& w, const Work&) { w.current().setCalendarTimeZone(zone); return Outcome{w.current().snapshot(), {}, {}, "Calendar timezone updated."}; });
}

} // namespace virgil
