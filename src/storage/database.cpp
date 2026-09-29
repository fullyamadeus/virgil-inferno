#include "storage/database.h"
#include "domain/error.h"
#include "services/paths.h"
#include <QFile>

#include <QFileInfo>
#include <QDir>
#include <QSqlError>
#include <QUuid>
#include <QSet>
#include <QHash>
#include <memory>

namespace virgil {
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

Database::Database(const QString& filename, bool create, const QString& expectedIdentity) : connection_("virgil-" + uuid())
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE"))
        throw Error("Qt's SQLite driver is missing. Install the Fedora qt6-qtbase runtime package.");
    if (!create && !QFileInfo(filename).isFile())
        throw Error("Library metadata is missing. Nothing was reset. Reconnect the original library or restore a backup.");
    try {
        {
            auto database = QSqlDatabase::addDatabase("QSQLITE", connection_);
            database.setDatabaseName(filename);
            database.setConnectOptions("QSQLITE_BUSY_TIMEOUT=3000");
            if (!database.open())
                throw Error("Cannot open library metadata: " + database.lastError().text());
        }
        initialize(create, expectedIdentity);
    } catch (...) {
        QSqlDatabase::removeDatabase(connection_);
        throw;
    }
}

Database::~Database()
{
    // No queries or copied database handles survive this object.
    { auto database = QSqlDatabase::database(connection_, false); database.close(); }
    QSqlDatabase::removeDatabase(connection_);
}

QSqlQuery Database::query(const QString& sql, const QVariantList& values) const
{
    QSqlQuery result(QSqlDatabase::database(connection_, false));
    if (!result.prepare(sql))
        throw Error("Cannot prepare a metadata operation: " + result.lastError().text());
    for (const auto& value : values)
        result.addBindValue(value);
    if (!result.exec())
        throw Error("Metadata operation failed; no database was reset: " + result.lastError().text());
    return result;
}

void Database::begin()
{
    if (!QSqlDatabase::database(connection_, false).transaction())
        throw Error("Cannot begin a metadata transaction.");
}

void Database::commit()
{
    auto database = QSqlDatabase::database(connection_, false);
    if (!database.commit())
        throw Error("Cannot commit metadata: " + database.lastError().text());
}

void Database::rollback() noexcept
{
    if (!QSqlDatabase::database(connection_, false).rollback())
        qWarning("Metadata rollback failed. Close the library and inspect its filesystem permissions.");
}

