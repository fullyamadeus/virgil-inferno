#include "ui/mainwindow.h"
#include "ui/calendarpage.h"
#include "services/calendar.h"
#include "services/librarycontroller.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QDateEdit>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QInputDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace virgil {
namespace {
QLabel* textLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText); // Filenames and course names are data, not HTML.
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QIcon colorIcon(const QString& color)
{
    QPixmap swatch(16, 16);
    swatch.fill(QColor(color));
    return QIcon(swatch);
}

void addButtons(QDialog& dialog, QVBoxLayout* layout, const QString& acceptText)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(acceptText);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
}

QWidget* laterPage(const QString& text, QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(textLabel(text, page));
    layout->addStretch();
    return page;
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), controller_(new LibraryController(this))
{
    setWindowTitle("Virgil Desktop");
    setObjectName("virgilMainWindow");
    resize(1100, 740);
    buildUi();
    if (settings_.contains("window/geometry"))
        restoreGeometry(settings_.value("window/geometry").toByteArray());
    connect(controller_, &LibraryController::snapshotReady, this, &MainWindow::applySnapshot);
    connect(controller_, &LibraryController::directoryInspected, this, &MainWindow::reviewDirectory);
    connect(controller_, &LibraryController::failed, this, &MainWindow::showError);
    connect(controller_, &LibraryController::localPathReady, this, [this](const QString& path) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            showError("The desktop could not open this local file/folder. Check its default application: " + path);
    });
    connect(controller_, &LibraryController::busyChanged, this, [this](bool busy) {
        progress_->setVisible(busy);
        cancel_->setVisible(busy);
        if (busy) {
            errorLabel_->hide();
            progress_->setRange(0, 0);
            statusBar()->showMessage("Working…");
        } else {
            statusBar()->clearMessage();
        }
        updateActions();
        if (!busy && closing_)
            QTimer::singleShot(0, this, &QWidget::close);
    });
    connect(controller_, &LibraryController::progress, this, [this](const QString& message, int percent) {
        statusBar()->showMessage(message);
        progress_->setRange(0, percent < 0 ? 0 : 100);
        if (percent >= 0) progress_->setValue(percent);
    });
    updateActions();
}

