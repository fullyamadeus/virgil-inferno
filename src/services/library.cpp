#include "services/library.h"
#include "services/paths.h"
#include "storage/database.h"
#include "domain/error.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <QUrl>
#include <QRegularExpression>
#include <algorithm>
#include <filesystem>

namespace virgil {
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

bool ownedBackupName(const QString& filename)
{
    static const QRegularExpression pattern("^automatic-[0-9]{8}-[0-9]{9}-[0-9a-f-]{36}\\.sqlite$");
    return pattern.match(filename).hasMatch() && !QUuid(filename.mid(29, 36)).isNull();
}

void makeDirectory(const QString& path)
{
    if (!QDir().mkdir(path))
        throw Error("Cannot create directory (it may already exist or be unwritable): " + path);
}

QString readIdentity(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
        throw Error("Cannot read the library identity. Reconnect the original library; nothing was reset: " + filename);
    return QString::fromUtf8(file.read(128)).trimmed();
}

bool sameFile(const QString& first, const QString& second)
{
    std::error_code error;
    return std::filesystem::equivalent(paths::native(first), paths::native(second), error) && !error;
}
}

void Work::checkpoint() const
{
    if (cancelled && cancelled->load())
        throw Error("Operation cancelled. Original files were kept. Refresh to inspect the library.");
}

void Work::report(const QString& message, int percent) const
{
    if (progress)
        progress(message, percent);
}

DirectoryPreview Library::inspect(const QString& directory, const Work& work)
{
    DirectoryPreview preview;
    preview.root = paths::rootDirectory(directory);
    std::error_code error;
    std::filesystem::directory_iterator it(paths::native(preview.root), error), end;
    if (error)
        throw Error("Cannot inspect the directory: " + QString::fromStdString(error.message()));
    while (it != end) {
        work.checkpoint();
        const QFileInfo info(paths::fromNative(it->path()));
        preview.entries.append(info.fileName() + (info.isSymLink() ? " [symlink; excluded]" : info.isDir() ? "/" : ""));
        if (preview.entries.size() == 200) {
            preview.truncated = true;
            break;
        }
        it.increment(error);
        if (error)
            throw Error("Directory inspection failed: " + QString::fromStdString(error.message()));
    }
    preview.entries.sort(Qt::CaseInsensitive);
    return preview;
}

std::unique_ptr<Library> Library::create(const QString& directory, bool adoptExisting)
{
    auto library = std::unique_ptr<Library>(new Library);
    library->initialize(directory, true, adoptExisting);
    return library;
}

std::unique_ptr<Library> Library::open(const QString& directory)
{
    auto library = std::unique_ptr<Library>(new Library);
    library->initialize(directory, false, false);
    return library;
}

Library::~Library() = default;

void Library::initialize(const QString& directory, bool create, bool adoptExisting)
{
    root_ = paths::rootDirectory(directory);
    const QString metadata = paths::inside(root_, ".virgil", true);
    if (create) {
        if (paths::nameExists(root_, ".virgil"))
            throw Error("This directory already contains .virgil metadata. Use Open library; it will not be replaced.");
        QString ancestor = QFileInfo(root_).dir().absolutePath();
        while (!QDir(ancestor).isRoot()) {
            if (QFileInfo::exists(QDir(ancestor).filePath(".virgil/library.sqlite")))
                throw Error("Do not create a library inside another library. Choose a separate directory.");
            ancestor = QFileInfo(ancestor).dir().absolutePath();
        }
        if (!adoptExisting && !inspect(root_).entries.isEmpty())
            throw Error("This directory is not empty. Inspect it and explicitly adopt it first; no files were changed.");
        makeDirectory(metadata);
    } else if (!QFileInfo(metadata).isDir()) {
        throw Error("No Virgil metadata was found here. Use Create library to inspect and adopt an ordinary directory.");
    }
    const QString lockPath = paths::inside(root_, ".virgil/library.lock", true);
    lock_ = std::make_unique<QLockFile>(lockPath);
    lock_->setStaleLockTime(0); // A long editing session must not expire by age.
    if (!lock_->tryLock(0))
        throw Error("Library is locked by another Virgil instance, or its metadata is not writable. "
                    "Close the other instance or check directory permissions. No lock was forcibly removed.");
    const QString dbPath = paths::inside(root_, ".virgil/library.sqlite", true);
    // SQLite may open these itself, so validate its sidecars too.
    for (const auto& suffix : {"-journal", "-wal", "-shm"})
        (void)paths::inside(root_, ".virgil/library.sqlite" + QString(suffix), true);
    const QString expectedIdentity = create ? QString() : readIdentity(paths::inside(root_, ".virgil/library.id", true));
    if (!create && QUuid(expectedIdentity).isNull()) throw Error("Library identity marker is invalid. Nothing migrated.");
    database_ = std::make_unique<Database>(dbPath, create, expectedIdentity);
    identity_ = database_->libraryId();
    const QString identityPath = paths::inside(root_, ".virgil/library.id", true);
    if (create) {
        QSaveFile marker(identityPath);
        const auto bytes = identity_.toUtf8() + '\n';
        if (!marker.open(QIODevice::WriteOnly) || marker.write(bytes) != bytes.size() || !marker.commit())
            throw Error("Cannot save library identity. Metadata was retained in .virgil for inspection.");
        makeDirectory(paths::inside(root_, ".virgil/staging", true));
    }
    ensureAvailable();
    recoverImports();
    recoverOperations();
    automaticBackup();
}