void Database::initialize(bool create, const QString& expectedIdentity)
{
    query("PRAGMA foreign_keys = ON");
    auto versionQuery = query("PRAGMA user_version");
    versionQuery.next();
    int version = versionQuery.value(0).toInt();
    versionQuery.finish();
    auto appQuery = query("PRAGMA application_id");
    appQuery.next();
    const int appId = appQuery.value(0).toInt();
    appQuery.finish();
    if (!create && ((version < 1 || version > schemaVersion) || appId != applicationId))
        throw Error(QString("Unsupported or unrecognized library schema (%1). This app supports version %2. "
                            "Nothing was reset; use a compatible Virgil version or a separate restored copy.")
                        .arg(version).arg(schemaVersion));
    if (!create && !expectedIdentity.isEmpty() && libraryId() != expectedIdentity)
        throw Error("Library identity does not match its marker. Nothing migrated or reset.");
    if (create) {
        if (version != 0 || !QSqlDatabase::database(connection_, false).tables().isEmpty())
            throw Error("Refusing to initialize a database that already contains information.");
        // Migration 001 bootstraps a NEW database only. Future migrations must
        // snapshot existing metadata with SQLite VACUUM INTO before changes.
        Transaction transaction(*this);
        query("CREATE TABLE library (id TEXT PRIMARY KEY, created_utc TEXT NOT NULL)");
        query("CREATE TABLE semesters (id TEXT PRIMARY KEY, name TEXT NOT NULL, "
              "season TEXT NOT NULL CHECK(season IN ('winter','summer')), start_date TEXT NOT NULL, "
              "end_date TEXT NOT NULL CHECK(end_date >= start_date), path TEXT NOT NULL UNIQUE)");
        query("CREATE TABLE courses (id TEXT PRIMARY KEY, semester_id TEXT NOT NULL REFERENCES semesters(id), "
              "name TEXT NOT NULL, code TEXT NOT NULL DEFAULT '', color TEXT NOT NULL, path TEXT NOT NULL UNIQUE)");
        query("CREATE TABLE materials (id TEXT PRIMARY KEY, course_id TEXT REFERENCES courses(id), "
              "title TEXT NOT NULL, path TEXT NOT NULL UNIQUE, size INTEGER NOT NULL, modified_utc TEXT NOT NULL, "
              "available INTEGER NOT NULL DEFAULT 1 CHECK(available IN (0,1)))");
        query("CREATE TABLE pending_imports (id TEXT PRIMARY KEY, destination TEXT NOT NULL, "
              "stage TEXT NOT NULL, state TEXT NOT NULL CHECK(state IN ('copying','ready')))");
        query("INSERT INTO library VALUES (?, ?)", {uuid(), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)});
        query(QString("PRAGMA application_id = %1").arg(applicationId));
        query("PRAGMA user_version = 1");
        transaction.commit();
    }
    {
        auto integrity = query("PRAGMA quick_check");
        if (!integrity.next() || integrity.value(0).toString() != "ok") throw Error("SQLite integrity check failed; nothing migrated.");
        auto relationships = query("PRAGMA foreign_key_check");
        if (relationships.next()) throw Error("Broken relationships; nothing migrated.");
    }
    if (version < 2) {
        if (!create) {
            const QString backup = QSqlDatabase::database(connection_, false).databaseName()
                + ".before-v2-" + uuid() + ".sqlite";
            createSnapshot(backup);
        }
        Transaction migration(*this);
        query("ALTER TABLE materials ADD COLUMN identity TEXT NOT NULL DEFAULT ''");
        query("CREATE TABLE folders (path TEXT PRIMARY KEY, identity TEXT NOT NULL, available INTEGER NOT NULL DEFAULT 1)");
        query("CREATE TABLE operations (id TEXT PRIMARY KEY, source TEXT NOT NULL, destination TEXT NOT NULL, identity TEXT NOT NULL, kind TEXT NOT NULL)");
        query("CREATE TABLE trash (id TEXT PRIMARY KEY, source TEXT NOT NULL, destination TEXT NOT NULL, identity TEXT NOT NULL)");
        query("PRAGMA user_version=2");
        migration.commit();
        version = 2;
    }
    if (version < 3) {
        if (!create) createSnapshot(QSqlDatabase::database(connection_, false).databaseName() + ".before-v3-" + uuid() + ".sqlite");
        Transaction migration(*this);
        query("ALTER TABLE materials ADD COLUMN category TEXT NOT NULL DEFAULT 'unclassified'");
        query("ALTER TABLE materials ADD COLUMN exam_date TEXT");
        query("CREATE TABLE resources (id TEXT PRIMARY KEY, course_id TEXT REFERENCES courses(id) ON DELETE SET NULL, "
              "kind TEXT NOT NULL CHECK(kind IN ('book','lecture','bookmark')), title TEXT NOT NULL, "
              "material_id TEXT REFERENCES materials(id) ON DELETE SET NULL, date TEXT, notes TEXT NOT NULL DEFAULT '', "
              "url TEXT NOT NULL DEFAULT '', folder TEXT NOT NULL DEFAULT '', category TEXT NOT NULL DEFAULT 'unclassified', exam_date TEXT)");
        query("PRAGMA user_version=3");
        migration.commit();
        version = 3;
    }
    if (version < 4) {
        if (!create) createSnapshot(QSqlDatabase::database(connection_, false).databaseName() + ".before-v4-" + uuid() + ".sqlite");
        Transaction migration(*this);
        query("ALTER TABLE semesters ADD COLUMN teaching_start TEXT");
        query("ALTER TABLE semesters ADD COLUMN teaching_end TEXT");
        query("CREATE TABLE calendar_settings (id INTEGER PRIMARY KEY CHECK(id=1), timezone TEXT NOT NULL)");
        query("INSERT INTO calendar_settings VALUES (1,'Europe/Skopje')");
        query("CREATE TABLE calendar_events (id TEXT PRIMARY KEY, semester_id TEXT REFERENCES semesters(id), "
              "course_id TEXT REFERENCES courses(id) ON DELETE SET NULL, title TEXT NOT NULL, "
              "kind TEXT NOT NULL CHECK(kind IN ('exam','assignment','review_session','lecture','lab')), "
              "all_day INTEGER NOT NULL CHECK(all_day IN (0,1)), on_date TEXT, starts_utc TEXT, ends_utc TEXT, "
              "timezone TEXT NOT NULL, completed INTEGER NOT NULL CHECK(completed IN (0,1)), "
              "CHECK((all_day=1 AND on_date IS NOT NULL AND starts_utc IS NULL AND ends_utc IS NULL) OR "
              "(all_day=0 AND on_date IS NULL AND starts_utc IS NOT NULL AND ends_utc IS NOT NULL AND ends_utc > starts_utc)), "
              "CHECK(completed=0 OR kind='assignment'))");
        query("CREATE TABLE weekly_classes (id TEXT PRIMARY KEY, semester_id TEXT NOT NULL REFERENCES semesters(id), "
              "course_id TEXT REFERENCES courses(id) ON DELETE SET NULL, title TEXT NOT NULL, "
              "kind TEXT NOT NULL CHECK(kind IN ('lecture','lab')), weekday INTEGER NOT NULL CHECK(weekday BETWEEN 1 AND 7), "
              "start_time TEXT NOT NULL, end_time TEXT NOT NULL CHECK(end_time > start_time), timezone TEXT NOT NULL, "
              "ambiguity TEXT NOT NULL CHECK(ambiguity IN ('reject','earlier','later')))");
        query("CREATE TABLE exam_ranges (id TEXT PRIMARY KEY, semester_id TEXT NOT NULL REFERENCES semesters(id), "
              "start_date TEXT NOT NULL, end_date TEXT NOT NULL CHECK(end_date >= start_date))");
        query("CREATE INDEX calendar_events_semester ON calendar_events(semester_id)");
        query("CREATE INDEX weekly_classes_semester ON weekly_classes(semester_id)");
        query("CREATE INDEX exam_ranges_semester ON exam_ranges(semester_id,start_date)");
        query("PRAGMA user_version=4");
        migration.commit();
    }
    query("PRAGMA synchronous = FULL");
    auto check = query("PRAGMA quick_check");
    if (!check.next() || check.value(0).toString() != "ok")
        throw Error("SQLite integrity check failed. Keep this library intact and restore a separate backup.");
    check.finish();
    auto foreign = query("PRAGMA foreign_key_check");
    if (foreign.next())
        throw Error("Library metadata contains broken relationships. Keep it intact for recovery.");
}