void MainWindow::buildUi()
{
    auto* libraryMenu = menuBar()->addMenu("&Library");
    createLibraryAction_ = libraryMenu->addAction("&Create library…", this, [this] { chooseLibrary(true); });
    createLibraryAction_->setObjectName("createLibraryAction");
    openLibraryAction_ = libraryMenu->addAction("&Open library…", this, [this] { chooseLibrary(false); });
    openLibraryAction_->setObjectName("openLibraryAction");
    openLibraryAction_->setShortcut(QKeySequence::Open);
    libraryMenu->addAction("Restore from library trash…", this, [this] { reviewRecovery(true); });
    libraryMenu->addAction("Review interrupted operations…", this, [this] { reviewRecovery(false); });
    libraryMenu->addSeparator();
    libraryMenu->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);
    auto* organize = menuBar()->addMenu("&Organize");
    semesterAction_ = organize->addAction("New &semester…", this, &MainWindow::addSemester);
    semesterAction_->setObjectName("semesterAction");
    courseAction_ = organize->addAction("New &course…", this, &MainWindow::addCourse);
    courseAction_->setObjectName("courseAction");
    importAction_ = organize->addAction("&Import file…", this, [this] { importMaterial(); });
    importAction_->setObjectName("importAction");
    importAction_->setShortcut(QKeySequence("Ctrl+I"));
    refreshAction_ = organize->addAction("&Refresh", controller_, &LibraryController::refresh);
    refreshAction_->setShortcut(QKeySequence::Refresh);
    refreshAction_->setObjectName("refreshAction");
    organizeActions_.append(organize->addAction("New folder…", this, &MainWindow::newFolder));
    organizeActions_.append(organize->addAction("Rename selected file / current folder…", this, [this] { organizePath(true); }));
    organizeActions_.append(organize->addAction("Move selected file / current folder…", this, [this] { organizePath(false); }));
    organizeActions_.append(organize->addAction("Move selected file / current folder to trash…", this, &MainWindow::trashSelection));
    organizeActions_.append(organize->addAction("Relink missing file…", this, &MainWindow::relinkSelection));
    deleteCourseAction_ = organize->addAction("Delete course…", this, &MainWindow::deleteCourse);
    deleteCourseAction_->setObjectName("deleteCourseAction");
    editCourseAction_ = organize->addAction("Edit course…", this, [this] { editOrganization(true); });
    editCourseAction_->setObjectName("editCourseAction");
    editSemesterAction_ = organize->addAction("Edit semester…", this, [this] { editOrganization(false); });
    editSemesterAction_->setObjectName("editSemesterAction");
    metadataAction_ = organize->addAction("Edit material metadata…", this, &MainWindow::editMaterialMetadata);
    metadataAction_->setObjectName("editMaterialAction");
    auto* toolbar = addToolBar("Library actions");
    toolbar->setObjectName("libraryToolbar");
    toolbar->setMovable(false);
    toolbar->addAction(createLibraryAction_);
    toolbar->addAction(openLibraryAction_);
    toolbar->addSeparator();
    toolbar->addAction(semesterAction_);
    toolbar->addAction(courseAction_);
    toolbar->addAction(importAction_);
    toolbar->addAction(refreshAction_);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    errorLabel_ = textLabel({}, central);
    errorLabel_->setObjectName("noticeLabel");
    errorLabel_->setFrameStyle(QFrame::StyledPanel);
    errorLabel_->setMargin(10);
    errorLabel_->hide();
    layout->addWidget(errorLabel_);
    auto* splitter = new QSplitter(central);
    navigation_ = new QListWidget(splitter);
    navigation_->setObjectName("navigation");
    navigation_->addItems({"Dashboard", "Courses", "Calendar", "Settings"});
    navigation_->setMinimumWidth(120);
    navigation_->setMaximumWidth(220);
    pages_ = new QStackedWidget(splitter);
    connect(navigation_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);

    auto* dashboardPage = new QWidget(pages_);
    auto* dashboardLayout = new QVBoxLayout(dashboardPage);
    dashboard_ = textLabel("Welcome to Virgil Desktop\n\nYour study files and metadata stay on this computer.\n\n"
                           "Start with Create library, then add a semester and a course. "
                           "You can inspect an existing directory before adopting it.", dashboardPage);
    dashboard_->setObjectName("dashboardSummary");
    dashboardLayout->addWidget(dashboard_);
    auto* browse = new QPushButton("Browse courses", dashboardPage);
    connect(browse, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(1); });
    dashboardLayout->addWidget(browse, 0, Qt::AlignLeft);
    dashboardCourses_ = new QListWidget(dashboardPage);
    dashboardCourses_->setObjectName("dashboardCourses");
    dashboardLayout->addWidget(dashboardCourses_);
    connect(dashboardCourses_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        QTreeWidgetItemIterator it(courses_);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toString() == item->data(Qt::UserRole).toString()) { courses_->setCurrentItem(*it); break; }
            ++it;
        }
        navigation_->setCurrentRow(1);
        resourceTabs_->setCurrentIndex(0);
    });
    pages_->addWidget(dashboardPage);

    auto* coursePage = new QWidget(pages_);
    auto* courseLayout = new QVBoxLayout(coursePage);
    auto* courseSplitter = new QSplitter(coursePage);
    courses_ = new QTreeWidget(courseSplitter);
    courses_->setObjectName("courseTree");
    courses_->setHeaderLabel("Semesters and courses");
    courses_->setMinimumWidth(200);
    courses_->setSortingEnabled(false);
    connect(courses_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::showCourse);
    courses_->setContextMenuPolicy(Qt::ActionsContextMenu);
    deleteCourseAction_->setShortcut(QKeySequence::Delete);
    deleteCourseAction_->setShortcutContext(Qt::WidgetShortcut);
    courses_->addActions({editCourseAction_, editSemesterAction_, deleteCourseAction_});
    auto* details = new QWidget(courseSplitter);
    auto* detailsLayout = new QVBoxLayout(details);
    courseHeading_ = textLabel("Create a semester and a course to begin.", details);
    detailsLayout->addWidget(courseHeading_);
    auto* courseButtons = new QHBoxLayout;
    auto* editCourseButton = new QPushButton("Edit course…", details);
    auto* deleteCourseButton = new QPushButton("Delete course…", details);
    deleteCourseButton->setObjectName("deleteCourseButton");
    connect(editCourseButton, &QPushButton::clicked, editCourseAction_, &QAction::trigger);
    connect(deleteCourseButton, &QPushButton::clicked, deleteCourseAction_, &QAction::trigger);
    connect(editCourseAction_, &QAction::changed, editCourseButton, [this, editCourseButton] { editCourseButton->setEnabled(editCourseAction_->isEnabled()); });
    connect(deleteCourseAction_, &QAction::changed, deleteCourseButton, [this, deleteCourseButton] { deleteCourseButton->setEnabled(deleteCourseAction_->isEnabled()); });
    courseButtons->addWidget(editCourseButton); courseButtons->addWidget(deleteCourseButton); courseButtons->addStretch();
    detailsLayout->addLayout(courseButtons);
    auto* searchRow = new QHBoxLayout;
    search_ = new QLineEdit(details);
    search_->setObjectName("metadataSearch");
    search_->setPlaceholderText("Search filenames, titles, categories, notes and URLs…");
    search_->setClearButtonEnabled(true);
    searchAll_ = new QCheckBox("All courses", details);
    searchAll_->setObjectName("searchAllCourses");
    searchRow->addWidget(search_); searchRow->addWidget(searchAll_);
    detailsLayout->addLayout(searchRow);
    connect(search_, &QLineEdit::textChanged, this, [this] { renderMaterials(); renderResources(); });
    connect(searchAll_, &QCheckBox::toggled, this, [this] { renderMaterials(); renderResources(); });
    auto* tabs = new QTabWidget(details);
    resourceTabs_ = tabs;
    tabs->setObjectName("courseResourceTabs");
    auto* materialsPage = new QWidget(tabs);
    auto* materialsLayout = new QVBoxLayout(materialsPage);
    materialHint_ = textLabel("Files are shown with their folder paths. Use Refresh after changes in your file manager.", materialsPage);
    materialsLayout->addWidget(materialHint_);
    browseFolders_ = new QComboBox(materialsPage);
    browseFolders_->setObjectName("folderNavigation");
    materialsLayout->addWidget(browseFolders_);
    connect(browseFolders_, &QComboBox::currentIndexChanged, this, [this] { renderMaterials(); });
    breadcrumbs_ = new QToolBar(materialsPage);
    breadcrumbs_->setObjectName("folderBreadcrumbs");
    breadcrumbs_->setMovable(false);
    breadcrumbs_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    materialsLayout->addWidget(breadcrumbs_);
    materials_ = new QTableView(materialsPage);
    materials_->setObjectName("materialsTable");
    materialModel_ = new QStandardItemModel(this);
    materials_->setModel(materialModel_);
    materials_->setAcceptDrops(true);
    materials_->viewport()->setAcceptDrops(true);
    materials_->viewport()->installEventFilter(this);
    materials_->setSelectionBehavior(QAbstractItemView::SelectRows);
    materials_->setSelectionMode(QAbstractItemView::SingleSelection);
    materials_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    materials_->setAlternatingRowColors(true);
    materials_->setSortingEnabled(true);
    materials_->setContextMenuPolicy(Qt::ActionsContextMenu);
    materials_->verticalHeader()->hide();
    connect(materials_, &QTableView::doubleClicked, this, [this] {
        const auto rows = materials_->selectionModel()->selectedRows();
        if (!rows.isEmpty() && rows.first().data(Qt::UserRole + 1).toString() == "folder") {
            const int index = browseFolders_->findData(rows.first().data(Qt::UserRole + 2));
            if (index >= 0) browseFolders_->setCurrentIndex(index);
        } else openSelection();
    });
    connect(materials_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);
    openAction_ = new QAction("Open in default application", this);
    openAction_->setObjectName("openMaterialAction");
    openAction_->setShortcut(QKeySequence(Qt::Key_Return));
    openAction_->setShortcutContext(Qt::WidgetShortcut);
    connect(openAction_, &QAction::triggered, this, [this] {
        const auto rows = materials_->selectionModel()->selectedRows();
        if (!rows.isEmpty() && rows.first().data(Qt::UserRole + 1).toString() == "folder") {
            const int index = browseFolders_->findData(rows.first().data(Qt::UserRole + 2));
            if (index >= 0) browseFolders_->setCurrentIndex(index);
        } else openSelection();
    });
    folderAction_ = new QAction("Open containing folder", this);
    connect(folderAction_, &QAction::triggered, this, [this] { openSelection(true); });
    materials_->addActions({openAction_, folderAction_, metadataAction_});
    auto* fileButtons = new QHBoxLayout;
    auto* openButton = new QPushButton("Open file", materialsPage);
    auto* folderButton = new QPushButton("Open containing folder", materialsPage);
    connect(openButton, &QPushButton::clicked, openAction_, &QAction::trigger);
    connect(folderButton, &QPushButton::clicked, folderAction_, &QAction::trigger);
    connect(openAction_, &QAction::changed, openButton, [this, openButton] { openButton->setEnabled(openAction_->isEnabled()); });
    connect(folderAction_, &QAction::changed, folderButton, [this, folderButton] { folderButton->setEnabled(folderAction_->isEnabled()); });
    fileButtons->addWidget(openButton);
    fileButtons->addWidget(folderButton);
    auto* metadataButton = new QPushButton("Edit metadata…", materialsPage);
    metadataButton->setObjectName("editMaterialButton");
    connect(metadataButton, &QPushButton::clicked, metadataAction_, &QAction::trigger);
    connect(metadataAction_, &QAction::changed, metadataButton, [this, metadataButton] { metadataButton->setEnabled(metadataAction_->isEnabled()); });
    fileButtons->addWidget(metadataButton);
    fileButtons->addStretch();
    materialsLayout->addWidget(materials_);
    materialsLayout->addLayout(fileButtons);
    tabs->addTab(materialsPage, "Materials");
    tabs->addTab(buildResourcePage("book", tabs), "Books");
    tabs->addTab(buildResourcePage("lecture", tabs), "Lectures");
    tabs->addTab(buildResourcePage("bookmark", tabs), "Bookmarks");
    tabs->addTab(laterPage("Offline quizzes, flashcards and progress arrive in milestone 6. No AI service is required for this planned workflow.", tabs), "Practice");
    detailsLayout->addWidget(tabs);
    courseSplitter->setStretchFactor(1, 1);
    courseSplitter->setSizes({230, 650});
    courseLayout->addWidget(courseSplitter);
    pages_->addWidget(coursePage);
    calendar_ = new CalendarPage(controller_, pages_);
    connect(calendar_, &CalendarPage::error, this, &MainWindow::showError);
    pages_->addWidget(calendar_);

    auto* settingsPage = new QWidget(pages_);
    auto* settingsLayout = new QVBoxLayout(settingsPage);
    settingsLabel_ = textLabel("No library open. Use Create library or Open library above.", settingsPage);
    settingsLayout->addWidget(settingsLabel_);
    auto* switchLibrary = new QPushButton("Open a different library…", settingsPage);
    connect(switchLibrary, &QPushButton::clicked, openLibraryAction_, &QAction::trigger);
    connect(openLibraryAction_, &QAction::changed, switchLibrary, [this, switchLibrary] { switchLibrary->setEnabled(openLibraryAction_->isEnabled()); });
    settingsLayout->addWidget(switchLibrary, 0, Qt::AlignLeft);
    auto* backup = new QPushButton("Create metadata-only backup now", settingsPage);
    backup->setObjectName("backupMetadataButton");
    connect(backup, &QPushButton::clicked, controller_, &LibraryController::backupMetadata);
    connect(controller_, &LibraryController::busyChanged, backup, [this, backup](bool busy) { backup->setEnabled(!busy && !snapshot_.root.isEmpty()); });
    connect(controller_, &LibraryController::snapshotReady, backup, [backup] { backup->setEnabled(true); });
    backup->setEnabled(false);
    settingsLayout->addWidget(backup, 0, Qt::AlignLeft);
    settingsLayout->addWidget(textLabel("Appearance follows your Qt desktop theme. Preferences and the last library location are stored separately from study data.\n\n"
        "Verified metadata-only backups retain the latest seven snapshots after imports, metadata edits and moves, and once daily on open/refresh. They do not protect material files. Full backup/restore and export/import arrive in milestone 5. "
        "Until then, close Virgil and copy the entire library, including hidden .virgil, to make a manual backup.\n\n"
        "Files are watched automatically. A 30-second reconciliation covers missed events; F5 refreshes immediately. Library trash retains files until restored.", settingsPage));
    settingsLayout->addStretch();
    pages_->addWidget(settingsPage);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({150, 900});
    layout->addWidget(splitter);
    setCentralWidget(central);
    navigation_->setCurrentRow(0);
    progress_ = new QProgressBar(this);
    progress_->setMaximumWidth(180);
    progress_->hide();
    cancel_ = new QPushButton("Cancel operation", this);
    cancel_->hide();
    connect(cancel_, &QPushButton::clicked, controller_, &LibraryController::cancel);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(cancel_);
}

