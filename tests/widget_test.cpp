#include "ui/mainwindow.h"
#include "ui/calendarpage.h"
#include "ui/calendarweek.h"
#include <QAbstractButton>
#include "services/librarycontroller.h"
#include <QAction>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QMimeData>
#include <QToolBar>
#include <QPlainTextEdit>
#include <QDateEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTreeWidget>
#include <QListWidget>
#include <QMessageBox>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QTimeEdit>
#include "services/calendar.h"
#include <QFileDialog>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

using namespace virgil;

class WidgetTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir preferences_;
    QUrl lastOpened_;

public slots:
    void captureUrl(const QUrl& url) { lastOpened_ = url; }

private slots:
    void initTestCase()
    {
        QVERIFY(preferences_.isValid());
        QCoreApplication::setOrganizationName("VirgilTests");
        QCoreApplication::setApplicationName("WidgetWorkflow");
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, preferences_.path());
        qRegisterMetaType<Snapshot>();
        qRegisterMetaType<DirectoryPreview>();
    }

    void init()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
        lastOpened_ = QUrl();
        QDesktopServices::setUrlHandler("file", this, "captureUrl");
    }

    void cleanup() { QDesktopServices::unsetUrlHandler("file"); QDesktopServices::unsetUrlHandler("https"); }

    void firstLaunchThroughFormsAndRestart()
    {
        QTemporaryDir libraryDirectory;
        QTemporaryDir sourceDirectory;
        const QString source = sourceDirectory.filePath("Лекција 1.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("An offline lecture.\n"), qint64(20));
        file.close();
        QString savedMaterialId;
        {
            MainWindow window;
            QSignalSpy ready(window.controller(), &LibraryController::snapshotReady);
            QSignalSpy errors(window.controller(), &LibraryController::failed);
            window.show();
            window.openRememberedLibrary();
            QVERIFY(!window.controller()->busy());
            QVERIFY(!window.findChild<QAction*>("semesterAction")->isEnabled());

            // Drive the real file picker, adoption review, forms and import dialog.
            QString pickerPath = libraryDirectory.path();
            int dialogsHandled = 0;
            QTimer dialogDriver;
            dialogDriver.setInterval(10);
            connect(&dialogDriver, &QTimer::timeout, this, [&] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog || dialog->property("testHandled").toBool()) return;
                if (auto* picker = qobject_cast<QFileDialog*>(dialog)) {
                    picker->setDirectory(QFileInfo(pickerPath).isDir() ? pickerPath : QFileInfo(pickerPath).absolutePath());
                    picker->selectFile(pickerPath);
                } else if (dialog->objectName() == "semesterDialog") {
                    dialog->findChild<QLineEdit*>("semesterName")->setText("Зимски 2026");
                } else if (dialog->objectName() == "courseDialog") {
                    dialog->findChild<QLineEdit*>("courseName")->setText("Математика");
                } else if (dialog->objectName() != "adoptDialog" && dialog->objectName() != "importDialog") {
                    return;
                }
                dialog->setProperty("testHandled", true);
                ++dialogsHandled;
                dialog->accept();
            });
            dialogDriver.start();
            window.findChild<QAction*>("createLibraryAction")->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
            QCOMPARE(errors.count(), 0);
            QCOMPARE(dialogsHandled, 2);
            window.findChild<QAction*>("semesterAction")->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
            window.findChild<QAction*>("courseAction")->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 10000);
            QVERIFY(window.findChild<QAction*>("importAction")->isEnabled());
            pickerPath = source;
            window.findChild<QAction*>("importAction")->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 4, 10000);
            dialogDriver.stop();
            QCOMPARE(dialogsHandled, 6);
            QCOMPARE(errors.count(), 0);
            const auto saved = qvariant_cast<Snapshot>(ready.last().at(0));
            QCOMPARE(saved.materials.size(), 1);
            savedMaterialId = saved.materials.first().id;
            auto* table = window.findChild<QTableView*>("materialsTable");
            QCOMPARE(table->model()->rowCount(), 1);
            table->selectRow(0);
            window.findChild<QAction*>("openMaterialAction")->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(!lastOpened_.isEmpty(), 10000);
            QVERIFY(lastOpened_.isLocalFile());
            QCOMPARE(lastOpened_.toLocalFile(), QDir(libraryDirectory.path()).filePath(saved.materials.first().path));
            QVERIFY(QFileInfo::exists(source));
            QCOMPARE(QSettings().value("library/lastPath").toString(), libraryDirectory.path());
            const QString screenshot = qEnvironmentVariable("VIRGIL_TEST_SCREENSHOT");
            if (!screenshot.isEmpty()) QVERIFY(window.grab().save(screenshot));
            window.close();
        }
        QVERIFY(!QFileInfo::exists(libraryDirectory.filePath(".virgil/library.lock")));
        {
            MainWindow reopened;
            QSignalSpy ready(reopened.controller(), &LibraryController::snapshotReady);
            reopened.openRememberedLibrary();
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
            const auto snapshot = qvariant_cast<Snapshot>(ready.last().at(0));
            QCOMPARE(snapshot.materials.first().id, savedMaterialId);
            QCOMPARE(reopened.findChild<QTableView*>("materialsTable")->model()->rowCount(), 1);
        }
    }

    void rememberedMissingLibraryOffersRecovery()
    {
        const QString missing = preferences_.filePath("unmounted-library");
        QSettings().setValue("library/lastPath", missing);
        MainWindow window;
        QSignalSpy errors(window.controller(), &LibraryController::failed);
        window.openRememberedLibrary();
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
        QVERIFY(window.findChild<QLabel*>("noticeLabel")->text().contains("unavailable"));
        QVERIFY(window.findChild<QAction*>("openLibraryAction")->isEnabled());
        QVERIFY(window.findChild<QAction*>("createLibraryAction")->isEnabled());
        QCOMPARE(QSettings().value("library/lastPath").toString(), missing);
    }

    void workerLeavesEventLoopResponsiveAndShutsDown()
    {
        QTemporaryDir libraryDirectory;
        QTemporaryDir sources;
        const QString source = sources.filePath("large.bin");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        // No old upload limit: this fixture is larger than 50 MB.
        for (int i = 0; i < 64; ++i) QCOMPARE(file.write(QByteArray(1024 * 1024, 'v')), qint64(1024 * 1024));
        file.close();
        {
            auto library = Library::create(libraryDirectory.path());
            auto semester = library->createSemester("Winter", "winter", QDate(2026, 10, 1), QDate(2027, 2, 17));
            library->createCourse(semester, "Math", "", "#2764C5");
        }
        LibraryController controller;
        QSignalSpy ready(&controller, &LibraryController::snapshotReady);
        QSignalSpy errors(&controller, &LibraryController::failed);
        bool wrongThread = false;
        connect(&controller, &LibraryController::snapshotReady, this, [&] {
            if (QThread::currentThread() != qApp->thread()) wrongThread = true;
        });
        controller.openLibrary(libraryDirectory.path());
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        int heartbeats = 0;
        QTimer heartbeat;
        heartbeat.setInterval(0);
        connect(&heartbeat, &QTimer::timeout, this, [&] { if (controller.busy()) ++heartbeats; });
        heartbeat.start();
        controller.importFile(source, "Winter/Math/Books", "large.bin");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
        heartbeat.stop();
        QVERIFY(heartbeats > 0);
        QVERIFY(!wrongThread);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(qvariant_cast<Snapshot>(ready.last().at(0)).materials.first().size, qint64(64 * 1024 * 1024));
        {
            // Destruction during a queued job must cancel/join, never terminate it.
            QTemporaryDir another;
            auto controllerToClose = std::make_unique<LibraryController>();
            controllerToClose->createLibrary(another.path(), false);
            controllerToClose.reset();
            QVERIFY(!QFileInfo::exists(another.filePath(".virgil/library.lock")));
        }
    }

    void watchesNestedDirectoriesEditsAndMoves()
    {
        QTemporaryDir temporary;
        { auto library = Library::create(temporary.path()); library->createFolder("", "Nested"); library->refresh(); }
        LibraryController controller;
        QSignalSpy ready(&controller, &LibraryController::snapshotReady);
        QSignalSpy failures(&controller, &LibraryController::failed);
        controller.openLibrary(temporary.path());
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        auto latest = [&] { return qvariant_cast<Snapshot>(ready.last().at(0)); };
        auto write = [&](const QString& path, const QByteArray& bytes) {
            QFile file(temporary.filePath(path)); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size()));
        };
        write("Nested/a.txt", "first");
        QTRY_COMPARE_WITH_TIMEOUT(latest().materials.size(), 1, 10000);
        const QString id = latest().materials.first().id;
        write("Nested/a.txt", "changed bytes");
        QTRY_COMPARE_WITH_TIMEOUT(latest().materials.first().size, qint64(13), 10000);
        QVERIFY(QDir().mkdir(temporary.filePath("Nested/Deeper")));
        write("Nested/Deeper/new.xyz", "unknown format");
        QTRY_COMPARE_WITH_TIMEOUT(latest().materials.size(), 2, 10000);
        QVERIFY(QFile::rename(temporary.filePath("Nested/a.txt"), temporary.filePath("Nested/Deeper/moved.txt")));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const auto& m : latest().materials) if (m.id == id && m.path == "Nested/Deeper/moved.txt") return true;
            return false;
        }(), 10000);
        QVERIFY(QFile::remove(temporary.filePath("Nested/Deeper/new.xyz")));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const auto& m : latest().materials) if (m.path == "Nested/Deeper/new.xyz") return !m.available;
            return false;
        }(), 10000);
        QCOMPARE(failures.count(), 0);
    }

    void watchLimitFallsBackToPeriodicReconciliation()
    {
        QTemporaryDir temporary;
        { auto library = Library::create(temporary.path()); }
        for (int i = 0; i < 4100; ++i) {
            QFile file(temporary.filePath(QString("file-%1.txt").arg(i, 5, 10, QChar('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("x"), qint64(1));
        }
        LibraryController controller;
        QSignalSpy ready(&controller, &LibraryController::snapshotReady);
        int heartbeats = 0;
        QTimer heartbeat; heartbeat.setInterval(0);
        connect(&heartbeat, &QTimer::timeout, this, [&] { if (controller.busy()) ++heartbeats; });
        heartbeat.start();
        controller.openLibrary(temporary.path());
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        heartbeat.stop();
        QVERIFY(heartbeats > 0);
        auto latest = [&] { return qvariant_cast<Snapshot>(ready.last().at(0)); };
        QVERIFY(latest().notices.join(' ').contains("watch limit"));
        {
            QFile last(temporary.filePath("file-04099.txt"));
            QVERIFY(last.open(QIODevice::WriteOnly)); QCOMPARE(last.write("unwatched edit"), qint64(14));
        }
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const auto& m : latest().materials) if (m.path == "file-04099.txt") return m.size == 14;
            return false;
        }(), 35000);
        QVERIFY(latest().notices.join(' ').contains("Periodic reconciliation found changes"));
    }

    void nestedNavigationBreadcrumbsAndDroppedImports()
    {
        QTemporaryDir temporary;
        QTemporaryDir sources;
        {
            auto library = Library::create(temporary.path());
            library->createFolder("", "Nested"); library->createFolder("Nested", "Deeper");
            library->refresh();
        }
        QFile file(sources.filePath("drop.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("dropped bytes"), qint64(13)); file.close();
        MainWindow window;
        window.show();
        QSignalSpy ready(window.controller(), &LibraryController::snapshotReady);
        window.controller()->openLibrary(temporary.path());
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        auto* folders = window.findChild<QComboBox*>("folderNavigation");
        auto* table = window.findChild<QTableView*>("materialsTable");
        auto* crumbs = window.findChild<QToolBar*>("folderBreadcrumbs");
        QVERIFY(folders); QVERIFY(table); QVERIFY(crumbs);
        folders->setCurrentIndex(folders->findData("Nested"));
        QCOMPARE(table->model()->rowCount(), 1); // empty child folders are navigable
        QCOMPARE(table->model()->index(0, 0).data().toString(), QString("Deeper/"));
        QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(table->model()->index(0, 0)).center());
        // Exercise the same activated row signal reliably on offscreen platforms.
        table->selectRow(0);
        QMetaObject::invokeMethod(table, "doubleClicked", Q_ARG(QModelIndex, table->model()->index(0, 0)));
        QCOMPARE(folders->currentData().toString(), QString("Nested/Deeper"));
        QCOMPARE(crumbs->actions().size(), 3);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(sources.filePath("drop.txt"))});
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(table->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(table->viewport(), &drop);
        QVERIFY(drop.isAccepted());
        QTRY_VERIFY_WITH_TIMEOUT(!window.controller()->busy(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 1, 10000);
        QCOMPARE(table->model()->index(0, 0).data().toString(), QString("drop.txt"));
        QVERIFY(QFileInfo::exists(sources.filePath("drop.txt")));
        QVERIFY(QFileInfo::exists(temporary.filePath("Nested/Deeper/drop.txt")));
        table->selectRow(0);
        const QString selectedId = table->selectionModel()->selectedRows().first().data(Qt::UserRole).toString();
        const qsizetype beforeRefresh = ready.count();
        window.controller()->refresh();
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() > beforeRefresh, 10000);
        QVERIFY(!table->selectionModel()->selectedRows().isEmpty());
        QCOMPARE(table->selectionModel()->selectedRows().first().data(Qt::UserRole).toString(), selectedId);
        crumbs->actions().first()->trigger();
        QCOMPARE(folders->currentData().toString(), QString(""));
        const QString screenshot = qEnvironmentVariable("VIRGIL_M2_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            folders->setCurrentIndex(folders->findData("Nested/Deeper"));
            QTest::qWait(50);
            QVERIFY(window.grab().save(screenshot));
        }
    }

    void visibleDeleteCourseButtonWorksWithMissingFolder()
    {
        QTemporaryDir temporary;
        {
            auto library = Library::create(temporary.path());
            const auto semester = library->createSemester("Winter", "winter", QDate(2026, 10, 1), QDate(2027, 2, 17));
            library->createCourse(semester, "Missing course", "MISSING", courseColors.first());
            QVERIFY(QDir(temporary.filePath("Winter/Missing course")).removeRecursively());
        }
        MainWindow window; window.show();
        QSignalSpy ready(window.controller(), &LibraryController::snapshotReady);
        QSignalSpy failures(window.controller(), &LibraryController::failed);
        window.controller()->openLibrary(temporary.path());
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        auto* action = window.findChild<QAction*>("deleteCourseAction");
        auto* button = window.findChild<QPushButton*>("deleteCourseButton");
        QVERIFY(action->isEnabled()); QVERIFY(button->isVisible()); QVERIFY(button->isEnabled());
        QTimer driver; driver.setInterval(10);
        connect(&driver, &QTimer::timeout, this, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog && dialog->objectName() == "deleteCourseDialog") dialog->accept();
        });
        driver.start(); button->click(); driver.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!window.controller()->busy(), 10000);
        QVERIFY(qvariant_cast<Snapshot>(ready.last().at(0)).courses.isEmpty());
        QVERIFY(!action->isEnabled()); QCOMPARE(failures.count(), 0);
        const auto id = window.findChild<QTreeWidget*>("courseTree")->currentItem()->data(0, Qt::UserRole).toString();
        QCOMPARE(id, QString("unassigned"));
    }

    void resourceFormsMetadataSearchAndAttachmentPreservation()
    {
        QTemporaryDir temporary;
        QString materialId;
        {
            auto library = Library::create(temporary.path());
            const auto semester = library->createSemester("Winter", "winter", QDate(2026, 10, 1), QDate(2027, 2, 17));
            library->createCourse(semester, "Math", "MATH", courseColors.first());
            QFile file(temporary.filePath("Winter/Math/Books/book.pdf"));
            QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("book bytes"), qint64(10)); file.close();
            materialId = library->refresh().materials.first().id;
        }
        MainWindow window; window.show();
        QSignalSpy ready(window.controller(), &LibraryController::snapshotReady);
        QSignalSpy failures(window.controller(), &LibraryController::failed);
        window.controller()->openLibrary(temporary.path());
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        auto latest = [&] { return qvariant_cast<Snapshot>(ready.last().at(0)); };
        QTimer driver; driver.setInterval(10);
        connect(&driver, &QTimer::timeout, this, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->property("handled").toBool()) return;
            if (dialog->objectName() == "bookDialog") {
                dialog->findChild<QLineEdit*>("resourceTitle")->setText("Local C++ book");
                auto* attachment = dialog->findChild<QComboBox*>("resourceAttachment");
                attachment->setCurrentIndex(attachment->findData(materialId));
            } else if (dialog->objectName() == "lectureDialog") {
                dialog->findChild<QLineEdit*>("resourceTitle")->setText("Ownership lecture");
                dialog->findChild<QDateEdit*>("lectureDate")->setDate(QDate(2026, 10, 12));
                dialog->findChild<QPlainTextEdit*>("lectureNotes")->setPlainText("Unique RAII note keyword • Белешки");
                auto* attachment = dialog->findChild<QComboBox*>("resourceAttachment");
                attachment->setCurrentIndex(attachment->findData(materialId));
                const QString screenshot = qEnvironmentVariable("VIRGIL_M3_FORM_SCREENSHOT");
                if (!screenshot.isEmpty()) QVERIFY(dialog->grab().save(screenshot));
            } else if (dialog->objectName() == "bookmarkDialog") {
                dialog->findChild<QLineEdit*>("resourceTitle")->setText("Course reference");
                dialog->findChild<QLineEdit*>("bookmarkUrl")->setText("https://example.org/reference");
            } else if (dialog->objectName() == "materialMetadataDialog") {
                dialog->findChild<QLineEdit*>("materialTitle")->setText("Midterm display title");
                auto* category = dialog->findChild<QComboBox*>("category"); category->setCurrentIndex(category->findData("past-exam"));
                dialog->findChild<QCheckBox*>("hasExamDate")->setChecked(true);
                dialog->findChild<QDateEdit*>("examDate")->setDate(QDate(2026, 11, 2));
            } else if (auto* message = qobject_cast<QMessageBox*>(dialog)) {
                dialog->setProperty("handled", true); message->button(QMessageBox::Yes)->click(); return;
            } else return;
            dialog->setProperty("handled", true); dialog->accept();
        });
        driver.start();
        window.findChild<QAction*>("bookAction0")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(latest().resources.size(), 1, 10000);
        window.findChild<QAction*>("lectureAction0")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(latest().resources.size(), 2, 10000);
        window.findChild<QAction*>("bookmarkAction0")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(latest().resources.size(), 3, 10000);
        auto* books = window.findChild<QTableView*>("bookTable");
        auto* lectures = window.findChild<QTableView*>("lectureTable");
        auto* bookmarks = window.findChild<QTableView*>("bookmarkTable");
        QCOMPARE(books->model()->rowCount(), 1); QCOMPARE(lectures->model()->rowCount(), 1); QCOMPARE(bookmarks->model()->rowCount(), 1);
        auto* search = window.findChild<QLineEdit*>("metadataSearch");
        search->setText("RAII"); QCOMPARE(lectures->model()->rowCount(), 1); QCOMPARE(books->model()->rowCount(), 0);
        search->setText("nonsense unmatched"); QCOMPARE(lectures->model()->rowCount(), 0);
        search->clear();
        auto* materials = window.findChild<QTableView*>("materialsTable");
        materials->selectRow(0); window.findChild<QAction*>("editMaterialAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(latest().materials.first().title, QString("Midterm display title"), 10000);
        QCOMPARE(latest().materials.first().category, QString("past-exam")); QCOMPARE(latest().materials.first().examDate, QDate(2026, 11, 2));
        search->setText("Midterm"); QCOMPARE(materials->model()->rowCount(), 1);
        search->clear();
        books->selectRow(0); window.findChild<QAction*>("bookAction3")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!lastOpened_.isEmpty(), 10000);
        QCOMPARE(lastOpened_.toLocalFile(), temporary.filePath("Winter/Math/Books/book.pdf"));
        QDesktopServices::setUrlHandler("https", this, "captureUrl");
        bookmarks->selectRow(0); window.findChild<QAction*>("bookmarkAction3")->trigger();
        QCOMPARE(lastOpened_, QUrl("https://example.org/reference"));
        QDesktopServices::unsetUrlHandler("https");
        window.findChild<QTabWidget*>("courseResourceTabs")->setCurrentIndex(2);
        const QString screenshot = qEnvironmentVariable("VIRGIL_M3_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(50); QVERIFY(window.grab().save(screenshot)); }
        lectures->selectRow(0); window.findChild<QAction*>("lectureAction2")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(latest().resources.size(), 2, 10000);
        QVERIFY(QFileInfo::exists(temporary.filePath("Winter/Math/Books/book.pdf")));
        QCOMPARE(latest().materials.first().id, materialId);
        driver.stop(); QCOMPARE(failures.count(), 0);
    }

    void resourceBrowseChoosesExternalFilesAndCancelKeepsLibraryUntouched()
    {
        QTemporaryDir temporary, outside;
        const QString source = outside.filePath("External attachment.txt");
        { QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("outside bytes"), qint64(13)); }
        QString courseId;
        {
            auto library = Library::create(temporary.path());
            const auto semester = library->createSemester("Winter", "winter", QDate(2026, 10, 1), QDate(2027, 2, 17));
            courseId = library->createCourse(semester, "Math", "MATH", courseColors.first());
        }
        MainWindow window; window.show();
        QSignalSpy ready(window.controller(), &LibraryController::snapshotReady);
        QSignalSpy failures(window.controller(), &LibraryController::failed);
        window.controller()->openLibrary(temporary.path());
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        auto latest = [&] { return qvariant_cast<Snapshot>(ready.last().at(0)); };
        bool cancelForm = true;
        QTimer driver; driver.setInterval(10);
        connect(&driver, &QTimer::timeout, this, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->property("handled").toBool()) return;
            if (auto* picker = qobject_cast<QFileDialog*>(dialog)) {
                dialog->setProperty("handled", true);
                QCOMPARE(picker->objectName(), QString("resourceFilePicker"));
                QCOMPARE(picker->fileMode(), QFileDialog::ExistingFile);
                QCOMPARE(picker->nameFilters(), QStringList{"All files (*)"});
                picker->setDirectory(outside.path()); picker->selectFile(source);
                QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
                return;
            }
            if (dialog->objectName() != "bookDialog" && dialog->objectName() != "lectureDialog") return;
            dialog->setProperty("handled", true);
            // Leave this timer callback before opening the nested file dialog,
            // so the same timer can drive the chooser's event loop.
            QTimer::singleShot(0, dialog, [&, dialog] {
                auto* course = dialog->findChild<QComboBox*>("resourceCourse");
                course->setCurrentIndex(course->findData(courseId));
                dialog->findChild<QPushButton*>("browseResourceFile")->click();
                QCOMPARE(dialog->findChild<QLineEdit*>("resourceTitle")->text(), QString("External attachment"));
                QVERIFY(dialog->findChild<QLabel*>("resourceFileSelection")->text().contains(source));
                if (auto* notes = dialog->findChild<QPlainTextEdit*>("lectureNotes")) notes->setPlainText("Browser-selected lecture notes");
                if (cancelForm) dialog->reject(); else dialog->accept();
            });
        });
        driver.start();
        window.findChild<QAction*>("bookAction0")->trigger();
        QVERIFY(latest().resources.isEmpty()); QVERIFY(latest().materials.isEmpty());
        QVERIFY(!QFileInfo::exists(temporary.filePath("Winter/Math/Books/External attachment.txt")));
        cancelForm = false;
        window.findChild<QAction*>("bookAction0")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(latest().resources.size(), 1, 10000);
        QVERIFY(QFileInfo::exists(temporary.filePath("Winter/Math/Books/External attachment.txt")));
        window.findChild<QAction*>("lectureAction0")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(latest().resources.size(), 2, 10000);
        QVERIFY(QFileInfo::exists(temporary.filePath("Winter/Math/Lectures/External attachment.txt")));
        QFile original(source); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("outside bytes"));
        driver.stop(); QCOMPARE(failures.count(), 0);
    }

    void organizationFormsAndGlobalMetadataSearch()
    {
        QTemporaryDir temporary;
        QString mathId, summerId;
        {
            auto library = Library::create(temporary.path());
            const auto winter = library->createSemester("Winter", "winter", QDate(2026, 10, 1), QDate(2027, 2, 17));
            mathId = library->createCourse(winter, "Math", "MATH", courseColors.first());
            summerId = library->createSemester("Summer", "summer", QDate(2027, 2, 18), QDate(2027, 9, 30));
            library->createCourse(summerId, "Physics", "PHYS", courseColors[1]);
            for (const auto& path : {QString("Winter/Math/Books/math.txt"), QString("Summer/Physics/Books/physics.txt")}) {
                QFile file(temporary.filePath(path)); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("notes"), qint64(5));
            }
            library->refresh();
        }
        MainWindow window; window.show();
        QSignalSpy ready(window.controller(), &LibraryController::snapshotReady);
        window.controller()->openLibrary(temporary.path()); QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        auto* tree = window.findChild<QTreeWidget*>("courseTree");
        QTreeWidgetItemIterator it(tree);
        while (*it) { if ((*it)->data(0, Qt::UserRole).toString() == mathId) { tree->setCurrentItem(*it); break; } ++it; }
        auto latest = [&] { return qvariant_cast<Snapshot>(ready.last().at(0)); };
        QTimer driver; driver.setInterval(10);
        connect(&driver, &QTimer::timeout, this, [&]( ) {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (dialog->objectName() == "editCourseDialog") {
                dialog->findChild<QLineEdit*>("organizationName")->setText("Calculus");
                dialog->findChild<QLineEdit*>("organizationCode")->setText("CALC");
                auto* semester = dialog->findChild<QComboBox*>("courseSemester"); semester->setCurrentIndex(semester->findData(summerId));
                auto* color = dialog->findChild<QComboBox*>("courseColor"); color->setCurrentIndex(color->findData(courseColors[3]));
                dialog->accept();
            } else if (dialog->objectName() == "editSemesterDialog") {
                dialog->findChild<QLineEdit*>("organizationName")->setText("My summer"); dialog->accept();
            }
        });
        driver.start(); window.findChild<QAction*>("editCourseAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!window.controller()->busy(), 10000);
        bool found = false;
        for (const auto& course : latest().courses) if (course.id == mathId) {
            QCOMPARE(course.name, QString("Calculus")); QCOMPARE(course.path, QString("Winter/Math")); QCOMPARE(course.semesterId, summerId); found = true;
        }
        QVERIFY(found);
        window.findChild<QAction*>("editSemesterAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!window.controller()->busy(), 10000); driver.stop();
        for (const auto& semester : latest().semesters) if (semester.id == summerId) QCOMPARE(semester.name, QString("My summer"));
        auto* search = window.findChild<QLineEdit*>("metadataSearch");
        auto* all = window.findChild<QCheckBox*>("searchAllCourses");
        auto* materials = window.findChild<QTableView*>("materialsTable");
        search->setText("PHYS"); QCOMPARE(materials->model()->rowCount(), 0);
        all->setChecked(true); QCOMPARE(materials->model()->rowCount(), 1);
        QCOMPARE(materials->model()->index(0, 0).data().toString(), QString("physics.txt"));
        search->setText("CALC"); QCOMPARE(materials->model()->rowCount(), 1);
        QCOMPARE(materials->model()->index(0, 0).data().toString(), QString("math.txt"));
    }

    void searchableOfflineCatalogFillsManualCourseForm()
    {
        QTemporaryDir temporary;
        { auto library = Library::create(temporary.path()); library->createSemester("Summer", "summer", QDate(2027, 2, 18), QDate(2027, 9, 30)); }
        MainWindow window; window.show();
        QSignalSpy ready(window.controller(), &LibraryController::snapshotReady);
        window.controller()->openLibrary(temporary.path()); QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        QTimer driver; driver.setInterval(10);
        bool selectedCatalog = false;
        connect(&driver, &QTimer::timeout, this, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (dialog->objectName() == "courseDialog") {
                if (!dialog->property("catalogOpened").toBool()) {
                    dialog->setProperty("catalogOpened", true);
                    auto* button = dialog->findChild<QPushButton*>("chooseCatalogButton");
                    QTimer::singleShot(0, button, &QPushButton::click);
                } else if (selectedCatalog) dialog->accept();
            } else if (dialog->objectName() == "catalogDialog") {
                dialog->findChild<QLineEdit*>("catalogSearch")->setText("F23L1S016");
                auto* list = dialog->findChild<QListWidget*>("catalogCourses");
                int visible = 0;
                for (int row = 0; row < list->count(); ++row) if (!list->item(row)->isHidden()) { list->setCurrentRow(row); ++visible; }
                QCOMPARE(visible, 1); selectedCatalog = true; dialog->accept();
            }
        });
        driver.start(); window.findChild<QAction*>("courseAction")->trigger(); driver.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!window.controller()->busy(), 10000);
        const auto snapshot = qvariant_cast<Snapshot>(ready.last().at(0));
        QCOMPARE(snapshot.courses.size(), 1); QCOMPARE(snapshot.courses.first().code, QString("F23L1S016"));
        QCOMPARE(snapshot.courses.first().name, QString("Object-oriented programming"));
    }

    void calendarEventFormsHandleClockChangesCompletionAndColorNames()
    {
        QTemporaryDir temporary; QString courseId, semesterId;
        {
            auto library = Library::create(temporary.path());
            semesterId = library->createSemester("Winter", "winter", QDate(2026, 9, 1), QDate(2027, 2, 28));
            courseId = library->createCourse(semesterId, "Math", "MATH", courseColors.first());
        }
        MainWindow window; window.show();
        QSignalSpy ready(window.controller(), &LibraryController::snapshotReady), failures(window.controller(), &LibraryController::failed);
        window.controller()->openLibrary(temporary.path()); QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        auto latest = [&] { return qvariant_cast<Snapshot>(ready.last().at(0)); };
        int formMode = 0;
        QTimer driver; driver.setInterval(10);
        connect(&driver, &QTimer::timeout, this, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->property("handled").toBool()) return;
            dialog->setProperty("handled", true);
            if (dialog->objectName() == "editCourseDialog") {
                auto* color = dialog->findChild<QComboBox*>("courseColor");
                QCOMPARE(color->count(), 10);
                for (int i = 0; i < color->count(); ++i) { QCOMPARE(color->itemText(i), courseColorNames[i]); QCOMPARE(color->itemData(i).toString(), courseColors[i]); }
                color->setCurrentIndex(color->findData(courseColors[7])); dialog->accept(); return;
            }
            if (auto* message = qobject_cast<QMessageBox*>(dialog)) { message->button(QMessageBox::Yes)->click(); return; }
            QCOMPARE(dialog->objectName(), QString("calendarEventDialog"));
            auto* save = dialog->findChild<QDialogButtonBox*>("calendarDialogButtons")->button(QDialogButtonBox::Save);
            auto* title = dialog->findChild<QLineEdit*>("eventTitle"); auto* kind = dialog->findChild<QComboBox*>("eventKind");
            auto* course = dialog->findChild<QComboBox*>("eventCourse"); course->setCurrentIndex(course->findData(courseId));
            if (formMode == 0) {
                title->setText("Homework"); kind->setCurrentIndex(kind->findData("assignment"));
                dialog->findChild<QDateEdit*>("eventDate")->setDate(QDate(2026, 10, 12));
                save->click();
            } else if (formMode == 1) {
                title->setText("Timed exam"); kind->setCurrentIndex(kind->findData("exam")); dialog->findChild<QCheckBox*>("eventAllDay")->setChecked(false);
                auto* date = dialog->findChild<QDateEdit*>("eventDate"); auto* end = dialog->findChild<QDateEdit*>("eventEndDate");
                auto* starts = dialog->findChild<QTimeEdit*>("eventStartTime"); auto* ends = dialog->findChild<QTimeEdit*>("eventEndTime");
                date->setDate(QDate(2026, 3, 29)); end->setDate(date->date()); starts->setTime(QTime(2, 30)); ends->setTime(QTime(3, 30));
                save->click(); QVERIFY(dialog->isVisible()); QVERIFY(dialog->findChild<QLabel*>("calendarFormError")->text().contains("does not exist"));
                date->setDate(QDate(2026, 10, 25)); end->setDate(date->date());
                save->click(); QVERIFY(dialog->isVisible()); QVERIFY(dialog->findChild<QLabel*>("calendarFormError")->text().contains("occurs twice"));
                auto* resolution = dialog->findChild<QComboBox*>("eventStartResolution"); resolution->setCurrentIndex(resolution->findData("later")); save->click();
            } else {
                QCOMPARE(dialog->findChild<QComboBox*>("eventStartResolution")->currentData().toString(), QString("later"));
                title->setText("Edited timed exam"); save->click();
            }
        });
        driver.start();
        window.findChild<QAction*>("editCourseAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(latest().courses.first().color, courseColors[7], 10000);
        window.findChild<QListWidget*>("navigation")->setCurrentRow(2);
        window.findChild<QPushButton*>("calendarNewEvent")->click(); QTRY_COMPARE_WITH_TIMEOUT(latest().events.size(), 1, 10000);
        const QString assignmentId = latest().events.first().id;
        auto* anchor = window.findChild<QDateEdit*>("calendarDate"); anchor->setDate(QDate(2026, 10, 12));
        auto* agenda = window.findChild<QTreeWidget*>("calendarAgenda"); QCOMPARE(agenda->topLevelItemCount(), 1); agenda->setCurrentItem(agenda->topLevelItem(0));
        window.findChild<QPushButton*>("calendarComplete")->click();
        QTRY_VERIFY_WITH_TIMEOUT(latest().events.first().completed, 10000);
        QVERIFY(window.findChild<QPushButton*>("calendarComplete")->text().contains("incomplete"));
        formMode = 1; window.findChild<QPushButton*>("calendarNewEvent")->click(); QTRY_COMPARE_WITH_TIMEOUT(latest().events.size(), 2, 10000);
        QString timedId;
        for (const auto& event : latest().events) if (event.kind == "exam") { timedId = event.id; QCOMPARE(event.startsAt, QDateTime::fromString("2026-10-25T01:30:00Z", Qt::ISODate)); }
        anchor->setDate(QDate(2026, 10, 25));
        for (int i = 0; i < agenda->topLevelItemCount(); ++i) if (agenda->topLevelItem(i)->data(0, Qt::UserRole).toString() == timedId) agenda->setCurrentItem(agenda->topLevelItem(i));
        formMode = 2; window.findChild<QPushButton*>("calendarEditEvent")->click();
        QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& event : latest().events) if (event.id == timedId) return event.title == "Edited timed exam"; return false; }(), 10000);
        window.findChild<QPushButton*>("calendarDeleteEvent")->click(); QTRY_COMPARE_WITH_TIMEOUT(latest().events.size(), 1, 10000);
        QCOMPARE(latest().events.first().id, assignmentId);
        driver.stop(); QCOMPARE(failures.count(), 0);
    }

    void calendarTimetableExamPeriodsNavigationAndSemesterFilter()
    {
        QTemporaryDir temporary; QString firstSemester, secondSemester, courseId;
        {
            auto library = Library::create(temporary.path());
            firstSemester = library->createSemester("Winter", "winter", QDate(2026, 9, 1), QDate(2027, 2, 28));
            secondSemester = library->createSemester("Summer", "summer", QDate(2027, 2, 1), QDate(2027, 8, 31));
            courseId = library->createCourse(firstSemester, "Math", "MATH", courseColors[6]);
            CalendarEvent exam; exam.title = "Independent saved exam"; exam.semesterId = firstSemester; exam.date = QDate(2026, 11, 18); library->saveCalendarEvent(exam);
            exam.title = "Global event"; exam.semesterId.clear(); library->saveCalendarEvent(exam);
        }
        MainWindow window; window.resize(1250, 850); window.show();
        QSignalSpy ready(window.controller(), &LibraryController::snapshotReady), failures(window.controller(), &LibraryController::failed);
        window.controller()->openLibrary(temporary.path()); QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 10000);
        auto latest = [&] { return qvariant_cast<Snapshot>(ready.last().at(0)); };
        window.findChild<QListWidget*>("navigation")->setCurrentRow(2);
        auto* semester = window.findChild<QComboBox*>("calendarSemester"); semester->setCurrentIndex(semester->findData(firstSemester));
        int rangeNumber = 0; bool editingRange = false; bool customPolicy = false;
        QTimer driver; driver.setInterval(10);
        connect(&driver, &QTimer::timeout, this, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->property("handled").toBool()) return;
            dialog->setProperty("handled", true);
            if (auto* message = qobject_cast<QMessageBox*>(dialog)) { message->button(QMessageBox::Yes)->click(); return; }
            auto* save = dialog->findChild<QDialogButtonBox*>("calendarDialogButtons")->button(QDialogButtonBox::Save);
            if (dialog->objectName() == "weeklyClassDialog") {
                auto* course = dialog->findChild<QComboBox*>("eventCourse"); course->setCurrentIndex(course->findData(courseId));
                if (dialog->windowTitle().startsWith("Edit")) {
                    dialog->findChild<QLineEdit*>("classTitle")->setText("Edited weekly lecture");
                    auto* room = dialog->findChild<QLineEdit*>("classRoom1");
                    QCOMPARE(room->text(), QString("101")); room->setText("102");
                    dialog->findChild<QTimeEdit*>("classStart1")->setTime(QTime(10, 0));
                    dialog->findChild<QTimeEdit*>("classEnd1")->setTime(QTime(11, 30));
                } else {
                    dialog->findChild<QCheckBox*>("classDay1")->setChecked(true); dialog->findChild<QCheckBox*>("classDay3")->setChecked(true);
                    auto* kind = dialog->findChild<QComboBox*>("classKind3");
                    QCOMPARE(kind->count(), 3);
                    kind->setCurrentIndex(kind->findData("auditory"));
                    dialog->findChild<QLineEdit*>("classRoom1")->setText("101");
                    dialog->findChild<QLineEdit*>("classRoom3")->setText("Лаб 204");
                }
                save->click();
            } else if (dialog->objectName() == "examRangeDialog") {
                const QList<QDate> starts{QDate(2026, 11, 12), QDate(2026, 12, 10), QDate(2027, 1, 10)};
                const QList<QDate> ends{QDate(2026, 11, 23), QDate(2026, 12, 17), QDate(2027, 1, 20)};
                dialog->findChild<QDateEdit*>("rangeStart")->setDate(editingRange ? QDate(2026, 11, 10) : starts[rangeNumber]);
                dialog->findChild<QDateEdit*>("rangeEnd")->setDate(editingRange ? QDate(2026, 11, 23) : ends[rangeNumber]); save->click();
            } else if (dialog->objectName() == "calendarSemesterDialog") {
                dialog->findChild<QCheckBox*>("customTeaching")->setChecked(customPolicy);
                if (customPolicy) { dialog->findChild<QDateEdit*>("teachingStart")->setDate(QDate(2026, 9, 1)); dialog->findChild<QDateEdit*>("teachingEnd")->setDate(QDate(2026, 9, 30)); }
                save->click();
            } else if (dialog->objectName() == "calendarTimezoneDialog") {
                dialog->findChild<QComboBox*>("calendarZone")->setCurrentText("UTC"); save->click();
            } else QFAIL("Unexpected calendar form");
        }); driver.start();
        window.findChild<QPushButton*>("calendarNewClasses")->click(); QTRY_COMPARE_WITH_TIMEOUT(latest().weeklyClasses.size(), 2, 10000);
        auto* timetable = window.findChild<QTreeWidget*>("calendarClasses"); timetable->setCurrentItem(timetable->topLevelItem(0));
        const QString classId = timetable->currentItem()->data(0, Qt::UserRole).toString();
        window.findChild<QPushButton*>("calendarEditClass")->click();
        QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& slot : latest().weeklyClasses) if (slot.id == classId) return slot.title == "Edited weekly lecture" && slot.start == QTime(10, 0) && slot.room == "102"; return false; }(), 10000);
        for (rangeNumber = 0; rangeNumber < 3; ++rangeNumber) {
            window.findChild<QPushButton*>("calendarNewRange")->click(); QTRY_COMPARE_WITH_TIMEOUT(latest().examRanges.size(), rangeNumber + 1, 10000);
        }
        QVERIFY(!window.findChild<QPushButton*>("calendarNewRange")->isEnabled());
        auto* anchor = window.findChild<QDateEdit*>("calendarDate"); auto* view = window.findChild<QComboBox*>("calendarView"); auto* grid = window.findChild<QTableWidget*>("calendarGrid");
        view->setCurrentIndex(1); anchor->setDate(QDate(2026, 11, 9));
        QVERIFY(window.findChild<CalendarWeek*>("calendarWeek")->isVisible());
        QCOMPARE(grid->columnCount(), 6); QCOMPARE(grid->horizontalHeaderItem(0)->text(), QString("Monday")); QCOMPARE(grid->horizontalHeaderItem(5)->text(), QString("Saturday"));
        QVERIFY(window.findChild<QPushButton*>("calendarDay-2026-11-09"));
        QVERIFY(!window.findChild<QPushButton*>("calendarDay-2026-11-15"));
        QVERIFY(!window.findChild<QLabel*>("calendarPolicy"));
        auto checkAuditory = [&] {
            auto* entries = window.findChild<QListWidget*>("calendarEntries-2026-11-11");
            QVERIFY(entries); QCOMPARE(entries->count(), 1);
            QVERIFY(entries->item(0)->text().contains("[AUDITORY]"));
            QVERIFY(entries->item(0)->toolTip().contains("Auditory Exercises"));
            QVERIFY(entries->item(0)->toolTip().contains("Room: Лаб 204"));
            QVERIFY(entries->item(0)->data(Qt::AccessibleTextRole).toString().contains("Лаб 204"));
            QImage image(240, 70, QImage::Format_ARGB32); image.fill(Qt::white);
            QPainter painter(&image);
            QStyleOptionViewItem option; option.rect = image.rect(); option.font = entries->font();
            entries->itemDelegate()->paint(&painter, option, entries->model()->index(0, 0));
            painter.end();
            QCOMPARE(image.pixelColor(237, 2), QColor("#808080"));
            // Text must actually be drawn in the top-left header, above the class title.
            QFont small = entries->font(); small.setPointSizeF(small.pointSizeF() * 0.8);
            const int headerHeight = QFontMetrics(small).height() + 4;
            bool hasRoomInk = false;
            const QColor background = image.pixelColor(0, 0);
            for (int y = 2; y < headerHeight - 2; ++y)
                for (int x = 3; x < 80; ++x) if (image.pixelColor(x, y) != background) hasRoomInk = true;
            QVERIFY(hasRoomInk);
        };
        QString auditoryId;
        for (const auto& slot : latest().weeklyClasses) if (slot.kind == "auditory") auditoryId = slot.id;
        auto* auditoryCard = window.findChild<QAbstractButton*>("calendarWeekEntry-" + auditoryId + "-2026-11-11");
        QVERIFY(auditoryCard); QVERIFY(auditoryCard->text().contains("[AUDITORY]"));
        QVERIFY(!auditoryCard->text().contains("Auditory Exercises"));
        QVERIFY(auditoryCard->accessibleName().contains("Лаб 204"));
        view->setCurrentIndex(0); checkAuditory(); view->setCurrentIndex(1);
        auto* agenda = window.findChild<QTreeWidget*>("calendarAgenda"); QCOMPARE(agenda->topLevelItemCount(), 2);
        anchor->setDate(QDate(2026, 11, 16)); QCOMPARE(agenda->topLevelItemCount(), 2); // Saved events remain during the first period.
        for (int i = 0; i < agenda->topLevelItemCount(); ++i) QVERIFY(agenda->topLevelItem(i)->data(0, Qt::UserRole + 1).toString().isEmpty());
        anchor->setDate(QDate(2026, 11, 30)); QCOMPARE(agenda->topLevelItemCount(), 2); // Classes resume.
        anchor->setDate(QDate(2026, 12, 14)); QCOMPARE(agenda->topLevelItemCount(), 0); // Second period stops classes.
        auto* ranges = window.findChild<QTreeWidget*>("calendarRanges"); ranges->setCurrentItem(ranges->topLevelItem(0));
        const QString rangeId = ranges->currentItem()->data(0, Qt::UserRole).toString(); editingRange = true;
        window.findChild<QPushButton*>("calendarEditRange")->click();
        QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& range : latest().examRanges) if (range.id == rangeId) return range.start == QDate(2026, 11, 10); return false; }(), 10000);
        customPolicy = true; window.findChild<QPushButton*>("calendarSemesterEdit")->click();
        QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& item : latest().semesters) if (item.id == firstSemester) return item.teachingStart.isValid(); return false; }(), 10000);
        anchor->setDate(QDate(2026, 9, 14)); QCOMPARE(agenda->topLevelItemCount(), 2);
        customPolicy = false; window.findChild<QPushButton*>("calendarSemesterEdit")->click();
        QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& item : latest().semesters) if (item.id == firstSemester) return !item.teachingStart.isValid(); return false; }(), 10000);
        QCOMPARE(agenda->topLevelItemCount(), 0);
        window.findChild<QPushButton*>("calendarTimezoneButton")->click(); QTRY_COMPARE_WITH_TIMEOUT(latest().calendarTimeZone, QString("UTC"), 10000);
        semester->setCurrentIndex(semester->findData(secondSemester)); anchor->setDate(QDate(2026, 11, 16)); QCOMPARE(agenda->topLevelItemCount(), 1);
        QCOMPARE(agenda->topLevelItem(0)->text(0), QString("Global event"));
        semester->setCurrentIndex(semester->findData(firstSemester)); anchor->setDate(QDate(2026, 11, 9));
        const QString screenshot = qEnvironmentVariable("VIRGIL_M4_SCREENSHOT"); if (!screenshot.isEmpty()) { QTest::qWait(50); QVERIFY(window.grab().save(screenshot)); }
        view->setCurrentIndex(0); QCOMPARE(grid->rowCount(), 6);
        QVERIFY(grid->cellWidget(0, 0)->findChild<QPushButton*>("calendarDay-2026-10-26"));
        const QString monthScreenshot = qEnvironmentVariable("VIRGIL_M4_MONTH_SCREENSHOT");
        if (!monthScreenshot.isEmpty()) { QTest::qWait(50); QVERIFY(window.grab().save(monthScreenshot)); }
        auto* selectedDay = window.findChild<QPushButton*>("calendarDay-2026-11-18"); selectedDay->click();
        QCOMPARE(view->currentText(), QString("Week")); QCOMPARE(anchor->date(), QDate(2026, 11, 18));
        ranges->setCurrentItem(ranges->topLevelItem(0)); window.findChild<QPushButton*>("calendarDeleteRange")->click();
        QTRY_COMPARE_WITH_TIMEOUT(latest().examRanges.size(), 2, 10000);
        auto* classes = window.findChild<QTreeWidget*>("calendarClasses"); classes->setCurrentItem(classes->topLevelItem(0));
        window.findChild<QPushButton*>("calendarDeleteClass")->click(); QTRY_COMPARE_WITH_TIMEOUT(latest().weeklyClasses.size(), 1, 10000);
        driver.stop(); QCOMPARE(failures.count(), 0);
    }

    void calendarWeekAlignsTimesAndSharesOverlaps()
    {
        QTemporaryDir temporary;
        Snapshot snapshot; snapshot.root = temporary.path(); snapshot.libraryId = "layout-library";
        Semester semester; semester.id = "winter"; semester.name = "Winter"; semester.season = "winter";
        semester.start = QDate(2026, 9, 1); semester.end = QDate(2027, 2, 28); snapshot.semesters.append(semester);
        Course course; course.id = "course"; course.semesterId = semester.id; course.name = "Math"; course.color = "#e6c800"; snapshot.courses.append(course);
        const auto add = [&](const QString& id, int weekday, QTime start, QTime end, const QString& kind = "lecture") {
            WeeklyClass slot; slot.id = id; slot.semesterId = semester.id; slot.courseId = course.id;
            slot.title = "Math · " + eventKindTitle(kind); slot.kind = kind; slot.weekday = weekday;
            slot.start = start; slot.end = end; slot.room = "204"; snapshot.weeklyClasses.append(slot);
        };
        add("early", 1, {8, 0}, {9, 0}); add("overlap-a", 1, {10, 0}, {12, 0}, "auditory");
        add("overlap-b", 1, {11, 0}, {13, 0}, "lab"); add("nested", 1, {11, 15}, {11, 45});
        add("reuse", 1, {12, 0}, {13, 0}); add("touching", 1, {13, 0}, {14, 0});
        add("last", 1, {20, 0}, {21, 0}); add("delayed", 2, {10, 0}, {11, 0});
        add("same-time", 3, {8, 0}, {9, 0}); add("half-hour", 4, {8, 30}, {9, 30});
        add("outside", 5, {7, 0}, {8, 0}); add("clipped", 6, {20, 30}, {21, 30});
        add("sunday", 7, {10, 0}, {11, 0});
        CalendarEvent allDay; allDay.id = "all-day"; allDay.title = "Deadline"; allDay.kind = "assignment";
        allDay.date = QDate(2026, 11, 3); snapshot.events.append(allDay);
        LibraryController controller; CalendarPage page(&controller); page.resize(1350, 900); page.show(); page.setSnapshot(snapshot);
        page.findChild<QDateEdit*>("calendarDate")->setDate(QDate(2026, 11, 2));
        page.findChild<QComboBox*>("calendarView")->setCurrentIndex(1);
        QApplication::processEvents();
        const auto card = [&](const QString& id, int day = 2) {
            return page.findChild<QAbstractButton*>("calendarWeekEntry-" + id + "-2026-11-" + QString::number(day).rightJustified(2, '0'));
        };
        auto* early = card("early"); auto* a = card("overlap-a"); auto* b = card("overlap-b");
        auto* nested = card("nested"); auto* reuse = card("reuse"); auto* touching = card("touching");
        auto* last = card("last"); auto* delayed = card("delayed", 3); auto* sameTime = card("same-time", 4);
        auto* halfHour = card("half-hour", 5); auto* outside = card("outside", 6); auto* clipped = card("clipped", 7);
        QVERIFY(early && a && b && nested && reuse && touching && last && delayed && sameTime && halfHour && outside && clipped);
        QCOMPARE(early->y(), sameTime->y()); QCOMPARE(a->y(), delayed->y());
        const int hour = (delayed->y() - early->y()) / 2;
        QVERIFY(hour > 0); QCOMPARE(early->height(), hour - 2);
        QCOMPARE(a->height(), 2 * hour - 2); QCOMPARE(last->y() - early->y(), 12 * hour);
        QCOMPARE(last->height(), hour - 2); QCOMPARE(halfHour->y() - early->y(), hour / 2);
        QCOMPARE(clipped->height(), hour / 2 - 2);
        QVERIFY(a->geometry().right() < b->x()); QVERIFY(b->geometry().right() < nested->x());
        QCOMPARE(reuse->x(), a->x()); QVERIFY(touching->width() > a->width() * 2);
        QVERIFY(a->text().contains("[AUDITORY]")); QVERIFY(!a->text().contains("Auditory Exercises"));
        QVERIFY(b->text().contains("[LAB]")); QVERIFY(!b->text().contains(" · Lab"));
        QVERIFY(outside->y() < early->y()); QVERIFY(card("all-day", 3)); QVERIFY(!card("sunday", 8));
        QVERIFY(!page.findChild<QPushButton*>("calendarDay-2026-11-08"));
        const auto painted = a->grab().toImage(); QCOMPARE(painted.pixelColor(painted.width() - 3, 2), QColor("#808080"));
        QTest::mouseClick(a, Qt::LeftButton); QVERIFY(page.findChild<QPushButton*>("calendarEditClass")->isEnabled());
        bool edited = false;
        QTimer::singleShot(0, &page, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            edited = dialog->findChild<QLineEdit*>("classRoom1")->text() == "204";
            dialog->reject();
        });
        QTest::mouseDClick(a, Qt::LeftButton); QVERIFY(edited);
        const QString screenshot = qEnvironmentVariable("VIRGIL_WEEK_LAYOUT_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(page.grab().save(screenshot));
    }

    void separateProcessesPersistAndRespectLocks()
    {
        QTemporaryDir temporary;
        const QString root = temporary.filePath("process-library");
        const QString executable = QCoreApplication::applicationDirPath() + "/workflow_probe";
        QProcess process;
        process.start(executable, {"create", root});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        const auto id = process.readAllStandardOutput().trimmed();
        QVERIFY(!id.isEmpty());
        process.start(executable, {"verify", root});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(process.readAllStandardOutput().trimmed(), id);
        auto library = Library::open(root);
        process.start(executable, {"expect-locked", root});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
    }
};

QTEST_MAIN(WidgetTest)
#include "widget_test.moc"