void Library::ensureAvailable() const
{
    if (paths::rootDirectory(root_) != root_)
        throw Error("The library path has changed. Reopen the correct directory.");
    const QString db = paths::inside(root_, ".virgil/library.sqlite", true);
    const QString marker = paths::inside(root_, ".virgil/library.id", true);
    if (!QFileInfo(db).isFile() || !QFileInfo(db).isReadable() || readIdentity(marker) != identity_)
        throw Error("The original library is unavailable or a different directory is mounted here. "
                    "Reconnect it or use Open library. Saved metadata has been retained.");
    for (const auto& suffix : {"-journal", "-wal", "-shm"})
        (void)paths::inside(root_, ".virgil/library.sqlite" + QString(suffix), true);
}

Snapshot Library::snapshot() const
{
    ensureAvailable();
    Snapshot result = database_->records();
    result.folders.removeIf([](const Folder& folder) { return !folder.available || folder.path.startsWith(".virgil/"); });
    result.root = root_;
    result.notices = notices_;
    try {
        const QString backupDirectory = paths::inside(root_, ".virgil/backups", true);
        if (QFileInfo(backupDirectory).isDir()) {
            result.backups = QDir(backupDirectory).entryList({"automatic-*.sqlite"}, QDir::Files | QDir::NoSymLinks, QDir::Name);
            result.backups.removeIf([](const QString& name) { return !ownedBackupName(name); });
        }
    } catch (const Error& error) { result.notices.append("Backup directory unavailable: " + QString::fromUtf8(error.what())); }
    if (!result.operations.isEmpty()) result.notices.append("Interrupted operations need review. Open Library → Review interrupted operations.");
    return result;
}

Material Library::describe(const QString& relativePath, const QList<Course>& courses) const
{
    const QFileInfo info(paths::inside(root_, relativePath));
    Material result{uuid(), {}, info.completeBaseName(), relativePath, info.size(), info.lastModified().toUTC(), true, paths::identity(info.absoluteFilePath())};
    if (result.title.isEmpty())
        result.title = info.fileName();
    for (const auto& course : courses) {
        if (relativePath.startsWith(course.path + '/')) {
            result.courseId = course.id;
            break;
        }
    }
    return result;
}

Snapshot Library::refresh(const Work& work, const QStringList& changedDirectories)
{
    ensureAvailable();
    ensureNoPendingMove();
    work.checkpoint();
    const auto previous = database_->records();
    const auto courses = previous.courses;
    QList<Material> discovered;
    QList<Folder> folders;
    QStringList roots;
    for (const auto& changed : changedDirectories) {
        if (!paths::isWithin(root_, changed)) continue;
        QString directory = changed;
        while (directory != root_ && !QFileInfo(directory).isDir()) directory = QFileInfo(directory).dir().absolutePath();
        if (directory != root_) (void)paths::relative(root_, directory);
        roots.append(directory);
    }
    roots.removeDuplicates();
    if (roots.isEmpty() || roots.contains(root_)) roots = {root_};
    QStringList minimal;
    for (const auto& candidate : roots) {
        bool nested = false;
        for (const auto& other : roots) if (candidate != other && candidate.startsWith(other + '/')) nested = true;
        if (!nested) minimal.append(candidate);
    }
    auto affected = [&](const QString& relative) {
        work.checkpoint();
        paths::validateRelative(relative, relative.startsWith(".virgil/"));
        const QString absolute = QDir(root_).filePath(relative);
        for (const auto& scope : minimal) if (absolute == scope || absolute.startsWith(scope + '/')) return true;
        return false;
    };
    for (const auto& m : previous.materials) if (m.available && !affected(m.path)) discovered.append(m);
    for (const auto& f : previous.folders) if (f.available && !f.path.startsWith(".virgil/") && !affected(f.path)) folders.append(f);
    QStringList directories = minimal;
    for (const auto& directory : minimal) if (directory != root_) folders.append({paths::relative(root_, directory), paths::identity(directory)});
    qsizetype skippedLinks = 0;
    QElapsedTimer updateTimer;
    updateTimer.start();
    work.report("Scanning filenames and file information…");
    while (!directories.isEmpty()) {
        work.checkpoint();
        const QString directory = directories.takeLast();
        if (directory != root_)
            (void)paths::inside(root_, paths::relative(root_, directory));
        if (!QFileInfo(directory).isReadable())
            throw Error("Scan incomplete: directory is unreadable. Previous metadata was kept: " + directory);
        std::error_code error;
        std::filesystem::directory_iterator it(paths::native(directory), error), end;
        if (error)
            throw Error("Scan incomplete; previous metadata was kept: " + directory + "\n" + QString::fromStdString(error.message()));
        while (it != end) {
            work.checkpoint();
            const QString absolute = paths::fromNative(it->path());
            const QFileInfo info(absolute);
            if (info.fileName().compare(".virgil", Qt::CaseInsensitive) == 0) {
                // Metadata, staging, caches, future backups/trash are never materials.
            } else if (info.isSymLink()) {
                ++skippedLinks;
            } else if (info.isDir()) {
                directories.append(absolute);
                folders.append({paths::relative(root_, absolute), paths::identity(absolute)});
            } else if (info.isFile()) {
                discovered.append(describe(paths::relative(root_, absolute), courses));
            }
            it.increment(error);
            if (error)
                throw Error("Scan interrupted by a filesystem error; previous metadata was kept. Refresh again.");
            if (updateTimer.elapsed() > 150) {
                work.report(QString("Scanned %1 files…").arg(discovered.size()));
                updateTimer.restart();
            }
        }
    }
    work.checkpoint();
    ensureAvailable(); // An unplugged root must never turn into a mass deletion.
    work.report("Saving file information…");
    {
        Transaction transaction(*database_);
        database_->reconcileFolders(folders, [&] { work.checkpoint(); });
        work.checkpoint();
        const auto updatedCourses = database_->records().courses;
        for (auto& material : discovered) {
            material.courseId.clear();
            for (const auto& course : updatedCourses)
                if (material.path.startsWith(course.path + '/')) { material.courseId = course.id; break; }
        }
        // One transaction covers folder identity, course paths and file reconciliation.
        database_->reconcile(discovered, [&] { work.checkpoint(); ensureAvailable(); }, false);
        transaction.commit();
    }
    automaticBackup();
    Snapshot result = snapshot();
    if (skippedLinks)
        result.notices.append(QString("%1 symbolic link(s) excluded. Choose real files/directories; links are not followed.").arg(skippedLinks));
    return result;
}

