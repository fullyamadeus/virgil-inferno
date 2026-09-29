#include "domain/error.h"
#include "services/library.h"
#include "services/paths.h"
#include "services/catalog.h"
#include "storage/database.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QSet>
#include <QUuid>
#include <filesystem>

using namespace virgil;

namespace {
void writeFile(const QString& path, const QByteArray& bytes = "study notes\n")
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw Error("Cannot write test fixture: " + path);
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw Error("Cannot read test fixture");
    return file.readAll();
}

QString addCourse(Library& library, const QString& name = "Математика")
{
    const QString semester = library.createSemester("Зимски 2026", "winter", QDate(2026, 10, 1), QDate(2027, 2, 17));
    return library.createCourse(semester, name, "F18L1", "#2764C5");
}

QString courseFolder(const Library& library)
{
    return library.snapshot().courses.first().path + "/Other Materials";
}
}

class LibraryTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void completeWorkflowAndRelocation()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString root = temporary.filePath("Virgil Библиотека");
        QVERIFY(QDir().mkdir(root));
        const QString source = temporary.filePath("Белешки со празни места.txt");
        const QByteArray content = QString("Локални материјали\nNo internet needed.\n").toUtf8();
        writeFile(source, content);
        QString materialId;
        QString courseId;
        QString relative;
        QString libraryId;
        {
            auto library = Library::create(root);
            courseId = addCourse(*library);
            library->importFile(source, courseFolder(*library), QFileInfo(source).fileName());
            const auto snapshot = library->refresh();
            QCOMPARE(snapshot.semesters.size(), 1);
            QCOMPARE(snapshot.courses.size(), 1);
            QCOMPARE(snapshot.materials.size(), 1);
            const auto material = snapshot.materials.first();
            QCOMPARE(material.courseId, courseId);
            QVERIFY(!QDir::isAbsolutePath(material.path));
            QCOMPARE(readFile(library->materialPath(material.id)), content);
            QCOMPARE(readFile(source), content);
            materialId = material.id;
            relative = material.path;
            libraryId = snapshot.libraryId;
            QVERIFY(QFileInfo(QDir(root).filePath("Зимски 2026/Математика/Books")).isDir());
        }
        {
            auto reopened = Library::open(root);
            QCOMPARE(reopened->refresh().materials.first().id, materialId);
            QCOMPARE(reopened->snapshot().libraryId, libraryId);
        }
        const QString moved = temporary.filePath("Moved library");
        QVERIFY(QDir().rename(root, moved));
        auto relocated = Library::open(moved);
        const auto snapshot = relocated->refresh();
        QCOMPARE(snapshot.courses.first().id, courseId);
        QCOMPARE(snapshot.materials.first().id, materialId);
        QCOMPARE(snapshot.materials.first().path, relative);
        QCOMPARE(readFile(relocated->materialPath(materialId)), content);
    }

    void inspectionAndAdoptionNeverReorganize()
    {
        QTemporaryDir temporary;
        const QString file = temporary.filePath("existing.unknown-format");
        writeFile(file, "original");
        const auto preview = Library::inspect(temporary.path());
        QCOMPARE(preview.entries, QStringList{"existing.unknown-format"});
        QVERIFY(!QFileInfo::exists(temporary.filePath(".virgil")));
        QVERIFY_EXCEPTION_THROWN(Library::create(temporary.path()), Error);
        auto library = Library::create(temporary.path(), true);
        const auto snapshot = library->refresh();
        QCOMPARE(snapshot.materials.size(), 1);
        QVERIFY(snapshot.materials.first().courseId.isEmpty());
        QCOMPARE(readFile(file), QByteArray("original"));
        QCOMPARE(library->materialPath(snapshot.materials.first().id), file);
        QVERIFY_EXCEPTION_THROWN(Library::create(temporary.path(), true), Error);
    }

    void collisionsInternalFilesAndDistinctDuplicates()
    {
        QTemporaryDir temporary;
        QTemporaryDir sources;
        const QString source = sources.filePath("notes.txt");
        writeFile(source);
        auto library = Library::create(temporary.path());
        addCourse(*library);
        const QString folder = courseFolder(*library);
        library->importFile(source, folder, "notes.txt");
        QVERIFY_EXCEPTION_THROWN(library->importFile(source, folder, "notes.txt"), Error);
        QVERIFY_EXCEPTION_THROWN(library->importFile(source, folder, "NOTES.TXT"), Error);
        const auto first = library->snapshot().materials.first();
        const QString destination = library->materialPath(first.id);
        library->importFile(destination, folder, "another-name.txt");
        QCOMPARE(library->snapshot().materials.size(), 1);
        QVERIFY(!QFileInfo::exists(temporary.filePath(folder + "/another-name.txt")));
        library->importFile(source, folder, "distinct-copy.txt");
        const auto snapshot = library->refresh();
        QCOMPARE(snapshot.materials.size(), 2);
        QVERIFY(snapshot.materials[0].id != snapshot.materials[1].id);
        QCOMPARE(readFile(source), readFile(destination));
    }

    void externalChangesRetainMetadataAndTitles()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        addCourse(*library);
        const QString folder = courseFolder(*library);
        QVERIFY(QDir().mkdir(temporary.filePath(folder + "/Nested")));
        const QString path = folder + "/Nested/external.txt";
        writeFile(temporary.filePath(path));
        auto snapshot = library->refresh();
        QCOMPARE(snapshot.materials.size(), 1);
        const QString id = snapshot.materials.first().id;
        library.reset();
        {
            Database database(temporary.filePath(".virgil/library.sqlite"), false);
            database.query("UPDATE materials SET title=? WHERE id=?", {"My display title", id});
        }
        // Simulate an editor's save-by-replacement while the app is closed.
        QVERIFY(QFile::remove(temporary.filePath(path)));
        writeFile(temporary.filePath(path), "replacement bytes");
        library = Library::open(temporary.path());
        snapshot = library->refresh();
        QCOMPARE(snapshot.materials.first().id, id);
        QCOMPARE(snapshot.materials.first().title, QString("My display title"));
        QCOMPARE(snapshot.materials.first().size, qint64(17));
        QVERIFY(QFile::rename(temporary.filePath(path), temporary.filePath(folder + "/renamed.txt")));
        snapshot = library->refresh();
        QCOMPARE(snapshot.materials.size(), 1);
        QCOMPARE(snapshot.materials.first().id, id);
        QCOMPARE(snapshot.materials.first().title, QString("My display title"));
        QVERIFY(snapshot.materials.first().available);
        QVERIFY(QFile::rename(temporary.filePath(folder + "/renamed.txt"), temporary.filePath(path)));
        snapshot = library->refresh();
        for (const auto& material : snapshot.materials)
            if (material.id == id) { QVERIFY(material.available); QCOMPARE(material.title, QString("My display title")); }
    }

    void missingRootDoesNotEraseRecords()
    {
        QTemporaryDir temporary;
        const QString root = temporary.filePath("library");
        const QString moved = temporary.filePath("unmounted");
        QVERIFY(QDir().mkdir(root));
        auto library = Library::create(root);
        writeFile(QDir(root).filePath("notes.txt"));
        const auto before = library->refresh();
        QVERIFY(QDir().rename(root, moved));
        QVERIFY(QDir().mkdir(root)); // Empty mount point must not be scanned as deletion.
        QVERIFY_EXCEPTION_THROWN(library->refresh(), Error);
        QVERIFY_EXCEPTION_THROWN(library->createSemester("New", "winter", QDate(2026, 1, 1), QDate(2026, 2, 1)), Error);
        QVERIFY(QDir().rmdir(root));
        QVERIFY(QDir().rename(moved, root));
        QCOMPARE(library->snapshot().materials.first().id, before.materials.first().id);
        QVERIFY(library->snapshot().materials.first().available);
    }

    void unreadableDirectoryDoesNotCommitPartialScan()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        const QString folder = temporary.filePath("private");
        QVERIFY(QDir().mkdir(folder));
        writeFile(folder + "/notes.txt");
        library->refresh();
        const auto permissions = QFile::permissions(folder);
        QVERIFY(QFile::setPermissions(folder, {}));
        bool rejected = false;
        try { library->refresh(); } catch (const Error&) { rejected = true; }
        QVERIFY(QFile::setPermissions(folder, permissions));
        QVERIFY(rejected);
        QVERIFY(library->snapshot().materials.first().available);