QString Database::libraryId() const
{
    auto result = query("SELECT id FROM library");
    if (!result.next())
        throw Error("Library identity is missing. Metadata has not been reset.");
    return result.value(0).toString();
}

Snapshot Database::records() const
{
    Snapshot result;
    result.libraryId = libraryId();
    auto semesters = query("SELECT id,name,season,start_date,end_date,path,teaching_start,teaching_end FROM semesters ORDER BY start_date DESC,name");
    while (semesters.next()) {
        result.semesters.append({semesters.value(0).toString(), semesters.value(1).toString(), semesters.value(2).toString(),
            QDate::fromString(semesters.value(3).toString(), Qt::ISODate),
            QDate::fromString(semesters.value(4).toString(), Qt::ISODate), semesters.value(5).toString(), QDate::fromString(semesters.value(6).toString(), Qt::ISODate),
            QDate::fromString(semesters.value(7).toString(), Qt::ISODate)});
    }
    auto courses = query("SELECT id,semester_id,name,code,color,path FROM courses ORDER BY name");
    while (courses.next()) {
        result.courses.append({courses.value(0).toString(), courses.value(1).toString(), courses.value(2).toString(),
            courses.value(3).toString(), courses.value(4).toString(), courses.value(5).toString()});
    }
    auto materials = query("SELECT id,course_id,title,path,size,modified_utc,available,identity,category,exam_date FROM materials ORDER BY path");
    while (materials.next()) {
        result.materials.append({materials.value(0).toString(), materials.value(1).toString(), materials.value(2).toString(),
            materials.value(3).toString(), materials.value(4).toLongLong(),
            QDateTime::fromString(materials.value(5).toString(), Qt::ISODateWithMs), materials.value(6).toBool(), materials.value(7).toString(), materials.value(8).toString(), QDate::fromString(materials.value(9).toString(), Qt::ISODate)});
    }
    auto folders = query("SELECT path,identity,available FROM folders ORDER BY path");
    while (folders.next()) result.folders.append({folders.value(0).toString(), folders.value(1).toString(), folders.value(2).toBool()});
    auto operations = query("SELECT id,source,destination,kind FROM operations");
    while (operations.next()) result.operations.append({operations.value(0).toString(), operations.value(1).toString(), operations.value(2).toString(), operations.value(3).toString()});
    auto imports = query("SELECT id,stage,destination,state FROM pending_imports");
    while (imports.next()) result.operations.append({imports.value(0).toString(), imports.value(1).toString(), imports.value(2).toString(), "import: " + imports.value(3).toString()});
    auto trash = query("SELECT id,source,destination FROM trash");
    while (trash.next()) result.trash.append({trash.value(0).toString(), trash.value(1).toString(), trash.value(2).toString(), "trash"});
    auto resources = query("SELECT id,course_id,kind,title,material_id,date,notes,url,folder,category,exam_date FROM resources ORDER BY kind,date,title");
    while (resources.next()) result.resources.append({resources.value(0).toString(), resources.value(1).toString(), resources.value(2).toString(),
        resources.value(3).toString(), resources.value(4).toString(), QDate::fromString(resources.value(5).toString(), Qt::ISODate),
        resources.value(6).toString(), resources.value(7).toString(), resources.value(8).toString(), resources.value(9).toString(),
        QDate::fromString(resources.value(10).toString(), Qt::ISODate)});
    auto events = query("SELECT id,semester_id,course_id,title,kind,all_day,on_date,starts_utc,ends_utc,timezone,completed FROM calendar_events ORDER BY on_date,starts_utc,title");
    while (events.next()) result.events.append({events.value(0).toString(), events.value(1).toString(), events.value(2).toString(),
        events.value(3).toString(), events.value(4).toString(), events.value(5).toBool(), QDate::fromString(events.value(6).toString(), Qt::ISODate),
        QDateTime::fromString(events.value(7).toString(), Qt::ISODateWithMs), QDateTime::fromString(events.value(8).toString(), Qt::ISODateWithMs),
        events.value(9).toString(), events.value(10).toBool()});
    auto classes = query("SELECT id,semester_id,course_id,title,kind,weekday,start_time,end_time,timezone,ambiguity FROM weekly_classes ORDER BY weekday,start_time");
    while (classes.next()) result.weeklyClasses.append({classes.value(0).toString(), classes.value(1).toString(), classes.value(2).toString(),
        classes.value(3).toString(), classes.value(4).toString(), classes.value(5).toInt(), QTime::fromString(classes.value(6).toString(), "HH:mm:ss"),
        QTime::fromString(classes.value(7).toString(), "HH:mm:ss"), classes.value(8).toString(), classes.value(9).toString()});
    auto ranges = query("SELECT id,semester_id,start_date,end_date FROM exam_ranges ORDER BY start_date");
    while (ranges.next()) result.examRanges.append({ranges.value(0).toString(), ranges.value(1).toString(),
        QDate::fromString(ranges.value(2).toString(), Qt::ISODate), QDate::fromString(ranges.value(3).toString(), Qt::ISODate)});
    auto timezone = query("SELECT timezone FROM calendar_settings WHERE id=1");
    if (!timezone.next()) throw Error("Calendar settings are missing; nothing was reset.");
    result.calendarTimeZone = timezone.value(0).toString();
    return result;
}