QString Library::createSemester(const QString& name, const QString& season, QDate start, QDate end)
{
    ensureAvailable();
    ensureNoPendingMove();
    paths::validateName(name);
    if ((season != "winter" && season != "summer") || !start.isValid() || !end.isValid() || end < start)
        throw Error("Choose winter or summer and a valid date range with the end on or after the start.");
    if (paths::nameExists(root_, name))
        throw Error("A file or folder with this name already exists (possibly with different capitalization). Choose another name.");
    const Semester semester{uuid(), name, season, start, end, name};
    Transaction transaction(*database_);
    makeDirectory(paths::inside(root_, semester.path));
    database_->insert(semester);
    transaction.commit();
    automaticBackup(true);
    return semester.id;
}

QString Library::createCourse(const QString& semesterId, const QString& name, const QString& code, const QString& color)
{
    ensureAvailable();
    ensureNoPendingMove();
    paths::validateName(name);
    if (!courseColors.contains(color))
        throw Error("Choose one of the supported course colors.");
    const auto semesters = database_->records().semesters;
    const auto semester = std::find_if(semesters.begin(), semesters.end(), [&](const Semester& item) { return item.id == semesterId; });
    if (semester == semesters.end())
        throw Error("Select a saved semester before creating a course.");
    const QString parent = paths::inside(root_, semester->path);
    if (!QFileInfo(parent).isDir())
        throw Error("The semester folder is missing. Restore it to its recorded path before adding a course.");
    if (paths::nameExists(parent, name))
        throw Error("A file or folder already uses this course name. Choose another name; existing files will not be reorganized.");
    const Course course{uuid(), semesterId, name, code, color, semester->path + '/' + name};
    Transaction transaction(*database_);
    const QString directory = paths::inside(root_, course.path);
    makeDirectory(directory);
    for (const auto& folder : {"Lectures", "Books", "Past Exams", "Other Materials"})
        makeDirectory(QDir(directory).filePath(folder));
    database_->insert(course);
    transaction.commit();
    automaticBackup(true);
    return course.id;
}