#else
        QSKIP("Unix permission fixture; Windows ACL behavior has not been verified.");
#endif
    }

    void cancelledCopyKeepsOriginalAndLeavesNoMaterial()
    {
        QTemporaryDir temporary;
        QTemporaryDir sources;
        const QString source = sources.filePath("large.dat");
        writeFile(source, QByteArray(3 * 1024 * 1024, 'x'));
        auto library = Library::create(temporary.path());
        addCourse(*library);
        Work work;
        work.progress = [&](const QString&, int) { work.cancelled->store(true); };
        QVERIFY_EXCEPTION_THROWN(library->importFile(source, courseFolder(*library), "large.dat", work), Error);
        QCOMPARE(QFileInfo(source).size(), qint64(3 * 1024 * 1024));
        QCOMPARE(library->refresh().materials.size(), 0);
        QCOMPARE(QDir(temporary.filePath(".virgil/staging")).entryList(QDir::Files).size(), 0);
    }

    void cancelledScanDoesNotHideExistingMaterials()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        writeFile(temporary.filePath("keep.txt"));
        library->refresh();
        Work work;
        work.cancelled->store(true);
        QVERIFY_EXCEPTION_THROWN(library->refresh(work), Error);
        QVERIFY(library->snapshot().materials.first().available);
    }

    void cancellationDuringReconciliationRollsBack()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        writeFile(temporary.filePath("keep.txt"));
        const QString id = library->refresh().materials.first().id;
        writeFile(temporary.filePath("new.txt"));
        Work work;
        work.progress = [&](const QString& message, int) {
            if (message.startsWith("Saving")) work.cancelled->store(true);
        };
        QVERIFY_EXCEPTION_THROWN(library->refresh(work), Error);
        const auto saved = library->snapshot();
        QCOMPARE(saved.materials.size(), 1);
        QCOMPARE(saved.materials.first().id, id);
        QVERIFY(saved.materials.first().available);
        QCOMPARE(library->refresh().materials.size(), 2);
    }

    void changingSourceIsRejectedWithoutInstallingPartialFile()
    {
        QTemporaryDir temporary;
        QTemporaryDir sources;
        const QString source = sources.filePath("changing.txt");
        writeFile(source, "original");
        auto library = Library::create(temporary.path());
        addCourse(*library);
        Work work;
        work.progress = [&](const QString&, int percent) {
            if (percent == 0) writeFile(source, "source was changed while copying");
        };
        QVERIFY_EXCEPTION_THROWN(library->importFile(source, courseFolder(*library), "changing.txt", work), Error);
        QCOMPARE(readFile(source), QByteArray("source was changed while copying"));
        QCOMPARE(library->refresh().materials.size(), 0);
    }

    void symlinksAndPathEscapesAreRejected()
    {
        QTemporaryDir temporary;
        QTemporaryDir outside;
        auto library = Library::create(temporary.path());
        addCourse(*library);
        writeFile(outside.filePath("outside.txt"));
        QVERIFY_EXCEPTION_THROWN(library->importFile(outside.filePath("outside.txt"), "../escape", "x.txt"), Error);
        QVERIFY_EXCEPTION_THROWN(paths::inside(temporary.path(), ".virgil/library.sqlite"), Error);
        QVERIFY_EXCEPTION_THROWN(paths::inside(temporary.path(), "Books/../../escape"), Error);
        QVERIFY_EXCEPTION_THROWN(paths::validateName("../danger"), Error);
        QVERIFY_EXCEPTION_THROWN(paths::validateName("CON.txt"), Error);
        QVERIFY_EXCEPTION_THROWN(paths::validateName("trailing."), Error);
#ifdef Q_OS_UNIX
        QVERIFY(QFile::link(outside.path(), temporary.filePath("outside-link")));
        QVERIFY_EXCEPTION_THROWN(paths::inside(temporary.path(), "outside-link/outside.txt"), Error);
        const auto snapshot = library->refresh();
        QCOMPARE(snapshot.materials.size(), 0);
        QVERIFY(!snapshot.notices.isEmpty());
        QVERIFY(QFile::link(temporary.path(), outside.filePath("library-link")));
        QVERIFY_EXCEPTION_THROWN(Library::open(outside.filePath("library-link")), Error);
#endif
    }

    void singleWriterLockIsReleased()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        QVERIFY_EXCEPTION_THROWN(Library::open(temporary.path()), Error);
        library.reset();
        auto reopened = Library::open(temporary.path());
        QVERIFY(!reopened->snapshot().libraryId.isEmpty());
    }

    void futureAndCorruptDatabasesAreNotReset()
    {
        QTemporaryDir temporary;
        { auto library = Library::create(temporary.path()); }
        const QString db = temporary.filePath(".virgil/library.sqlite");
        { Database database(db, false); database.query("PRAGMA user_version=999"); }
        const auto before = readFile(db);
        QVERIFY_EXCEPTION_THROWN(Library::open(temporary.path()), Error);
        QCOMPARE(readFile(db), before);
        writeFile(db, "not a SQLite database");
        const auto corrupt = readFile(db);
        QVERIFY_EXCEPTION_THROWN(Library::open(temporary.path()), Error);
        QCOMPARE(readFile(db), corrupt);
    }

    void interruptedImportRecovery()
    {
        QTemporaryDir temporary;
        QString destination;
        { auto library = Library::create(temporary.path()); addCourse(*library); destination = courseFolder(*library) + "/recovered.txt"; }
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString stage = ".virgil/staging/" + id + ".part";
        writeFile(temporary.filePath(stage), "completed copy");
        paths::finalizeWithoutOverwrite(temporary.filePath(stage), temporary.filePath(destination));
        {
            Database database(temporary.filePath(".virgil/library.sqlite"), false);
            database.query("INSERT INTO pending_imports VALUES (?,?,?,'ready')", {id, destination, stage});
        }
        { // Simulates a crash after installing bytes but before committing metadata.
            auto library = Library::open(temporary.path());
            const auto snapshot = library->refresh();
            QCOMPARE(snapshot.materials.size(), 1);
            QCOMPARE(readFile(library->materialPath(snapshot.materials.first().id)), QByteArray("completed copy"));
            QVERIFY(!QFileInfo::exists(temporary.filePath(stage)));
            QVERIFY(!snapshot.notices.isEmpty());
        }
        const QString partialId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString partial = ".virgil/staging/" + partialId + ".part";
        writeFile(temporary.filePath(partial), "partial");
        {
            Database database(temporary.filePath(".virgil/library.sqlite"), false);
            database.query("INSERT INTO pending_imports VALUES (?,?,?,'copying')", {partialId, destination + ".other", partial});
        }
        auto library = Library::open(temporary.path());
        QCOMPARE(library->refresh().materials.size(), 1); // No staging file shown as material.
        QCOMPARE(readFile(temporary.filePath(partial)), QByteArray("partial"));
        QVERIFY(!library->snapshot().notices.isEmpty());
    }

    void finalizationNeverOverwrites()
    {
        QTemporaryDir temporary;
        writeFile(temporary.filePath("stage"), "new bytes");
        writeFile(temporary.filePath("destination"), "original bytes");
        QVERIFY_EXCEPTION_THROWN(paths::finalizeWithoutOverwrite(temporary.filePath("stage"), temporary.filePath("destination")), Error);
        QCOMPARE(readFile(temporary.filePath("destination")), QByteArray("original bytes"));
        QCOMPARE(readFile(temporary.filePath("stage")), QByteArray("new bytes"));
    }

    void foldersMovesTrashAndCourseRemoval()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        const QString courseId = addCourse(*library);
        const QString folder = courseFolder(*library);
        library->createFolder(folder, "Nested");
        writeFile(temporary.filePath(folder + "/Nested/test.txt"));
        auto saved = library->refresh();
        const QString id = saved.materials.first().id;
        QVERIFY_EXCEPTION_THROWN(library->createFolder(folder, "NESTED"), Error);
        QVERIFY_EXCEPTION_THROWN(library->movePath(folder + "/Nested", folder + "/NESTED"), Error);
        QVERIFY_EXCEPTION_THROWN(library->movePath(folder + "/Nested", folder + "/Nested/child"), Error);
        library->movePath(folder + "/Nested", folder + "/Moved");
        saved = library->refresh();
        QCOMPARE(saved.materials.first().id, id);
        QCOMPARE(saved.materials.first().path, folder + "/Moved/test.txt");
        library->trashPath(folder + "/Moved");
        saved = library->refresh();
        QCOMPARE(saved.trash.size(), 1);
        QVERIFY(!saved.materials.first().available);
        const QString trashId = saved.trash.first().id;
        writeFile(temporary.filePath(folder + "/Moved"), "collision");
        QVERIFY_EXCEPTION_THROWN(library->restoreTrash(trashId), Error);
        QCOMPARE(readFile(temporary.filePath(folder + "/Moved")), QByteArray("collision"));
        QVERIFY(QFile::remove(temporary.filePath(folder + "/Moved")));
        library->restoreTrash(trashId);
        saved = library->refresh();
        QCOMPARE(saved.materials.first().id, id);
        QVERIFY(saved.materials.first().available);
        QVERIFY(saved.trash.isEmpty());
        library->removeCourse(courseId);
        saved = library->refresh();
        QVERIFY(saved.courses.isEmpty());
        QVERIFY(saved.materials.first().courseId.isEmpty());
        QCOMPARE(readFile(library->materialPath(id)), QByteArray("study notes\n"));
    }

    void externalFolderRenameAndAmbiguousHardLinks()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        const QString courseId = addCourse(*library);
        const QString course = library->snapshot().courses.first().path;
        writeFile(temporary.filePath(course + "/Books/a.txt"));
        auto saved = library->refresh();
        const QString id = saved.materials.first().id;
        const QString renamed = QFileInfo(course).path() + "/Renamed course";
        library.reset();
        QVERIFY(QDir().rename(temporary.filePath(course), temporary.filePath(renamed)));
        library = Library::open(temporary.path());
        saved = library->refresh();
        QCOMPARE(saved.courses.first().path, renamed);
        QCOMPARE(saved.courses.first().id, courseId);
        QCOMPARE(saved.materials.first().id, id);
        QCOMPARE(saved.materials.first().courseId, courseId);
        // Hard links share identity: neither path should steal the other's metadata.
        std::filesystem::create_hard_link(paths::native(temporary.filePath(renamed + "/Books/a.txt")), paths::native(temporary.filePath(renamed + "/Books/b.txt")));
        saved = library->refresh();
        QCOMPARE(saved.materials.size(), 2);
        QVERIFY(QFile::rename(temporary.filePath(renamed + "/Books/a.txt"), temporary.filePath(renamed + "/Books/c.txt")));
        saved = library->refresh();
        QCOMPARE(saved.materials.size(), 3);
        int missing = 0;
        for (const auto& m : saved.materials) if (!m.available) ++missing;
        QCOMPARE(missing, 1);
    }

    void explicitRelinkKeepsMissingIdentityAndTitle()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        writeFile(temporary.filePath("old.txt"), "old");
        auto saved = library->refresh();
        const QString id = saved.materials.first().id;
        QVERIFY(QFile::remove(temporary.filePath("old.txt")));
        writeFile(temporary.filePath("new.txt"), "new replacement");
        saved = library->refresh();
        QString replacement;
        for (const auto& m : saved.materials) if (m.path == "new.txt") replacement = m.id;
        QCOMPARE(saved.materials.size(), 2);
        library->relink(id, replacement);
        saved = library->refresh();
        QCOMPARE(saved.materials.size(), 1);
        QCOMPARE(saved.materials.first().id, id);
        QCOMPARE(saved.materials.first().title, QString("old"));
        QCOMPARE(saved.materials.first().path, QString("new.txt"));
        QCOMPARE(readFile(library->materialPath(id)), QByteArray("new replacement"));
        QVERIFY_EXCEPTION_THROWN(library->relink(id, replacement), Error);
    }

    void incrementalScanAndLargeScanCancellation()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        library->createFolder("", "A");
        library->createFolder("", "B");
        writeFile(temporary.filePath("A/one.txt"));
        writeFile(temporary.filePath("B/two.txt"));
        auto saved = library->refresh();
        QVERIFY(QFile::remove(temporary.filePath("B/two.txt")));
        writeFile(temporary.filePath("A/new.unknown"));
        saved = library->refresh({}, {temporary.filePath("A")});
        QCOMPARE(saved.materials.size(), 3);
        for (const auto& m : saved.materials) if (m.path == "B/two.txt") QVERIFY(m.available); // outside this incremental scope
        saved = library->refresh();
        for (const auto& m : saved.materials) if (m.path == "B/two.txt") QVERIFY(!m.available);
        for (int i = 0; i < 4000; ++i) writeFile(temporary.filePath(QString("A/%1.bin").arg(i)), "x");
        Work work;
        work.progress = [&](const QString& message, int) { if (message.startsWith("Saving")) work.cancelled->store(true); };
        QVERIFY_EXCEPTION_THROWN(library->refresh(work), Error);
        QCOMPARE(library->snapshot().materials.size(), 3);
        saved = library->refresh();
        QCOMPARE(saved.materials.size(), 4003);
    }

    void schemaOneMigrationKeepsVerifiedSnapshot()
    {
        QTemporaryDir temporary;
        QString id;
        { auto library = Library::create(temporary.path()); writeFile(temporary.filePath("keep.txt")); id = library->refresh().materials.first().id; }
        // Reconstruct the actual v1 schema, with no v2 columns/tables.
        {
            Database db(temporary.filePath(".virgil/library.sqlite"), false);
            db.query("DROP TABLE calendar_events"); db.query("DROP TABLE weekly_classes");
            db.query("DROP TABLE exam_ranges"); db.query("DROP TABLE calendar_settings");
            db.query("ALTER TABLE semesters DROP COLUMN teaching_start"); db.query("ALTER TABLE semesters DROP COLUMN teaching_end");
            db.query("DROP TABLE resources");
            db.query("ALTER TABLE materials DROP COLUMN category"); db.query("ALTER TABLE materials DROP COLUMN exam_date");
            db.query("DROP TABLE folders"); db.query("DROP TABLE operations"); db.query("DROP TABLE trash");
            db.query("ALTER TABLE materials DROP COLUMN identity"); db.query("PRAGMA user_version=1");
        }
        auto library = Library::open(temporary.path());
        QCOMPARE(library->refresh().materials.first().id, id);
        const auto backups = QDir(temporary.filePath(".virgil")).entryList({"*.before-v2-*.sqlite"}, QDir::Files);
        QCOMPARE(backups.size(), 1);
        const QString connection = "migration-backup-check";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(temporary.filePath(".virgil/" + backups.first()));
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("PRAGMA quick_check")); QVERIFY(query.next()); QCOMPARE(query.value(0).toString(), QString("ok"));
            QVERIFY(query.exec("PRAGMA user_version")); QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 1);
            QVERIFY(query.exec("SELECT id FROM materials")); QVERIFY(query.next()); QCOMPARE(query.value(0).toString(), id);
        }
        QSqlDatabase::removeDatabase(connection);
    }

    void interruptedMoveTrashAndRestoreRecovery()
    {
        for (const auto& kind : {QString("move"), QString("trash"), QString("restore")}) {
            QTemporaryDir temporary;
            QString materialId, source, destination, identity, operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            {
                auto library = Library::create(temporary.path());
                writeFile(temporary.filePath("keep.txt"));
                materialId = library->refresh().materials.first().id;
                source = "keep.txt";
                destination = kind == "trash" ? ".virgil/trash/" + operationId : "moved.txt";
                if (kind == "restore") {
                    library->trashPath("keep.txt");
                    auto saved = library->refresh();
                    source = saved.trash.first().destination;
                    destination = "keep.txt";
                } else if (kind == "trash") QVERIFY(QDir().mkdir(temporary.filePath(".virgil/trash")));
                identity = paths::identity(temporary.filePath(source));
            }
            {
                Database db(temporary.filePath(".virgil/library.sqlite"), false);
                db.query("INSERT INTO operations VALUES (?,?,?,?,?)", {operationId, source, destination, identity, kind});
            }
            // Process interruption after filesystem move, before metadata commit.
            paths::renameWithoutOverwrite(temporary.filePath(source), temporary.filePath(destination));
            auto recovered = Library::open(temporary.path());
            auto saved = recovered->refresh();
            QVERIFY(saved.operations.isEmpty());
            QCOMPARE(saved.materials.first().id, materialId);
            QCOMPARE(saved.materials.first().path, destination);
            QCOMPARE(saved.trash.size(), kind == "trash" ? 1 : 0);
            QCOMPARE(saved.materials.first().available, kind != "trash");
        }
    }

    void atomicMoveCollisionAndSymlinkRejection()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        writeFile(temporary.filePath("source.txt"), "source");
        writeFile(temporary.filePath("target.txt"), "target");
        library->refresh();
        QVERIFY_EXCEPTION_THROWN(paths::renameWithoutOverwrite(temporary.filePath("source.txt"), temporary.filePath("target.txt")), Error);
        QVERIFY_EXCEPTION_THROWN(library->movePath("source.txt", "target.txt"), Error);
        QCOMPARE(readFile(temporary.filePath("source.txt")), QByteArray("source"));
        QCOMPARE(readFile(temporary.filePath("target.txt")), QByteArray("target"));
        QVERIFY(QFile::link(temporary.filePath("source.txt"), temporary.filePath("link")));
        QVERIFY_EXCEPTION_THROWN(library->movePath("link", "moved-link"), Error);
        QVERIFY_EXCEPTION_THROWN(library->trashPath("link"), Error);
        QVERIFY(library->snapshot().operations.isEmpty());
    }

    void unstartedAndAmbiguousMoveReview()
    {
        QTemporaryDir temporary;
        QString identity;
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        { auto library = Library::create(temporary.path()); writeFile(temporary.filePath("source.txt"), "source"); library->refresh(); identity = paths::identity(temporary.filePath("source.txt")); }
        auto journal = [&] {
            Database db(temporary.filePath(".virgil/library.sqlite"), false);
            db.query("INSERT INTO operations VALUES (?,?,?,?,?)", {id, "source.txt", "target.txt", identity, "move"});
        };
        journal();
        { auto library = Library::open(temporary.path()); QVERIFY(library->snapshot().operations.isEmpty()); }
        journal();
        writeFile(temporary.filePath("target.txt"), "unrelated");
        auto library = Library::open(temporary.path());
        QCOMPARE(library->snapshot().operations.size(), 1);
        QVERIFY_EXCEPTION_THROWN(library->reviewOperation(id, true), Error);
        QVERIFY_EXCEPTION_THROWN(library->trashPath("source.txt"), Error);
        QCOMPARE(readFile(temporary.filePath("source.txt")), QByteArray("source"));
        QCOMPARE(readFile(temporary.filePath("target.txt")), QByteArray("unrelated"));
        QVERIFY(QFile::remove(temporary.filePath("target.txt")));
        library->reviewOperation(id, true);
        QVERIFY(library->snapshot().operations.isEmpty());
        QCOMPARE(readFile(temporary.filePath("target.txt")), QByteArray("source"));
    }

    void deleteCourseAfterExternalFolderRemovalKeepsResourceRecords()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        const QString courseId = addCourse(*library);
        const QString path = courseFolder(*library) + "/lecture.txt";
        writeFile(temporary.filePath(path));
        auto saved = library->refresh();
        const QString materialId = saved.materials.first().id;
        Resource lecture;
        lecture.courseId = courseId; lecture.kind = "lecture"; lecture.title = "Lecture one";
        lecture.date = QDate(2026, 10, 5); lecture.notes = "Notes survive the missing attachment"; lecture.materialId = materialId;
        const QString lectureId = library->saveResource(lecture);
        const QString directory = library->snapshot().courses.first().path;
        QVERIFY(QDir(temporary.filePath(directory)).removeRecursively());
        library->refresh();
        library->removeCourse(courseId);
        saved = library->snapshot();
        QVERIFY(saved.courses.isEmpty());
        QCOMPARE(saved.materials.first().id, materialId);
        QVERIFY(!saved.materials.first().available);
        QVERIFY(saved.materials.first().courseId.isEmpty());
        QCOMPARE(saved.resources.first().id, lectureId);
        QVERIFY(saved.resources.first().courseId.isEmpty());
        QCOMPARE(saved.resources.first().notes, lecture.notes);
        QVERIFY_EXCEPTION_THROWN(library->removeCourse(courseId), Error);
        library.reset(); library = Library::open(temporary.path());
        QVERIFY(library->snapshot().courses.isEmpty());
        QCOMPARE(library->snapshot().resources.first().materialId, materialId);
    }

    void organizationAndMaterialMetadataKeepIdsPathsAcrossScans()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        const QString courseId = addCourse(*library);
        const QString path = courseFolder(*library) + "/exam.pdf";
        writeFile(temporary.filePath(path));
        auto saved = library->refresh();
        const QString materialId = saved.materials.first().id;
        const auto original = saved.courses.first();
        auto semester = saved.semesters.first(); semester.name = "My winter"; semester.end = QDate(2027, 2, 18);
        library->editSemester(semester);
        const QString nextSemester = library->createSemester("Summer", "summer", QDate(2027, 2, 19), QDate(2027, 9, 30));
        auto course = original; course.semesterId = nextSemester; course.name = "Renamed display title"; course.code = "NEW"; course.color = courseColors[1];
        library->editCourse(course);
        library->editMaterial(materialId, "Midterm 2026", "past-exam", QDate(2026, 11, 2));
        writeFile(temporary.filePath(path), "editor replacement bytes");
        saved = library->refresh();
        QCOMPARE(saved.courses.first().id, courseId); QCOMPARE(saved.courses.first().path, original.path);
        QCOMPARE(saved.courses.first().semesterId, nextSemester); QCOMPARE(saved.courses.first().color, courseColors[1]);
        QCOMPARE(saved.materials.first().id, materialId); QCOMPARE(saved.materials.first().title, QString("Midterm 2026"));
        QCOMPARE(saved.materials.first().category, QString("past-exam")); QCOMPARE(saved.materials.first().examDate, QDate(2026, 11, 2));
        QVERIFY(QFile::rename(temporary.filePath(path), temporary.filePath(courseFolder(*library) + "/renamed.pdf")));
        saved = library->refresh(); QCOMPARE(saved.materials.first().id, materialId); QCOMPARE(saved.materials.first().category, QString("past-exam"));
        library.reset(); library = Library::open(temporary.path());
        QCOMPARE(library->snapshot().materials.first().examDate, QDate(2026, 11, 2));
        semester.end = semester.start.addDays(-1); QVERIFY_EXCEPTION_THROWN(library->editSemester(semester), Error);
        QVERIFY_EXCEPTION_THROWN(library->editMaterial(materialId, "", "past-exam"), Error);
        QVERIFY_EXCEPTION_THROWN(library->editMaterial(materialId, "Title", "invalid"), Error);
    }

    void booksLecturesBookmarksPersistAndDeletingRecordsKeepsAttachments()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        const QString courseId = addCourse(*library);
        const QString folder = courseFolder(*library);
        writeFile(temporary.filePath(folder + "/book.pdf"), "local book");
        const QString materialId = library->refresh().materials.first().id;
        Resource book; book.courseId = courseId; book.kind = "book"; book.title = "C++ reading"; book.materialId = materialId;
        const QString bookId = library->saveResource(book);
        Resource lecture; lecture.courseId = courseId; lecture.kind = "lecture"; lecture.title = "Ownership"; lecture.date = QDate(2026, 10, 12); lecture.notes = "RAII notes со кирилица";
        const QString lectureId = library->saveResource(lecture);
        Resource bookmark; bookmark.courseId = courseId; bookmark.kind = "bookmark"; bookmark.title = "Reference"; bookmark.url = "https://example.org/course?q=1";
        bookmark.folder = folder; bookmark.category = "study-guide"; bookmark.examDate = QDate(2026, 11, 2);
        const QString bookmarkId = library->saveResource(bookmark);
        bookmark.url = "file:///etc/passwd"; QVERIFY_EXCEPTION_THROWN(library->saveResource(bookmark), Error);
        bookmark.url = "https://user:password@example.org/"; QVERIFY_EXCEPTION_THROWN(library->saveResource(bookmark), Error);
        Resource invalidBook = book; invalidBook.materialId.clear(); QVERIFY_EXCEPTION_THROWN(library->saveResource(invalidBook), Error);
        Resource invalidLecture = lecture; invalidLecture.date = {}; QVERIFY_EXCEPTION_THROWN(library->saveResource(invalidLecture), Error);
        library->movePath(folder, QFileInfo(folder).path() + "/Resources");
        auto saved = library->refresh();
        QCOMPARE(saved.resources.size(), 3);
        for (const auto& item : saved.resources) if (item.id == bookmarkId) QVERIFY(item.folder.endsWith("/Resources"));
        library.reset(); library = Library::open(temporary.path());
        saved = library->snapshot(); QCOMPARE(saved.resources.size(), 3);
        for (const auto& item : saved.resources) if (item.id == lectureId) QCOMPARE(item.notes, lecture.notes);
        library->removeResource(bookId); library->removeResource(lectureId);
        QCOMPARE(library->snapshot().resources.size(), 1);
        QCOMPARE(readFile(library->materialPath(materialId)), QByteArray("local book"));
        QVERIFY(library->snapshot().backups.size() <= 7);
    }

    void resourceAttachmentsFollowExplicitRelinking()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        writeFile(temporary.filePath("old.txt"));
        const QString oldId = library->refresh().materials.first().id;
        QVERIFY(QFile::remove(temporary.filePath("old.txt")));
        writeFile(temporary.filePath("new.txt"), "replacement");
        const auto saved = library->refresh();
        QString newId;
        for (const auto& item : saved.materials) if (item.available) newId = item.id;
        Resource book; book.kind = "book"; book.title = "Keep reference"; book.materialId = newId;
        const QString resourceId = library->saveResource(book);
        library->relink(oldId, newId);
        const auto linked = library->snapshot();
        QCOMPARE(linked.resources.first().id, resourceId); QCOMPARE(linked.resources.first().materialId, oldId);
        QCOMPARE(linked.materials.size(), 1);
    }

    void metadataBackupsRotateVerifyAndFailureDoesNotUndoSavedNotes()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        Resource lecture; lecture.kind = "lecture"; lecture.title = "Notes"; lecture.date = QDate(2026, 10, 1);
        lecture.id = library->saveResource(lecture);
        const QString unknown = temporary.filePath(".virgil/backups/automatic-manual.sqlite");
        writeFile(unknown, "unknown file stays intact");
        for (int i = 0; i < 10; ++i) { lecture.notes = QString("revision %1").arg(i); library->saveResource(lecture); }
        const auto saved = library->snapshot(); QCOMPARE(saved.backups.size(), 7);
        QCOMPARE(readFile(unknown), QByteArray("unknown file stays intact"));
        {
            Database db(temporary.filePath(".virgil/library.sqlite"), false);
            for (const auto& backup : saved.backups) db.verifySnapshot(temporary.filePath(".virgil/backups/" + backup));
        }
        QVERIFY(QDir().rename(temporary.filePath(".virgil/backups"), temporary.filePath(".virgil/saved-backups")));
        writeFile(temporary.filePath(".virgil/backups"), "a file blocks the directory");
        lecture.notes = "still saved when backup fails";
        library->saveResource(lecture);
        QCOMPARE(library->snapshot().resources.first().notes, lecture.notes);
        QVERIFY(library->snapshot().notices.join(' ').contains("backup failed"));
        QCOMPARE(readFile(temporary.filePath(".virgil/backups")), QByteArray("a file blocks the directory"));
    }

    void schemaTwoMigrationKeepsVerifiedSnapshot()
    {
        QTemporaryDir temporary;
        QString id;
        { auto library = Library::create(temporary.path()); writeFile(temporary.filePath("keep.txt")); id = library->refresh().materials.first().id; }
        {
            Database db(temporary.filePath(".virgil/library.sqlite"), false);
            db.query("DROP TABLE calendar_events"); db.query("DROP TABLE weekly_classes");
            db.query("DROP TABLE exam_ranges"); db.query("DROP TABLE calendar_settings");
            db.query("ALTER TABLE semesters DROP COLUMN teaching_start"); db.query("ALTER TABLE semesters DROP COLUMN teaching_end");
            db.query("DROP TABLE resources"); db.query("ALTER TABLE materials DROP COLUMN category");
            db.query("ALTER TABLE materials DROP COLUMN exam_date"); db.query("PRAGMA user_version=2");
        }
        auto library = Library::open(temporary.path());
        const auto saved = library->refresh(); QCOMPARE(saved.materials.first().id, id);
        QCOMPARE(saved.materials.first().category, QString("unclassified"));
        const auto backups = QDir(temporary.filePath(".virgil")).entryList({"*.before-v3-*.sqlite"}, QDir::Files);
        QCOMPARE(backups.size(), 1);
        Database db(temporary.filePath(".virgil/library.sqlite"), false);
        db.verifySnapshot(temporary.filePath(".virgil/" + backups.first()));
    }

    void snapshotPublicationRejectsExistingStagesAndSymlinks()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        Database db(temporary.filePath(".virgil/library.sqlite"), false);
        const QString destination = temporary.filePath(".virgil/manual.sqlite");
        writeFile(destination + ".part", "");
        QVERIFY_EXCEPTION_THROWN(db.createSnapshot(destination), Error);
        QCOMPARE(readFile(destination + ".part"), QByteArray());
        QVERIFY(!QFileInfo::exists(destination));
        QVERIFY(QFile::remove(destination + ".part"));
        const QString original = temporary.filePath("original-empty.txt"); writeFile(original, "");
        QVERIFY(QFile::link(original, destination + ".part"));
        QVERIFY_EXCEPTION_THROWN(db.createSnapshot(destination), Error);
        QCOMPARE(readFile(original), QByteArray());
        QVERIFY(QFile::remove(destination + ".part"));
        writeFile(destination, "existing snapshot");
        QVERIFY_EXCEPTION_THROWN(db.createSnapshot(destination), Error);
        QCOMPARE(readFile(destination), QByteArray("existing snapshot"));
    }

    void resourceFileImportsPreserveOriginalsAndRejectInvalidOrCollidingSaves()
    {
        QTemporaryDir temporary, outside;
        auto library = Library::create(temporary.path());
        const QString course = addCourse(*library);
        const QString source = outside.filePath("Book with spaces.pdf");
        writeFile(source, "original book bytes");
        Resource book; book.kind = "book"; book.title = "My book"; book.courseId = course;
        Resource invalid = book; invalid.title.clear();
        QVERIFY_EXCEPTION_THROWN(library->saveResourceWithFile(invalid, source), Error);
        QVERIFY(library->snapshot().materials.isEmpty());
        const QString bookId = library->saveResourceWithFile(book, source);
        auto saved = library->snapshot();
        QCOMPARE(saved.resources.size(), 1); QCOMPARE(saved.materials.size(), 1);
        const QString materialId = saved.resources.first().materialId;
        const QString imported = library->materialPath(materialId);
        QCOMPARE(readFile(source), QByteArray("original book bytes"));
        QCOMPARE(readFile(imported), readFile(source));
        QVERIFY(saved.materials.first().path.contains("/Books/"));
        QVERIFY_EXCEPTION_THROWN(library->saveResourceWithFile(book, source), Error);
        QCOMPARE(library->snapshot().resources.size(), 1);
        QCOMPARE(readFile(imported), QByteArray("original book bytes"));
        Resource lecture; lecture.kind = "lecture"; lecture.title = "Lecture";
        lecture.courseId = course; lecture.date = QDate(2026, 10, 10); lecture.notes = "Notes stay";
        library->saveResourceWithFile(lecture, imported);
        saved = library->snapshot(); QCOMPARE(saved.materials.size(), 1);
        for (const auto& record : saved.resources) QCOMPARE(record.materialId, materialId);
        const QString lectureFile = outside.filePath("slides.txt"); writeFile(lectureFile, "slides bytes");
        library->saveResourceWithFile(lecture, lectureFile);
        QVERIFY(QFileInfo::exists(temporary.filePath(saved.courses.first().path + "/Lectures/slides.txt")));
        QCOMPARE(readFile(lectureFile), QByteArray("slides bytes"));
        Work cancelled; cancelled.cancelled->store(true);
        const QString cancelledFile = outside.filePath("cancelled.txt"); writeFile(cancelledFile);
        QVERIFY_EXCEPTION_THROWN(library->saveResourceWithFile(lecture, cancelledFile, cancelled), Error);
        QCOMPARE(library->snapshot().resources.size(), 3);
        book.id = bookId; book.title = "Edited book";
        library->saveResourceWithFile(book, imported);
        QCOMPARE(library->snapshot().resources.size(), 3);
        library.reset(); library = Library::open(temporary.path());
        QCOMPARE(library->snapshot().resources.size(), 3);
        QCOMPARE(library->snapshot().materials.size(), 2);
    }

    void offlineCatalogHasVerifiedCodesAndNoContradictoryRows()
    {
        const auto catalog = courseCatalog();
        QCOMPARE(catalog.size(), 157);
        QSet<QString> codes;
        bool oop = false;
        for (const auto& course : catalog) {
            QVERIFY(!codes.contains(course.code)); codes.insert(course.code);
            QVERIFY(!course.name.isEmpty()); QVERIFY(course.semester >= 1 && course.semester <= 8);
            QVERIFY(course.code != "F23L2S066"); QVERIFY(course.code != "F23L1S026");
            if (course.code == "F23L1S016") { QCOMPARE(course.name, QString("Object-oriented programming")); QCOMPARE(course.semester, 2); oop = true; }
        }
        QVERIFY(oop);
    }

    void invalidDatesNamesAndSameCourseAcrossSemesters()
    {
        QTemporaryDir temporary;
        auto library = Library::create(temporary.path());
        QVERIFY_EXCEPTION_THROWN(library->createSemester("bad", "winter", QDate(2027, 1, 1), QDate(2026, 1, 1)), Error);
        QVERIFY_EXCEPTION_THROWN(library->createSemester("bad", "guessed", QDate(2026, 1, 1), QDate(2027, 1, 1)), Error);
        const QString first = addCourse(*library);
        const QString summer = library->createSemester("Летен 2027", "summer", QDate(2027, 2, 18), QDate(2027, 9, 30));
        const QString second = library->createCourse(summer, "Математика", "", "#E63946");
        QVERIFY(first != second);
        QCOMPARE(library->snapshot().courses.size(), 2);
        QVERIFY_EXCEPTION_THROWN(library->createCourse(summer, "МАТЕМАТИКА", "", "#E63946"), Error);
        QVERIFY_EXCEPTION_THROWN(library->createCourse("missing", "Unknown", "", "#E63946"), Error);
    }
};

QTEST_GUILESS_MAIN(LibraryTest)
#include "library_test.moc"