void Database::insert(const Semester& semester)
{
    query("INSERT INTO semesters (id,name,season,start_date,end_date,path) VALUES (?,?,?,?,?,?)", {semester.id, semester.name, semester.season,
        semester.start.toString(Qt::ISODate), semester.end.toString(Qt::ISODate), semester.path});
}

void Database::insert(const Course& course)
{
    query("INSERT INTO courses VALUES (?,?,?,?,?,?)", {course.id, course.semesterId, course.name,
        course.code.isNull() ? QString("") : course.code, course.color, course.path});
}

void Database::saveMaterial(const Material& material)
{
    // Updating the same path preserves its UUID and its user-facing title.
    query("INSERT INTO materials (id,course_id,title,path,size,modified_utc,available,identity) VALUES (?,?,?,?,?,?,1,?) "
          "ON CONFLICT(path) DO UPDATE SET course_id=excluded.course_id,size=excluded.size,"
          "modified_utc=excluded.modified_utc,available=1,identity=excluded.identity",
        {material.id, material.courseId.isEmpty() ? QVariant() : QVariant(material.courseId), material.title,
         material.path, material.size, material.modified.toUTC().toString(Qt::ISODateWithMs), material.identity.isNull() ? QString("") : material.identity});
}

void Database::reconcile(const QList<Material>& discovered, const std::function<void()>& checkpoint, bool ownTransaction)
{
    // Discovery contains every currently available record outside the completed scan scopes.
    std::unique_ptr<Transaction> transaction;
    if (ownTransaction) transaction = std::make_unique<Transaction>(*this);
    const auto previous = records().materials;
    QSet<QString> found;
    QHash<QString, int> oldCounts, newCounts;
    QHash<QString, Material> byIdentity;
    for (const auto& m : previous) { ++oldCounts[m.identity]; byIdentity.insert(m.identity, m); }
    for (const auto& m : discovered) { found.insert(m.path); ++newCounts[m.identity]; }
    qsizetype matched = 0;
    for (const auto& m : discovered) {
        if (matched++ % 256 == 0) checkpoint();
        if (m.identity.isEmpty() || oldCounts[m.identity] != 1 || newCounts[m.identity] != 1) continue;
        const auto old = byIdentity.value(m.identity);
        if (!found.contains(old.path)) {
            auto occupied = query("SELECT id FROM materials WHERE path=?", {m.path});
            if (!occupied.next()) query("UPDATE materials SET path=? WHERE id=?", {m.path, old.id});
        }
    }
    QHash<QString, Material> savedByPath;
    for (const auto& old : records().materials) {
        checkpoint();
        savedByPath.insert(old.path, old);
        if (old.available && !found.contains(old.path)) query("UPDATE materials SET available=0 WHERE id=?", {old.id});
    }
    qsizetype count = 0;
    for (const auto& material : discovered) {
        if (count++ % 256 == 0) checkpoint();
        const auto old = savedByPath.value(material.path);
        if (old.id.isEmpty() || !old.available || old.courseId != material.courseId
            || old.size != material.size || old.modified != material.modified || old.identity != material.identity)
            saveMaterial(material);
    }
    checkpoint();
    if (transaction) transaction->commit();
}