QString Library::importFile(const QString& source, const QString& destinationDirectory, const QString& filename, const Work& work)
{
    ensureAvailable();
    ensureNoPendingMove();
    work.checkpoint();
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.isFile() || sourceInfo.isSymLink() || !sourceInfo.isReadable())
        throw Error("Choose a readable regular file. Directories and symbolic links cannot be imported.");
    const auto courses = database_->records().courses;
    if (paths::isWithin(root_, sourceInfo.absoluteFilePath())) {
        const QString existing = paths::relative(root_, sourceInfo.absoluteFilePath());
        database_->saveMaterial(describe(existing, courses));
        automaticBackup(true);
        return "This file is already inside the library. Its location was kept; no duplicate was created.";
    }
    paths::validateName(filename);
    const QString directory = destinationDirectory.isEmpty() ? root_ : paths::inside(root_, destinationDirectory);
    if (!QFileInfo(directory).isDir() || !QFileInfo(directory).isWritable())
        throw Error("Import destination is missing or unwritable: " + directory);
    if (paths::nameExists(directory, filename))
        throw Error("A destination with this filename already exists. Import again with another filename; nothing was overwritten.");
    const QString destination = destinationDirectory.isEmpty() ? filename : destinationDirectory + '/' + filename;
    const QString absoluteDestination = paths::inside(root_, destination);
    const QString id = uuid();
    const QString staging = ".virgil/staging/" + id + ".part";
    const QString absoluteStaging = paths::inside(root_, staging, true);
    database_->query("INSERT INTO pending_imports VALUES (?,?,?,'copying')", {id, destination, staging});
    // The journal is durable before we write any bytes. If the process is killed,
    // startup can identify our staging file without touching an original.
    QFile input(sourceInfo.absoluteFilePath());
    QFile output(absoluteStaging);
    bool stagingCreated = false;
    bool finalized = false;
    try {
        if (!input.open(QIODevice::ReadOnly))
            throw Error("Cannot read source file: " + input.errorString());
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            throw Error("Cannot create import staging file: " + output.errorString());
        stagingCreated = true;
        const qint64 expected = input.size();
        qint64 copied = 0;
        QByteArray buffer(1024 * 1024, Qt::Uninitialized);
        QElapsedTimer updateTimer;
        updateTimer.start();
        work.report("Copying " + filename + "…", 0);
        while (true) {
            work.checkpoint();
            const qint64 count = input.read(buffer.data(), buffer.size());
            if (count < 0)
                throw Error("Reading source failed: " + input.errorString());
            if (count == 0)
                break;
            if (output.write(buffer.constData(), count) != count)
                throw Error("Writing the copy failed (check free disk space): " + output.errorString());
            copied += count;
            if (updateTimer.elapsed() > 100) {
                const int percent = expected > 0 ? static_cast<int>(100.0 * static_cast<double>(copied) / static_cast<double>(expected)) : 0;
                work.report("Copying " + filename + "…", std::clamp(percent, 0, 99));
                updateTimer.restart();
            }
        }
        const QFileInfo after(sourceInfo.absoluteFilePath());
        if (copied != expected || after.size() != sourceInfo.size() || after.lastModified() != sourceInfo.lastModified())
            throw Error("The source changed while it was being copied. Wait for its writer to finish and import again.");
        if (!output.flush())
            throw Error("Cannot flush the staged copy: " + output.errorString());
        output.close();
        work.checkpoint();
        ensureAvailable();
        (void)paths::inside(root_, destination);
        database_->query("UPDATE pending_imports SET state='ready' WHERE id=?", {id});
        paths::finalizeWithoutOverwrite(absoluteStaging, absoluteDestination);
        finalized = true;
        {
            Transaction transaction(*database_);
            database_->saveMaterial(describe(destination, courses));
            database_->query("DELETE FROM pending_imports WHERE id=?", {id});
            transaction.commit();
        }
        if (!QFile::remove(absoluteStaging))
            notices_.append("An unused staging copy remains at " + staging + ". Your imported material is saved.");
        automaticBackup(true);
        work.report("Import complete", 100);
        return "Copied into the library. The source file was kept.";
    } catch (...) {
        output.close();
        // Once finalized, keep the journal and staging link for startup recovery.
        // Before finalization, remove only this operation's own partial file.
        if (!finalized) {
            const bool removed = !stagingCreated || !QFileInfo::exists(absoluteStaging) || QFile::remove(absoluteStaging);
            if (removed) {
                try { database_->query("DELETE FROM pending_imports WHERE id=?", {id}); }
                catch (const Error&) { qWarning("Import journal cleanup failed; startup will report the pending operation."); }
            }
        }
        throw;
    }
}

void Library::recoverImports()
{
    struct Pending { QString id; QString destination; QString stage; QString state; };
    QList<Pending> pending;
    {
        auto query = database_->query("SELECT id,destination,stage,state FROM pending_imports");
        while (query.next())
            pending.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(), query.value(3).toString()});
    }
    const auto courses = database_->records().courses;
    for (const auto& item : pending) {
        // Never trust a journal's arbitrary pathname as a deletion instruction.
        if (QUuid(item.id).isNull() || item.stage != ".virgil/staging/" + item.id + ".part")
            throw Error("Invalid import recovery record. Keep .virgil intact for inspection.");
        const QString stage = paths::inside(root_, item.stage, true);
        const QString destination = paths::inside(root_, item.destination);
        if (item.state == "ready" && QFileInfo(stage).isFile() && sameFile(stage, destination)) {
            Transaction transaction(*database_);
            database_->saveMaterial(describe(item.destination, courses));
            database_->query("DELETE FROM pending_imports WHERE id=?", {item.id});
            transaction.commit();
            if (!QFile::remove(stage))
                notices_.append("Recovered an import; its redundant staging link could not be removed: " + item.stage);
            else
                notices_.append("Recovered a completed import after interruption: " + item.destination);
        } else {
            // A partial or ambiguous copy is retained, never guessed into place.
            notices_.append("Interrupted import retained at " + item.stage + ". Its source was not removed. "
                            "Import the original again; see docs/data-safety.md for reviewing staging files.");
        }
    }
}