void MainWindow::openRememberedLibrary()
{
    const QString path = settings_.value("library/lastPath").toString();
    if (!path.isEmpty())
        controller_->openLibrary(path);
}

void MainWindow::chooseLibrary(bool create)
{
    const QString directory = QFileDialog::getExistingDirectory(this, create ? "Choose or create a library directory" : "Open Virgil library",
        snapshot_.root.isEmpty() ? QDir::homePath() : snapshot_.root, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty()) return;
    if (create) controller_->inspectDirectory(directory);
    else controller_->openLibrary(directory);
}

void MainWindow::reviewDirectory(const DirectoryPreview& preview)
{
    if (closing_) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Inspect library directory");
    dialog.setObjectName("adoptDialog");
    dialog.resize(650, 450);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(textLabel(preview.root + "\n\nVirgil will add a hidden .virgil directory here. Existing files stay in place. "
                                "They will appear as unassigned materials until course organization is added.", &dialog));
    auto* entries = new QListWidget(&dialog);
    entries->addItems(preview.entries);
    if (preview.entries.isEmpty()) entries->addItem("(Empty directory)");
    layout->addWidget(entries);
    if (preview.truncated)
        layout->addWidget(textLabel("First 200 entries shown. Inspect the full directory in your file manager before adopting it.", &dialog));
    auto* inspect = new QPushButton("Inspect in file manager", &dialog);
    connect(inspect, &QPushButton::clicked, &dialog, [this, preview] {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(preview.root)))
            showError("Could not open the file manager for " + preview.root);
    });
    layout->addWidget(inspect, 0, Qt::AlignLeft);
    addButtons(dialog, layout, preview.entries.isEmpty() ? "Create library here" : "Adopt directory without moving files");
    if (dialog.exec() == QDialog::Accepted)
        controller_->createLibrary(preview.root, true);
}