void Database::relocate(const QString& source, const QString& destination)
{
    QList<QPair<QString, QString>> bookmarks;
    auto resources = query("SELECT id,folder FROM resources WHERE folder <> ''");
    while (resources.next()) {
        const QString path = resources.value(1).toString();
        if (path == source || path.startsWith(source + '/')) bookmarks.append({resources.value(0).toString(), destination + path.mid(source.size())});
    }
    resources.finish();
    for (const auto& bookmark : bookmarks) query("UPDATE resources SET folder=? WHERE id=?", {bookmark.second, bookmark.first});
    // Exact component boundaries, never SQL LIKE wildcards from filenames.
    for (const auto& table : {"semesters", "courses", "materials", "folders"}) {
        QList<QPair<QString, QString>> changes;
        auto rows = query(QString("SELECT path FROM %1").arg(table));
        while (rows.next()) {
            const QString path = rows.value(0).toString();
            if (path == source || path.startsWith(source + '/'))
                changes.append({path, destination + path.mid(source.size())});
        }
        rows.finish();
        for (const auto& change : changes)
            query(QString("UPDATE %1 SET path=? WHERE path=?").arg(table), {change.second, change.first});
    }
}

void Database::reconcileFolders(const QList<Folder>& folders, const std::function<void()>& checkpoint)
{
    const auto old = records().folders;
    QSet<QString> found;
    QHash<QString, int> oldCounts, newCounts;
    QHash<QString, Folder> byIdentity;
    for (const auto& f : old) { ++oldCounts[f.identity]; byIdentity.insert(f.identity, f); }
    for (const auto& f : folders) { found.insert(f.path); ++newCounts[f.identity]; }
    for (const auto& f : folders) {
        if (f.identity.isEmpty() || oldCounts[f.identity] != 1 || newCounts[f.identity] != 1) continue;
        checkpoint();
        const auto previous = byIdentity.value(f.identity);
        if (found.contains(previous.path)) continue;
        // A pre-existing destination with different metadata needs user review.
        auto occupied = query("SELECT path FROM folders WHERE path=?", {f.path});
        if (!occupied.next()) relocate(previous.path, f.path);
    }
    QHash<QString, Folder> savedByPath;
    for (const auto& f : records().folders) {
        savedByPath.insert(f.path, f);
        if (f.available && !found.contains(f.path)) query("UPDATE folders SET available=0 WHERE path=?", {f.path});
    }
    for (const auto& f : folders) {
        checkpoint();
        if (!savedByPath.contains(f.path) || savedByPath.value(f.path) != f)
            query("INSERT INTO folders VALUES (?,?,1) ON CONFLICT(path) DO UPDATE SET identity=excluded.identity,available=1", {f.path, f.identity.isNull() ? QString("") : f.identity});
    }
}