QString Library::materialPath(const QString& id) const
{
    ensureAvailable();
    const auto materials = database_->records().materials;
    for (const auto& material : materials) {
        if (material.id == id) {
            const QString path = paths::inside(root_, material.path);
            if (!QFileInfo(path).isFile() || !QFileInfo(path).isReadable())
                throw Error("This material is missing or unreadable. Its metadata is retained. Restore it to "
                            + material.path + " and Refresh. or use Relink missing file.");
            return path;
        }
    }
    throw Error("Select a material that is saved in this library.");
}

QString Library::containingFolder(const QString& id) const
{
    return QFileInfo(materialPath(id)).absolutePath();
}

void Library::createFolder(const QString& parent, const QString& name)
{
    ensureAvailable();
    ensureNoPendingMove();
    paths::validateName(name);
    const QString directory = parent.isEmpty() ? root_ : paths::inside(root_, parent);
    if (!QFileInfo(directory).isDir() || paths::nameExists(directory, name))
        throw Error("Parent is missing or a name already exists (including case-only names).");
    makeDirectory(QDir(directory).filePath(name));
}

void Library::finishOperation(const QString& id, const QString& source, const QString& destination,
                              const QString& identity, const QString& kind)
{
    const bool hiddenSource = kind == "restore";
    const bool hiddenDestination = kind == "trash";
    const QString from = paths::inside(root_, source, hiddenSource);
    const QString to = paths::inside(root_, destination, hiddenDestination);
    if (identity.isEmpty() || paths::identity(to) != identity || QFileInfo::exists(from))
        throw Error("Move outcome is ambiguous. Files and journal retained for review.");
    Transaction transaction(*database_);
    database_->relocate(source, destination);
    if (kind == "trash") {
        database_->query("INSERT INTO trash VALUES (?,?,?,?)", {id, source, destination, identity});
        // Course organization stays saved until explicitly removed; never delete it as a side effect.
        for (const auto& material : database_->records().materials)
            if (material.path == destination || material.path.startsWith(destination + '/'))
                database_->query("UPDATE materials SET available=0 WHERE id=?", {material.id});
    } else if (kind == "restore") {
        database_->query("DELETE FROM trash WHERE destination=?", {source});
    }
    database_->query("DELETE FROM operations WHERE id=?", {id});
    transaction.commit();
    automaticBackup(true);
}

void Library::movePath(const QString& source, const QString& destination)
{
    ensureAvailable();
    ensureNoPendingMove();
    const QString from = paths::inside(root_, source), to = paths::inside(root_, destination);
    paths::validateName(QFileInfo(destination).fileName());
    if (destination == source || destination.startsWith(source + '/'))
        throw Error("Choose a different destination outside the source folder.");
    if (paths::nameExists(QFileInfo(to).dir().absolutePath(), QFileInfo(to).fileName()))
        throw Error("Destination already exists, including a case-only collision. Rename through a distinct temporary name.");
    // Metadata-only missing rows also reserve their paths.
    const auto saved = database_->records();
    for (const auto& m : saved.materials)
        if (m.path == destination || m.path.startsWith(destination + '/')) throw Error("Destination has retained metadata. Relink or choose another path.");
    for (const auto& c : saved.courses)
        if (c.path == destination || c.path.startsWith(destination + '/')) throw Error("Destination has saved course organization.");
    if (!QFileInfo(from).isFile() && !QFileInfo(from).isDir()) throw Error("Choose a regular file or directory.");
    const QString identity = paths::identity(from);
    if (identity.isEmpty()) throw Error("This filesystem cannot provide a durable move identity. No files changed.");
    const QString id = uuid();
    database_->query("INSERT INTO operations VALUES (?,?,?,?,?)", {id, source, destination, identity, "move"});
    paths::renameWithoutOverwrite(from, to);
    finishOperation(id, source, destination, identity, "move");
}

void Library::trashPath(const QString& source)
{
    ensureAvailable();
    ensureNoPendingMove();
    const QString from = paths::inside(root_, source);
    const QString identity = paths::identity(from);
    if (identity.isEmpty()) throw Error("Cannot identify this file/folder safely. No files changed.");
    const QString trashRoot = paths::inside(root_, ".virgil/trash", true);
    if (!QFileInfo::exists(trashRoot)) makeDirectory(trashRoot);
    const QString id = uuid(), destination = ".virgil/trash/" + id;
    database_->query("INSERT INTO operations VALUES (?,?,?,?,?)", {id, source, destination, identity, "trash"});
    paths::renameWithoutOverwrite(from, paths::inside(root_, destination, true));
    finishOperation(id, source, destination, identity, "trash");
}