void MainWindow::addSemester()
{
    QDialog dialog(this);
    dialog.setObjectName("semesterDialog");
    dialog.setWindowTitle("New semester");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(&dialog);
    name->setObjectName("semesterName");
    name->setPlaceholderText("Winter 2026");
    auto* season = new QComboBox(&dialog);
    season->addItem("Winter", "winter");
    season->addItem("Summer", "summer");
    const int year = QDate::currentDate().year();
    auto* start = new QDateEdit(QDate(year, 10, 1), &dialog);
    auto* end = new QDateEdit(QDate(year + 1, 2, 17), &dialog);
    for (auto* date : {start, end}) { date->setCalendarPopup(true); date->setDisplayFormat("yyyy-MM-dd"); }
    form->addRow("&Name / folder:", name);
    form->addRow("&Teaching season:", season);
    form->addRow("&Start date:", start);
    form->addRow("&End date:", end);
    layout->addLayout(form);
    layout->addWidget(textLabel("Creates a new folder with this name. Teaching season is explicit; dates are yours to choose.", &dialog));
    addButtons(dialog, layout, "Create semester");
    if (dialog.exec() == QDialog::Accepted)
        controller_->createSemester(name->text(), season->currentData().toString(), start->date(), end->date());
}

void MainWindow::addCourse()
{
    QDialog dialog(this);
    dialog.setObjectName("courseDialog");
    dialog.setWindowTitle("New course");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* semester = new QComboBox(&dialog);
    for (const auto& item : snapshot_.semesters) semester->addItem(item.name, item.id);
    const Course* current = selectedCourse();
    const QString selectedSemester = current ? current->semesterId : courses_->currentItem() ? courses_->currentItem()->data(0, Qt::UserRole + 1).toString() : QString();
    const int selectedIndex = semester->findData(selectedSemester);
    if (selectedIndex >= 0) semester->setCurrentIndex(selectedIndex);
    auto* name = new QLineEdit(&dialog);
    name->setObjectName("courseName");
    auto* code = new QLineEdit(&dialog);
    auto* color = new QComboBox(&dialog);
    for (qsizetype i = 0; i < courseColors.size(); ++i)
        color->addItem(colorIcon(courseColors[i]), courseColorNames[i], courseColors[i]);
    form->addRow("&Semester:", semester);
    form->addRow("&Name / folder:", name);
    form->addRow("Course co&de (optional):", code);
    form->addRow("&Color:", color);
    layout->addLayout(form);
    layout->addWidget(textLabel("Creates Lectures, Books, Past Exams and Other Materials folders. "
                               "Manual entry works offline; the catalog can fill a verified code and name.", &dialog));
    auto* catalog = new QPushButton("Choose from offline FINKI SEIS23 catalog…", &dialog);
    catalog->setObjectName("chooseCatalogButton");
    connect(catalog, &QPushButton::clicked, &dialog, [this, name, code] { chooseCatalog(name, code); });
    layout->addWidget(catalog);
    addButtons(dialog, layout, "Create course");
    if (dialog.exec() == QDialog::Accepted)
        controller_->createCourse(semester->currentData().toString(), name->text(), code->text(), color->currentData().toString());
}