void Database::verifySnapshot(const QString& filename) const
{
    const QString name = "verify-" + uuid();
    bool valid = false;
    {
        auto copy = QSqlDatabase::addDatabase("QSQLITE", name);
        copy.setDatabaseName(filename);
        copy.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (copy.open()) {
            QSqlQuery check(copy);
            valid = check.exec("PRAGMA quick_check") && check.next() && check.value(0).toString() == "ok";
            valid = valid && check.exec("PRAGMA foreign_key_check") && !check.next();
            valid = valid && check.exec("SELECT id FROM library") && check.next() && check.value(0).toString() == libraryId();
            valid = valid && check.exec("PRAGMA application_id") && check.next() && check.value(0).toInt() == applicationId;
        }
        copy.close();
    }
    QSqlDatabase::removeDatabase(name);
    if (!valid) throw Error("Metadata snapshot could not be verified. The original database was kept.");
}

void Database::createSnapshot(const QString& filename) const
{
    // VACUUM INTO never replaces an existing file. Incomplete stages never look like verified snapshots.
    const QString stage = filename + ".part";
    // SQLite accepts an existing EMPTY output file; our staging path must be new.
    for (const auto& path : {filename, stage, stage + "-journal", stage + "-wal", stage + "-shm"})
        if (QFileInfo(path).exists() || QFileInfo(path).isSymLink())
            throw Error("Snapshot destination or staging path already exists. Nothing was replaced.");
    QString parent = QFileInfo(filename).dir().absolutePath();
    while (true) {
        if (QFileInfo(parent).isSymLink()) throw Error("Snapshot paths cannot traverse symbolic links.");
        const QString next = QFileInfo(parent).dir().absolutePath();
        if (next == parent) break;
        parent = next;
    }
    query("VACUUM INTO ?", {stage});
    verifySnapshot(stage);
    paths::finalizeWithoutOverwrite(stage, filename);
    if (!QFile::remove(stage)) throw Error("Verified snapshot saved, but its redundant staging link could not be removed: " + stage);
}

} // namespace virgil