void Library::restoreTrash(const QString& id)
{
    ensureAvailable();
    ensureNoPendingMove();
    auto item = database_->query("SELECT source,destination,identity FROM trash WHERE id=?", {id});
    if (!item.next()) throw Error("Trash entry no longer exists.");
    const QString destination = item.value(0).toString(), source = item.value(1).toString(), identity = item.value(2).toString();
    if (source != ".virgil/trash/" + id || QUuid(id).isNull()) throw Error("Invalid trash record. Nothing changed.");
    const QString from = paths::inside(root_, source, true), to = paths::inside(root_, destination);
    if (paths::identity(from) != identity || paths::nameExists(QFileInfo(to).dir().absolutePath(), QFileInfo(to).fileName()))
        throw Error("Trash changed or original location is occupied. Restore refused; nothing replaced.");
    const QString operation = uuid();
    database_->query("INSERT INTO operations VALUES (?,?,?,?,?)", {operation, source, destination, identity, "restore"});
    paths::renameWithoutOverwrite(from, to);
    finishOperation(operation, source, destination, identity, "restore");
}

void Library::recoverOperations()
{
    const auto pending = database_->records().operations;
    for (const auto& item : pending) {
        if (item.state.startsWith("import:")) continue;
        try { reviewOperation(item.id, false); }
        catch (const Error& e) { notices_.append(QString::fromUtf8(e.what())); }
    }
}

void Library::reviewOperation(const QString& id, bool retry)
{
    ensureAvailable();
    auto row = database_->query("SELECT source,destination,identity,kind FROM operations WHERE id=?", {id});
    if (!row.next()) throw Error("Import copies remain for manual inspection; open their staging folder and consult data-safety.md.");
    const QString source = row.value(0).toString(), destination = row.value(1).toString();
    const QString identity = row.value(2).toString(), kind = row.value(3).toString();
    if (QUuid(id).isNull() || (kind != "move" && kind != "trash" && kind != "restore")
        || (kind == "trash" && destination != ".virgil/trash/" + id)
        || (kind == "restore" && !source.startsWith(".virgil/trash/")))
        throw Error("Invalid operation journal. Nothing changed.");
    if (kind == "restore") {
        const QString trashId = source.mid(QString(".virgil/trash/").size());
        if (QUuid(trashId).isNull() || source != ".virgil/trash/" + trashId) throw Error("Invalid restore journal.");
        auto saved = database_->query("SELECT source,identity FROM trash WHERE id=?", {trashId});
        if (!saved.next() || saved.value(0).toString() != destination || saved.value(1).toString() != identity)
            throw Error("Restore journal does not match its trash entry. Nothing changed.");
    }
    const QString from = paths::inside(root_, source, kind == "restore");
    const QString to = paths::inside(root_, destination, kind == "trash");
    if (!QFileInfo::exists(from) && paths::identity(to) == identity) {
        finishOperation(id, source, destination, identity, kind);
        notices_.append("Recovered interrupted " + kind + ": " + destination);
    } else if (paths::identity(from) == identity && !QFileInfo::exists(to)) {
        if (retry) {
            if (paths::nameExists(QFileInfo(to).dir().absolutePath(), QFileInfo(to).fileName())) throw Error("Destination collision.");
            paths::renameWithoutOverwrite(from, to);
            finishOperation(id, source, destination, identity, kind);
        } else {
            // Proven unstarted operation can be abandoned without touching any files.
            database_->query("DELETE FROM operations WHERE id=?", {id});
        }
    } else throw Error("Interrupted move needs review: " + source + " → " + destination + ". Neither file was removed.");
}

void Library::removeCourse(const QString& id)
{
    ensureAvailable();
    ensureNoPendingMove();
    Transaction transaction(*database_);
    auto course = database_->query("SELECT id FROM courses WHERE id=?", {id});
    if (!course.next()) throw Error("This course no longer exists. Refresh and select another course.");
    course.finish();
    database_->query("UPDATE materials SET course_id=NULL WHERE course_id=?", {id});
    database_->query("DELETE FROM courses WHERE id=?", {id});
    transaction.commit(); // Files and detached resource records stay intact, even when the folder is missing.
    automaticBackup(true);
}

void Library::relink(const QString& missingId, const QString& replacementId)
{
    ensureAvailable();
    ensureNoPendingMove();
    const auto saved = database_->records().materials;
    Material missing, replacement;
    for (const auto& m : saved) { if (m.id == missingId) missing = m; if (m.id == replacementId) replacement = m; }
    if (missing.id.isEmpty() || replacement.id.isEmpty() || missing.available || !replacement.available
        || missing.path.startsWith(".virgil/")) throw Error("Choose a missing record and an available replacement outside trash.");
    if (QFileInfo::exists(paths::inside(root_, missing.path))) throw Error("Original file has returned. Refresh first.");
    (void)materialPath(replacementId);
    Transaction transaction(*database_);
    database_->query("UPDATE resources SET material_id=? WHERE material_id=?", {missingId, replacementId});
    database_->query("DELETE FROM materials WHERE id=?", {replacementId});
    database_->query("UPDATE materials SET path=?,course_id=?,size=?,modified_utc=?,identity=?,available=1 WHERE id=?",
        {replacement.path, replacement.courseId.isEmpty() ? QVariant() : QVariant(replacement.courseId), replacement.size,
         replacement.modified.toUTC().toString(Qt::ISODateWithMs), replacement.identity, missingId});
    transaction.commit(); // Only an explicit choice joins identities; never matching file contents.
    automaticBackup(true);
}