void MainWindow::importMaterial(const QString& dropped)
{
    const Course* course = selectedCourse();
    if (snapshot_.root.isEmpty()) return;
    const QString source = dropped.isEmpty() ? QFileDialog::getOpenFileName(this, "Copy a file into the library", {}, "All files (*)") : dropped;
    if (source.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName("importDialog");
    dialog.setWindowTitle("Import a copy");
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(textLabel("Source: " + source + "\nThe original will be kept. If already inside this library, its location is kept without duplication.", &dialog));
    auto* form = new QFormLayout;
    auto* folder = new QComboBox(&dialog);
    folder->addItem("Library root", QString(""));
    for (const auto& f : snapshot_.folders) folder->addItem(f.path, f.path);
    QString destination = currentFolder();
    if (browseFolders_->currentData().toString() == "*" && course) destination = course->path + "/Other Materials";
    const int index = folder->findData(destination);
    if (index >= 0) folder->setCurrentIndex(index);
    auto* filename = new QLineEdit(QFileInfo(source).fileName(), &dialog);
    form->addRow("&Destination:", folder);
    form->addRow("&Filename:", filename);
    layout->addLayout(form);
    addButtons(dialog, layout, "Import copy");
    if (dialog.exec() == QDialog::Accepted)
        controller_->importFile(source, folder->currentData().toString(), filename->text());
}

void MainWindow::applySnapshot(const Snapshot& snapshot, const QString& message)
{
    const bool differentLibrary = snapshot_.libraryId != snapshot.libraryId;
    QString oldSelection = courses_->currentItem() ? courses_->currentItem()->data(0, Qt::UserRole).toString() : QString();
    if (differentLibrary) { oldSelection.clear(); browsingCourse_.clear(); materials_->clearSelection(); search_->clear(); searchAll_->setChecked(false); }
    if (!differentLibrary && snapshot.courses.size() > snapshot_.courses.size()) {
        for (const auto& candidate : snapshot.courses) {
            bool alreadyKnown = false;
            for (const auto& previous : snapshot_.courses)
                if (previous.id == candidate.id) alreadyKnown = true;
            if (!alreadyKnown) { oldSelection = candidate.id; break; }
        }
    }
    snapshot_ = snapshot;
    calendar_->setSnapshot(snapshot);
    settings_.setValue("library/lastPath", snapshot.root);
    settings_.sync();
    setWindowTitle(QFileInfo(snapshot.root).fileName() + " — Virgil Desktop");
    const QSignalBlocker blocker(courses_);
    courses_->clear();
    QTreeWidgetItem* selection = nullptr;
    for (const auto& semester : snapshot.semesters) {
        auto* parent = new QTreeWidgetItem(courses_, {semester.name});
        parent->setData(0, Qt::UserRole + 1, semester.id);
        parent->setToolTip(0, semester.season + " · " + semester.start.toString(Qt::ISODate) + " – " + semester.end.toString(Qt::ISODate));
        for (const auto& course : snapshot.courses) {
            if (course.semesterId != semester.id) continue;
            auto* item = new QTreeWidgetItem(parent, {course.name});
            item->setIcon(0, colorIcon(course.color));
            item->setData(0, Qt::UserRole, course.id);
            item->setToolTip(0, course.code + "\n" + course.path);
            if (!selection || course.id == oldSelection) selection = item;
        }
        parent->setExpanded(true);
    }
    auto* unassigned = new QTreeWidgetItem(courses_, {"Unassigned files"});
    unassigned->setData(0, Qt::UserRole, "unassigned");
    if (!selection || oldSelection == "unassigned") selection = unassigned;
    courses_->setCurrentItem(selection);
    showCourse();
    dashboardCourses_->clear();
    for (const auto& course : snapshot.courses) {
        auto* item = new QListWidgetItem(colorIcon(course.color), course.name + " · " + course.code, dashboardCourses_);
        item->setData(Qt::UserRole, course.id);
    }
    qsizetype available = 0;
    for (const auto& item : snapshot.materials) if (item.available) ++available;
    QStringList currentCourses;
    const QDate today = QDate::currentDate();
    for (const auto& semester : snapshot.semesters) {
        if (today < semester.start || today > semester.end) continue;
        for (const auto& course : snapshot.courses)
            if (course.semesterId == semester.id) currentCourses.append(course.name + " (" + semester.name + ")");
    }
    dashboard_->setText(QString("Your local study workspace\n\n%1 semesters · %2 courses · %3 available files · %4 missing records\n\n"
                               "Current courses\n%5\n\nCreate a semester, add a course, then import a file. "
                               "Open it with your usual desktop application.\n\n"
                               "Open Calendar for upcoming events and weekly classes. Saved practice arrives in milestone 6.")
        .arg(snapshot.semesters.size()).arg(snapshot.courses.size()).arg(available).arg(snapshot.materials.size() - available)
        .arg(currentCourses.isEmpty() ? "No courses in a semester covering today. Browse Courses for all semesters." : currentCourses.join('\n')));
    settingsLabel_->setText("Library: " + snapshot.root + "\nLibrary ID: " + snapshot.libraryId + "\nMetadata schema: 6\nQt runtime: " + qVersion()
        + "\nCalendar timezone: " + snapshot.calendarTimeZone + "\nPreferences: " + settings_.fileName()
        + QString("\nMetadata snapshots retained: %1 (materials are not backed up)").arg(snapshot.backups.size())
        + "\n\nSQLite is a local metadata file. No server, account, subscription, quota or database administration is needed.");
    QStringList notices = snapshot.notices;
    if (settings_.status() != QSettings::NoError)
        notices.append("Your study data was saved, but the library preference could not be saved. Check settings-directory permissions.");
    errorLabel_->setText(notices.join("\n\n"));
    errorLabel_->setVisible(!notices.isEmpty());
    statusBar()->showMessage(message, 12000);
    if (differentLibrary) navigation_->setCurrentRow(1);
    updateActions();
}

const Course* MainWindow::selectedCourse() const
{
    if (!courses_->currentItem()) return nullptr;
    const QString id = courses_->currentItem()->data(0, Qt::UserRole).toString();
    for (const auto& course : snapshot_.courses)
        if (course.id == id) return &course;
    return nullptr;
}

void MainWindow::showCourse()
{
    const Course* course = selectedCourse();
    const bool unassigned = courses_->currentItem() && courses_->currentItem()->data(0, Qt::UserRole).toString() == "unassigned";
    courseHeading_->setText(course ? course->name + (course->code.isEmpty() ? "" : " · " + course->code) + "\n" + course->path
                                  : unassigned ? "Unassigned files — kept in their original folders" : "Select a course, or create one in this semester.");
    const QString courseId = course ? course->id : QString("unassigned");
    const QString previous = browsingCourse_ == courseId ? browseFolders_->currentData().toString() : QString("*");
    browsingCourse_ = courseId;
    {
        const QSignalBlocker blocker(browseFolders_);
        browseFolders_->clear();
        browseFolders_->addItem("All files (including nested folders)", "*");
        if (!course) browseFolders_->addItem("Library root", QString(""));
        else if (!course->path.startsWith(".virgil/")) browseFolders_->addItem(course->path, course->path);
        for (const auto& folder : snapshot_.folders)
            if (course ? folder.path.startsWith(course->path + '/') : true) browseFolders_->addItem(folder.path, folder.path);
        int index = browseFolders_->findData(previous);
        browseFolders_->setCurrentIndex(index < 0 ? 0 : index);
    }
    renderMaterials();
    renderResources();
}

void MainWindow::renderMaterials()
{
    const auto oldRows = materials_->selectionModel()->selectedRows();
    const QString oldId = oldRows.isEmpty() ? QString() : oldRows.first().data(Qt::UserRole).toString();
    const QString oldFolder = !oldRows.isEmpty() && oldRows.first().data(Qt::UserRole + 1).toString() == "folder"
        ? oldRows.first().data(Qt::UserRole + 2).toString() : QString();
    const Course* course = selectedCourse();
    const bool unassigned = courses_->currentItem() && courses_->currentItem()->data(0, Qt::UserRole).toString() == "unassigned";
    const QString folder = currentFolder();
    const bool all = browseFolders_->currentData().toString() == "*";
    breadcrumbs_->clear();
    if (!all) {
        auto add = [this](const QString& title, const QString& path) {
            breadcrumbs_->addAction(title, this, [this, path] {
                const int index = browseFolders_->findData(path);
                if (index >= 0) browseFolders_->setCurrentIndex(index);
            });
        };
        if (!course) add("Library", "");
        QString path;
        for (const auto& part : folder.split('/', Qt::SkipEmptyParts)) {
            path = path.isEmpty() ? part : path + '/' + part;
            if (browseFolders_->findData(path) >= 0) add(part, path);
        }
    }
    materials_->setSortingEnabled(false);
    materialModel_->clear();
    materialModel_->setHorizontalHeaderLabels({"Filename", "Status", "Bytes", "Folder", "Modified", "Display title", "Category", "Exam date"});
    if (!all && !searchAll_->isChecked() && search_->text().isEmpty()) {
        for (const auto& f : snapshot_.folders) {
            const QString parent = QFileInfo(f.path).path() == "." ? QString("") : QFileInfo(f.path).path();
            if (parent != folder) continue;
            auto* name = new QStandardItem(QFileInfo(f.path).fileName() + '/');
            name->setData("folder", Qt::UserRole + 1);
            name->setData(f.path, Qt::UserRole + 2);
            materialModel_->appendRow({name, new QStandardItem("Folder"), new QStandardItem, new QStandardItem(parent), new QStandardItem, new QStandardItem});
        }
    }
    for (const auto& material : snapshot_.materials) {
        if (material.path.startsWith(".virgil/")) continue;
        const QString parent = QFileInfo(material.path).path() == "." ? QString("") : QFileInfo(material.path).path();
        if (!searchAll_->isChecked() && !all && parent != folder) continue;
        if (!searchAll_->isChecked() && (course ? material.courseId != course->id : !unassigned || !material.courseId.isEmpty())) continue;
        QString courseText;
        for (const auto& c : snapshot_.courses) if (c.id == material.courseId) courseText = c.name + ' ' + c.code;
        if (!matchesSearch(courseText + ' ' + material.path + ' ' + material.title + ' ' + material.category + ' ' + material.examDate.toString(Qt::ISODate))) continue;
        auto* name = new QStandardItem(QFileInfo(material.path).fileName());
        name->setData(material.id, Qt::UserRole);
        name->setData(material.path, Qt::UserRole + 2);
        name->setToolTip(material.path);
        for (const auto& c : snapshot_.courses) if (c.id == material.courseId) name->setIcon(colorIcon(c.color));
        auto* bytes = new QStandardItem;
        bytes->setData(material.size, Qt::DisplayRole);
        materialModel_->appendRow({name,
            new QStandardItem(material.available ? "Available" : "Missing · metadata kept"), bytes,
            new QStandardItem(QFileInfo(material.path).path()),
            new QStandardItem(material.modified.toLocalTime().toString("yyyy-MM-dd HH:mm")), new QStandardItem(material.title), new QStandardItem(categoryTitle(material.category)), new QStandardItem(material.examDate.toString(Qt::ISODate))});
    }
    materials_->setSortingEnabled(true);
    materials_->sortByColumn(0, Qt::AscendingOrder);
    for (int row = 0; row < materialModel_->rowCount(); ++row) {
        const auto index = materialModel_->index(row, 0);
        if ((!oldId.isEmpty() && index.data(Qt::UserRole).toString() == oldId)
            || (!oldFolder.isEmpty() && index.data(Qt::UserRole + 2).toString() == oldFolder)) {
            materials_->selectRow(row); break;
        }
    }
    materials_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    materials_->setColumnWidth(0, 175);
    materials_->setColumnWidth(1, 175);
    materials_->setColumnWidth(2, 90);
    materials_->setColumnWidth(3, 210);
    materials_->setColumnWidth(4, 140);
    materials_->horizontalHeader()->setStretchLastSection(true);
    materialHint_->setText(materialModel_->rowCount() == 0
        ? "No files here yet. Import a file into a course, or add files in your file manager and Refresh."
        : "Double-click folders to navigate and files to open. Drop local files to import. F5 refreshes immediately; missing records can be relinked.");
    updateActions();
}

QString MainWindow::selectedMaterialId() const
{
    const auto selected = materials_->selectionModel()->selectedRows();
    return selected.isEmpty() ? QString() : selected.first().data(Qt::UserRole).toString();
}

void MainWindow::updateActions()
{
    const bool idle = !controller_->busy() && !closing_;
    const bool open = !snapshot_.root.isEmpty();
    createLibraryAction_->setEnabled(idle);
    openLibraryAction_->setEnabled(idle);
    semesterAction_->setEnabled(idle && open);
    courseAction_->setEnabled(idle && open && !snapshot_.semesters.isEmpty());
    importAction_->setEnabled(idle && open);
    browseFolders_->setEnabled(idle && open);
    for (auto* action : organizeActions_) action->setEnabled(idle && open);
    deleteCourseAction_->setEnabled(idle && selectedCourse());
    editCourseAction_->setEnabled(idle && selectedCourse());
    editSemesterAction_->setEnabled(idle && !selectedSemesterId().isEmpty());
    metadataAction_->setEnabled(idle && !selectedMaterialId().isEmpty());
    for (const auto& view : resourceViews_) {
        const bool selectedResource = !view.table->selectionModel()->selectedRows().isEmpty();
        for (qsizetype i = 0; i < view.actions.size(); ++i) view.actions[i]->setEnabled(idle && open && (i == 0 || selectedResource));
    }
    refreshAction_->setEnabled(idle && open);
    const bool selected = idle && !selectedMaterialId().isEmpty();
    openAction_->setEnabled(selected);
    folderAction_->setEnabled(selected);
}

void MainWindow::openSelection(bool folder)
{
    const QString id = selectedMaterialId();
    if (!id.isEmpty()) controller_->resolveMaterial(id, folder);
}

void MainWindow::showError(const QString& message)
{
    errorLabel_->setText(message);
    errorLabel_->show();
    statusBar()->showMessage("Operation did not complete. Details are shown above.");
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (controller_->busy()) {
        closing_ = true;
        controller_->cancel();
        statusBar()->showMessage("Cancelling the current operation before closing…");
        updateActions();
        event->ignore();
        return;
    }
    settings_.setValue("window/geometry", saveGeometry());
    settings_.sync();
    QMainWindow::closeEvent(event);
}
QString MainWindow::currentFolder() const
{
    const QString folder = browseFolders_->currentData().toString();
    if (folder != "*") return folder;
    const auto* course = selectedCourse();
    return course ? course->path : QString("");
}

QString MainWindow::selectedPath() const
{
    const auto selected = materials_->selectionModel()->selectedRows();
    return selected.isEmpty() ? currentFolder() : selected.first().data(Qt::UserRole + 2).toString();
}

void MainWindow::newFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New folder", "Folder name:", QLineEdit::Normal, {}, &ok);
    if (ok) controller_->createFolder(currentFolder(), name);
}