void Library::ensureNoPendingMove() const
{
    auto pending = database_->query("SELECT id FROM operations LIMIT 1");
    if (pending.next()) throw Error("Review the interrupted move before starting another move or trash operation.");
}

namespace {
QVariant optionalText(const QString& value) { return value.isEmpty() ? QVariant() : QVariant(value); }
QVariant optionalDate(QDate value) { return value.isValid() ? QVariant(value.toString(Qt::ISODate)) : QVariant(); }
QString text(const QString& value) { return value.isNull() ? QString("") : value; }
void requireTitle(const QString& title)
{
    if (title.trimmed().isEmpty() || title.contains(QChar::Null)) throw Error("Enter a nonempty title.");
}
}

void Library::editSemester(const Semester& semester)
{
    ensureAvailable(); ensureNoPendingMove();
    requireTitle(semester.name);
    if ((semester.season != "winter" && semester.season != "summer") || !semester.start.isValid()
        || !semester.end.isValid() || semester.end < semester.start) throw Error("Choose a valid semester date range and season.");
    auto row = database_->query("SELECT id FROM semesters WHERE id=?", {semester.id});
    if (!row.next()) throw Error("Semester no longer exists.");
    row.finish();
    if (semester.teachingStart.isValid() != semester.teachingEnd.isValid()
        || (semester.teachingStart.isValid() && (semester.teachingStart < semester.start
            || semester.teachingEnd > semester.end || semester.teachingEnd < semester.teachingStart)))
        throw Error("Custom teaching dates must both be valid and inside the semester.");
    for (const auto& range : database_->records().examRanges)
        if (range.semesterId == semester.id && (range.start < semester.start || range.end > semester.end))
            throw Error("Semester dates must include all existing exam ranges. Edit the ranges first.");
    database_->query("UPDATE semesters SET name=?,season=?,start_date=?,end_date=?,teaching_start=?,teaching_end=? WHERE id=?",
        {semester.name.trimmed(), semester.season, semester.start.toString(Qt::ISODate), semester.end.toString(Qt::ISODate),
         optionalDate(semester.teachingStart), optionalDate(semester.teachingEnd), semester.id});
    automaticBackup(true);
}

void Library::editCourse(const Course& course)
{
    ensureAvailable(); ensureNoPendingMove(); requireTitle(course.name);
    if (!courseColors.contains(course.color)) throw Error("Choose a supported course color.");
    auto row = database_->query("SELECT id FROM courses WHERE id=?", {course.id});
    if (!row.next()) throw Error("Course no longer exists.");
    row.finish();
    {
        Transaction transaction(*database_);
        database_->query("UPDATE courses SET semester_id=?,name=?,code=?,color=? WHERE id=?",
            {course.semesterId, course.name.trimmed(), text(course.code), course.color, course.id});
        database_->query("UPDATE calendar_events SET semester_id=? WHERE course_id=?", {course.semesterId, course.id});
        database_->query("UPDATE weekly_classes SET semester_id=? WHERE course_id=?", {course.semesterId, course.id});
        transaction.commit();
    }
    automaticBackup(true); // IDs and ordinary folder paths are never rebuilt from display names.
}

void Library::editMaterial(const QString& id, const QString& title, const QString& category, QDate examDate)
{
    ensureAvailable(); ensureNoPendingMove(); requireTitle(title);
    if (!materialCategories.contains(category)) throw Error("Choose a supported material category.");
    auto row = database_->query("SELECT id FROM materials WHERE id=?", {id});
    if (!row.next()) throw Error("Material no longer exists.");
    row.finish();
    database_->query("UPDATE materials SET title=?,category=?,exam_date=? WHERE id=?", {title.trimmed(), category, optionalDate(examDate), id});
    automaticBackup(true);
}