void MainWindow::organizePath(bool rename)
{
    const QString source = selectedPath();
    if (source.isEmpty()) { showError("Select a file or folder first. The library root cannot be moved."); return; }
    if (rename) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Rename", "New name (case-only changes need an intermediate distinct name):", QLineEdit::Normal, QFileInfo(source).fileName(), &ok);
        const QString parent = QFileInfo(source).path();
        if (ok) controller_->movePath(source, parent == "." ? name : parent + '/' + name);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Move file or folder");
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(textLabel("Move: " + source + "\nFiles and metadata will move together. Existing names will not be replaced.", &dialog));
    auto* folder = new QComboBox(&dialog);
    folder->addItem("Library root", QString(""));
    for (const auto& f : snapshot_.folders) folder->addItem(f.path, f.path);
    layout->addWidget(folder);
    addButtons(dialog, layout, "Move");
    if (dialog.exec() == QDialog::Accepted) {
        const QString parent = folder->currentData().toString();
        controller_->movePath(source, parent.isEmpty() ? QFileInfo(source).fileName() : parent + '/' + QFileInfo(source).fileName());
    }
}

void MainWindow::trashSelection()
{
    const QString source = selectedPath();
    if (source.isEmpty()) { showError("Select a file or folder. The library root cannot be trashed."); return; }
    if (QMessageBox::question(this, "Move to library trash", "Move " + source + " and everything inside it to recoverable library trash? Restore through the Library menu. Course organization is retained.") == QMessageBox::Yes)
        controller_->trashPath(source);
}