void Library::validateResource(const Resource& resource, bool pendingAttachment) const
{
    ensureAvailable(); ensureNoPendingMove(); requireTitle(resource.title);
    if (!QStringList{"book", "lecture", "bookmark"}.contains(resource.kind) || !materialCategories.contains(resource.category))
        throw Error("Choose a supported resource type and category.");
    if (!resource.courseId.isEmpty()) {
        auto course = database_->query("SELECT id FROM courses WHERE id=?", {resource.courseId});
        if (!course.next()) throw Error("Select a saved course.");
    }
    if (!resource.materialId.isEmpty()) {
        auto material = database_->query("SELECT id FROM materials WHERE id=?", {resource.materialId});
        if (!material.next()) throw Error("Select a saved local material. Import it first if necessary.");
    }
    if (resource.kind == "book" && resource.materialId.isEmpty() && !pendingAttachment) throw Error("A book needs a local material attachment.");
    if (resource.kind == "lecture" && !resource.date.isValid()) throw Error("A lecture needs a valid date.");
    if (resource.kind == "bookmark") {
        const QUrl url(resource.url, QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty()
            || !url.userInfo().isEmpty()) throw Error("Use an absolute http/https bookmark URL without credentials.");
        if (!resource.folder.isEmpty()) {
            bool retainedTrashScope = false;
            if (resource.folder.startsWith(".virgil/trash/") && !resource.id.isEmpty()) {
                auto previous = database_->query("SELECT folder FROM resources WHERE id=?", {resource.id});
                retainedTrashScope = previous.next() && previous.value(0).toString() == resource.folder;
            }
            (void)paths::inside(root_, resource.folder, retainedTrashScope);
        }
    }
    if (!resource.id.isEmpty()) {
        auto existing = database_->query("SELECT kind FROM resources WHERE id=?", {resource.id});
        if (!existing.next() || existing.value(0).toString() != resource.kind) throw Error("Resource no longer exists or its type changed.");
    }
}

QString Library::saveResource(Resource resource)
{
    validateResource(resource);
    if (resource.kind == "bookmark") resource.materialId.clear();
    else { resource.url.clear(); resource.folder.clear(); }
    if (resource.id.isEmpty()) resource.id = uuid();
    database_->query("INSERT INTO resources VALUES (?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
        "course_id=excluded.course_id,title=excluded.title,material_id=excluded.material_id,date=excluded.date,"
        "notes=excluded.notes,url=excluded.url,folder=excluded.folder,category=excluded.category,exam_date=excluded.exam_date",
        {resource.id, optionalText(resource.courseId), resource.kind, resource.title.trimmed(), optionalText(resource.materialId),
         optionalDate(resource.date), text(resource.notes), text(resource.url), text(resource.folder), resource.category, optionalDate(resource.examDate)});
    automaticBackup(true);
    return resource.id;
}

QString Library::saveResourceWithFile(Resource resource, const QString& source, const Work& work)
{
    resource.materialId.clear();
    validateResource(resource, true); // Validate the form before importing any bytes.
    if (resource.kind != "book" && resource.kind != "lecture")
        throw Error("Only books and lectures support local attachments.");
    QString folder;
    for (const auto& course : database_->records().courses)
        if (course.id == resource.courseId) folder = course.path + (resource.kind == "book" ? "/Books" : "/Lectures");
    const QString absoluteSource = QFileInfo(source).absoluteFilePath();
    const QString relativePath = paths::isWithin(root_, absoluteSource)
        ? paths::relative(root_, absoluteSource)
        : (folder.isEmpty() ? QString() : folder + '/') + QFileInfo(source).fileName();
    importFile(source, folder, QFileInfo(source).fileName(), work);
    for (const auto& material : database_->records().materials)
        if (material.path == relativePath) resource.materialId = material.id;
    try { return saveResource(resource); }
    catch (const Error& error) {
        throw Error("The file is kept in Materials, but the resource record could not be saved: " + QString::fromUtf8(error.what()));
    }
}

void Library::removeResource(const QString& id)
{
    ensureAvailable(); ensureNoPendingMove();
    database_->query("DELETE FROM resources WHERE id=?", {id});
    automaticBackup(true); // Only the record is deleted. Its material remains an ordinary file.
}

void Library::backupMetadata()
{
    ensureAvailable(); ensureNoPendingMove();
    const QString directory = paths::inside(root_, ".virgil/backups", true);
    if (!QFileInfo::exists(directory)) makeDirectory(directory);
    const QString filename = "automatic-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz") + '-' + uuid() + ".sqlite";
    database_->createSnapshot(paths::inside(root_, ".virgil/backups/" + filename, true));
    auto backups = QDir(directory).entryList({"automatic-*.sqlite"}, QDir::Files | QDir::NoSymLinks, QDir::Name);
    backups.removeIf([](const QString& name) { return !ownedBackupName(name); });
    qsizetype remaining = backups.size();
    for (const auto& backup : backups) {
        if (remaining <= 7) break;
        const QString path = paths::inside(root_, ".virgil/backups/" + backup, true);
        database_->verifySnapshot(path);
        if (!QFile::remove(path)) throw Error("Backup saved, but an older metadata backup could not be rotated.");
        --remaining;
    }
}

void Library::automaticBackup(bool changed)
{
    try {
        const auto backups = QDir(paths::inside(root_, ".virgil/backups", true)).entryList({"automatic-*.sqlite"}, QDir::Files | QDir::NoSymLinks, QDir::Name);
        const QString today = "automatic-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + '-';
        bool current = false;
        for (const auto& backup : backups) if (ownedBackupName(backup) && backup.startsWith(today)) current = true;
        if (changed || !current) backupMetadata();
    } catch (const std::exception& error) {
        const QString notice = "Automatic metadata backup failed; your saved library was kept: " + QString::fromUtf8(error.what());
        if (!notices_.contains(notice)) notices_.append(notice);
    }
}

} // namespace virgil