void MainWindow::relinkSelection()
{
    const QString id = selectedMaterialId();
    bool missing = false;
    for (const auto& m : snapshot_.materials) if (m.id == id) missing = !m.available;
    if (!missing) { showError("Select a missing file record first."); return; }
    QDialog dialog(this);
    dialog.setWindowTitle("Relink missing record");
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(textLabel("Choose the replacement explicitly. The missing record keeps its UUID and title; the selected discovered record is merged into it. File bytes are unchanged.", &dialog));
    auto* replacement = new QComboBox(&dialog);
    for (const auto& m : snapshot_.materials) if (m.available) replacement->addItem(m.path, m.id);
    layout->addWidget(replacement);
    addButtons(dialog, layout, "Relink selected replacement");
    if (dialog.exec() == QDialog::Accepted && replacement->count()) controller_->relink(id, replacement->currentData().toString());
}

void MainWindow::reviewRecovery(bool trash)
{
    if (controller_->busy() || snapshot_.root.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(trash ? "Recoverable library trash" : "Interrupted operations");
    dialog.resize(700, 360);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(textLabel(trash ? "Restore to the original path. Existing files will never be replaced. There is no permanent erase command." : "Review retained operations. Retry only resumes a provable move. Ambiguous outcomes keep their files and journal. Import staging copies require manual inspection; never assume a partial copy is complete.", &dialog));
    auto* entries = new QComboBox(&dialog);
    for (const auto& item : trash ? snapshot_.trash : snapshot_.operations)
        entries->addItem(item.state + ": " + item.source + " → " + item.destination, item.id);
    layout->addWidget(entries);
    auto* inspect = new QPushButton("Inspect recovery files in file manager", &dialog);
    connect(inspect, &QPushButton::clicked, &dialog, [this, trash] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(snapshot_.root).filePath(trash ? ".virgil/trash" : ".virgil/staging")));
    });
    layout->addWidget(inspect);
    if (!trash) {
        auto* abandon = new QPushButton("Abandon a proven unstarted move", &dialog);
        connect(abandon, &QPushButton::clicked, &dialog, [this, entries, &dialog] {
            if (entries->count()) { controller_->reviewOperation(entries->currentData().toString(), false); dialog.reject(); }
        });
        layout->addWidget(abandon);
    }
    addButtons(dialog, layout, trash ? "Restore selected entry" : "Review / retry selected operation");
    if (dialog.exec() == QDialog::Accepted && entries->count()) {
        if (trash) controller_->restoreTrash(entries->currentData().toString());
        else controller_->reviewOperation(entries->currentData().toString());
    }
}

bool MainWindow::eventFilter(QObject* object, QEvent* event)
{
    if (object == materials_->viewport() && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto* drag = static_cast<QDragMoveEvent*>(event);
        if (!controller_->busy() && !snapshot_.root.isEmpty() && drag->mimeData()->hasUrls()) {
            bool local = true;
            for (const auto& url : drag->mimeData()->urls()) if (!url.isLocalFile()) local = false;
            if (local) { drag->acceptProposedAction(); return true; }
        }
    }
    if (object == materials_->viewport() && event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        if (!controller_->busy() && !snapshot_.root.isEmpty() && drop->mimeData()->hasUrls()) {
            QStringList sources;
            for (const auto& url : drop->mimeData()->urls()) if (url.isLocalFile()) sources.append(url.toLocalFile());
            if (!sources.isEmpty()) {
                controller_->importFiles(sources, currentFolder());
                drop->acceptProposedAction();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(object, event);
}

} // namespace virgil
