#include "ui/mainwindow.h"
#include "services/librarycontroller.h"
#include "services/catalog.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace virgil {
namespace {
void buttons(QDialog& dialog, QVBoxLayout* layout, const QString& label)
{
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    box->button(QDialogButtonBox::Ok)->setText(label);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(box);
}
QLabel* explanation(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    return label;
}
QComboBox* categoryBox(QWidget* parent, const QString& current)
{
    auto* box = new QComboBox(parent);
    box->setObjectName("category");
    for (const auto& category : materialCategories) box->addItem(categoryTitle(category), category);
    box->setCurrentIndex(qMax(0, box->findData(current)));
    return box;
}
QDateEdit* dateEdit(QWidget* parent, QDate date)
{
    auto* editor = new QDateEdit(date.isValid() ? date : QDate::currentDate(), parent);
    editor->setCalendarPopup(true); editor->setDisplayFormat("yyyy-MM-dd");
    return editor;
}
QComboBox* courseBox(QWidget* parent, const Snapshot& snapshot, const QString& selected)
{
    auto* box = new QComboBox(parent);
    box->setObjectName("resourceCourse");
    box->addItem("Unassigned", QString(""));
    for (const auto& course : snapshot.courses) box->addItem(course.name + " · " + course.code, course.id);
    box->setCurrentIndex(qMax(0, box->findData(selected)));
    return box;
}
}

QString MainWindow::selectedSemesterId() const
{
    if (const auto* course = selectedCourse()) return course->semesterId;
    const auto* item = courses_->currentItem();
    return item ? item->data(0, Qt::UserRole + 1).toString() : QString();
}

void MainWindow::deleteCourse()
{
    if (!selectedCourse()) return;
    const Course course = *selectedCourse();
    QDialog dialog(this);
    dialog.setObjectName("deleteCourseDialog"); dialog.setWindowTitle("Delete course");
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(explanation("Delete “" + course.name + "” from Courses?\n\nIts remaining files and saved books, lectures and bookmarks will be kept and become unassigned. This works even if the course folder was removed in your file manager. To remove files, use the separate recoverable trash command.", &dialog));
    buttons(dialog, layout, "Delete course, keep files and resources");
    if (dialog.exec() == QDialog::Accepted) controller_->removeCourse(course.id);
}

void MainWindow::editOrganization(bool editingCourse)
{
    Course course;
    Semester semester;
    if (editingCourse) {
        if (!selectedCourse()) return;
        course = *selectedCourse();
    } else {
        const QString id = selectedSemesterId();
        for (const auto& item : snapshot_.semesters) if (item.id == id) semester = item;
        if (semester.id.isEmpty()) return;
    }
    QDialog dialog(this);
    dialog.setObjectName(editingCourse ? "editCourseDialog" : "editSemesterDialog");
    dialog.setWindowTitle(editingCourse ? "Edit course" : "Edit semester");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(editingCourse ? course.name : semester.name, &dialog);
    name->setObjectName("organizationName");
    form->addRow("Name:", name);
    QComboBox* parent = nullptr;
    QComboBox* color = nullptr;
    QLineEdit* code = nullptr;
    QDateEdit* start = nullptr;
    QDateEdit* end = nullptr;
    QComboBox* season = nullptr;
    if (editingCourse) {
        parent = new QComboBox(&dialog); parent->setObjectName("courseSemester");
        for (const auto& item : snapshot_.semesters) parent->addItem(item.name, item.id);
        parent->setCurrentIndex(parent->findData(course.semesterId));
        code = new QLineEdit(course.code, &dialog); code->setObjectName("organizationCode");
        color = new QComboBox(&dialog); color->setObjectName("courseColor");
        for (const auto& value : courseColors) { QPixmap swatch(16, 16); swatch.fill(QColor(value)); color->addItem(QIcon(swatch), courseColorName(value), value); }
        color->setCurrentIndex(color->findData(course.color));
        form->addRow("Semester:", parent); form->addRow("Code:", code); form->addRow("Color:", color);
    } else {
        season = new QComboBox(&dialog); season->addItem("Winter", "winter"); season->addItem("Summer", "summer");
        season->setCurrentIndex(season->findData(semester.season));
        start = dateEdit(&dialog, semester.start); end = dateEdit(&dialog, semester.end);
        form->addRow("Season:", season); form->addRow("Start:", start); form->addRow("End:", end);
    }
    layout->addLayout(form);
    layout->addWidget(explanation("Editing names and semester organization keeps existing files in their current folders. Use Organize → Rename / Move for physical folder changes.", &dialog));
    buttons(dialog, layout, "Save changes");
    if (dialog.exec() != QDialog::Accepted) return;
    if (editingCourse) {
        course.name = name->text(); course.code = code->text(); course.color = color->currentData().toString(); course.semesterId = parent->currentData().toString();
        controller_->editCourse(course);
    } else {
        semester.name = name->text(); semester.season = season->currentData().toString(); semester.start = start->date(); semester.end = end->date();
        controller_->editSemester(semester);
    }
}

void MainWindow::editMaterialMetadata()
{
    const QString id = selectedMaterialId();
    Material material;
    for (const auto& item : snapshot_.materials) if (item.id == id) material = item;
    if (material.id.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName("materialMetadataDialog"); dialog.setWindowTitle("Material metadata");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* title = new QLineEdit(material.title, &dialog); title->setObjectName("materialTitle");
    auto* category = categoryBox(&dialog, material.category);
    auto* hasDate = new QCheckBox("Exam date known", &dialog); hasDate->setObjectName("hasExamDate"); hasDate->setChecked(material.examDate.isValid());
    auto* exam = dateEdit(&dialog, material.examDate); exam->setObjectName("examDate"); exam->setEnabled(hasDate->isChecked());
    connect(hasDate, &QCheckBox::toggled, exam, &QWidget::setEnabled);
    form->addRow("Display title:", title); form->addRow("Category:", category); form->addRow(hasDate, exam);
    layout->addLayout(form);
    layout->addWidget(explanation("The display title is independent of the filename. Missing files keep their saved metadata.", &dialog));
    buttons(dialog, layout, "Save metadata");
    if (dialog.exec() == QDialog::Accepted) controller_->editMaterial(id, title->text(), category->currentData().toString(), hasDate->isChecked() ? exam->date() : QDate{});
}

QWidget* MainWindow::buildResourcePage(const QString& kind, QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(explanation(kind == "book" ? "Books have a title and a local file. Browse your computer to attach a file; originals are kept."
        : kind == "lecture" ? "Keep a lecture title, date and notes, with an optional local attachment. Deleting a record keeps its file."
        : "Bookmarks belong to a course or folder. Saving a URL does not download it; opening a remote page needs internet.", page));
    ResourceView view;
    view.table = new QTableView(page); view.table->setObjectName(kind + "Table");
    view.model = new QStandardItemModel(this); view.table->setModel(view.model);
    view.table->setSelectionBehavior(QAbstractItemView::SelectRows); view.table->setSelectionMode(QAbstractItemView::SingleSelection);
    view.table->setEditTriggers(QAbstractItemView::NoEditTriggers); view.table->setAlternatingRowColors(true);
    view.table->verticalHeader()->hide(); view.table->setSortingEnabled(true);
    connect(view.table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);
    connect(view.table, &QTableView::doubleClicked, this, [this, kind] { editResource(kind, true); });
    layout->addWidget(view.table);
    auto* row = new QHBoxLayout;
    const QStringList labels{"New…", "Edit…", "Delete record…", kind == "bookmark" ? "Open URL" : "Open attachment"};
    for (int i = 0; i < labels.size(); ++i) {
        auto* action = new QAction(labels[i], this); action->setObjectName(kind + QString("Action%1").arg(i));
        if (i == 0) connect(action, &QAction::triggered, this, [this, kind] { editResource(kind, false); });
        else if (i == 1) connect(action, &QAction::triggered, this, [this, kind] { editResource(kind, true); });
        else if (i == 2) connect(action, &QAction::triggered, this, [this, kind] { deleteResource(kind); });
        else connect(action, &QAction::triggered, this, [this, kind] { openResource(kind); });
        auto* button = new QPushButton(labels[i], page); button->setObjectName(kind + QString("Button%1").arg(i));
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, button, [button, action] { button->setEnabled(action->isEnabled()); });
        row->addWidget(button); view.actions.append(action);
    }
    row->addStretch(); layout->addLayout(row);
    resourceViews_.insert(kind, view);
    return page;
}

QString MainWindow::selectedResourceId(const QString& kind) const
{
    const auto rows = resourceViews_.value(kind).table->selectionModel()->selectedRows();
    return rows.isEmpty() ? QString() : rows.first().data(Qt::UserRole).toString();
}

bool MainWindow::matchesSearch(const QString& value) const
{
    for (const auto& term : search_->text().trimmed().split(' ', Qt::SkipEmptyParts))
        if (!value.contains(term, Qt::CaseInsensitive)) return false;
    return true;
}

void MainWindow::renderResources()
{
    const auto* course = selectedCourse();
    const QString courseId = course ? course->id : QString();
    for (auto it = resourceViews_.begin(); it != resourceViews_.end(); ++it) {
        auto& view = it.value();
        const QString selected = selectedResourceId(it.key());
        view.table->setSortingEnabled(false); view.model->clear();
        view.model->setHorizontalHeaderLabels(it.key() == "book" ? QStringList{"Title", "Local file"}
            : it.key() == "lecture" ? QStringList{"Title", "Lecture date", "Attachment", "Notes"}
            : QStringList{"Title", "URL", "Folder", "Category", "Exam date"});
        for (const auto& resource : snapshot_.resources) {
            if (resource.kind != it.key() || (!searchAll_->isChecked() && resource.courseId != courseId)) continue;
            QString attachment = resource.url;
            for (const auto& m : snapshot_.materials) if (m.id == resource.materialId) attachment = m.path + (m.available ? "" : " [missing]");
            QString courseText;
            for (const auto& c : snapshot_.courses) if (c.id == resource.courseId) courseText = c.name + ' ' + c.code;
            if (!matchesSearch(courseText + ' ' + resource.kind + ' ' + resource.title + ' ' + resource.notes + ' ' + attachment + ' ' + resource.folder + ' ' + resource.category + ' ' + resource.date.toString(Qt::ISODate) + ' ' + resource.examDate.toString(Qt::ISODate))) continue;
            auto* title = new QStandardItem(resource.title); title->setData(resource.id, Qt::UserRole);
            for (const auto& c : snapshot_.courses) if (c.id == resource.courseId) { QPixmap swatch(16, 16); swatch.fill(QColor(c.color)); title->setIcon(QIcon(swatch)); }
            title->setToolTip(resource.title + "\n" + courseText);
            if (resource.kind == "book") view.model->appendRow({title, new QStandardItem(attachment)});
            else if (resource.kind == "lecture") {
                auto* notes = new QStandardItem(resource.notes); notes->setToolTip(resource.notes);
                view.model->appendRow({title, new QStandardItem(resource.date.toString(Qt::ISODate)), new QStandardItem(attachment), notes});
            } else view.model->appendRow({title, new QStandardItem(attachment), new QStandardItem(resource.folder),
                new QStandardItem(categoryTitle(resource.category)), new QStandardItem(resource.examDate.toString(Qt::ISODate))});
        }
        view.table->setSortingEnabled(true); view.table->sortByColumn(0, Qt::AscendingOrder);
        view.table->setColumnWidth(0, 170);
        view.table->setColumnWidth(1, it.key() == "lecture" ? 100 : 230);
        if (it.key() == "lecture") view.table->setColumnWidth(2, 180);
        if (it.key() == "bookmark") { view.table->setColumnWidth(2, 130); view.table->setColumnWidth(3, 100); }
        view.table->horizontalHeader()->setStretchLastSection(true);
        for (int row = 0; row < view.model->rowCount(); ++row)
            if (view.model->index(row, 0).data(Qt::UserRole).toString() == selected) { view.table->selectRow(row); break; }
    }
    updateActions();
}

void MainWindow::editResource(const QString& kind, bool existing)
{
    Resource resource;
    resource.kind = kind;
    if (const auto* course = selectedCourse()) resource.courseId = course->id;
    resource.folder = currentFolder();
    if (existing) {
        const QString id = selectedResourceId(kind);
        for (const auto& item : snapshot_.resources) if (item.id == id) resource = item;
        if (resource.id.isEmpty()) return;
    }
    QDialog dialog(this);
    dialog.setObjectName(kind + "Dialog"); dialog.setWindowTitle((existing ? "Edit " : "New ") + kind);
    dialog.resize(650, kind == "lecture" ? 500 : 320);
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    auto* title = new QLineEdit(resource.title, &dialog); title->setObjectName("resourceTitle");
    auto* course = courseBox(&dialog, snapshot_, resource.courseId);
    form->addRow("Title:", title); form->addRow("Course:", course);
    QString selectedFile;
    QComboBox* attachment = nullptr;
    QDateEdit* date = nullptr;
    QPlainTextEdit* notes = nullptr;
    QLineEdit* url = nullptr;
    QComboBox* folder = nullptr;
    QComboBox* category = nullptr;
    QCheckBox* hasExamDate = nullptr;
    QDateEdit* examDate = nullptr;
    if (kind != "bookmark") {
        attachment = new QComboBox(&dialog); attachment->setObjectName("resourceAttachment");
        attachment->addItem("No attachment", QString(""));
        for (const auto& m : snapshot_.materials) if (!m.path.startsWith(".virgil/")) attachment->addItem(m.path + (m.available ? "" : " [missing]"), m.id);
        // Retain a trashed attachment when editing its notes/title.
        if (!resource.materialId.isEmpty() && attachment->findData(resource.materialId) < 0)
            attachment->addItem("Retained attachment in library trash", resource.materialId);
        attachment->setCurrentIndex(qMax(0, attachment->findData(resource.materialId)));
        auto* fileRow = new QHBoxLayout;
        auto* browse = new QPushButton("Browse files…", &dialog); browse->setObjectName("browseResourceFile");
        fileRow->addWidget(browse); fileRow->addWidget(attachment, 1);
        auto* chosen = explanation("Choose any local file on your computer, or an existing library material above. Outside files are copied into the library when you save.", &dialog);
        chosen->setObjectName("resourceFileSelection");
        connect(attachment, &QComboBox::currentIndexChanged, &dialog, [&selectedFile, chosen] {
            selectedFile.clear(); chosen->setText("Using the selected library material.");
        });
        connect(browse, &QPushButton::clicked, &dialog, [&, chosen] {
            QFileDialog picker(&dialog, "Choose " + kind + " file", selectedFile.isEmpty() ? QDir::homePath() : QFileInfo(selectedFile).absolutePath());
            picker.setObjectName("resourceFilePicker");
            picker.setFileMode(QFileDialog::ExistingFile); picker.setNameFilter("All files (*)");
            if (picker.exec() != QDialog::Accepted || picker.selectedFiles().isEmpty()) return;
            const QString path = picker.selectedFiles().first();
            const int previous = attachment->findData(true, Qt::UserRole + 1);
            if (previous >= 0) attachment->removeItem(previous);
            attachment->addItem("Selected file: " + QFileInfo(path).fileName(), QString(""));
            attachment->setItemData(attachment->count() - 1, true, Qt::UserRole + 1);
            attachment->setCurrentIndex(attachment->count() - 1);
            selectedFile = path;
            chosen->setText(selectedFile + "\nOutside files are copied on Save; originals are kept.");
            if (title->text().trimmed().isEmpty()) title->setText(QFileInfo(selectedFile).completeBaseName());
        });
        form->addRow(kind == "book" ? "Local file (required):" : "Local file (optional):", fileRow);
        form->addRow(chosen);
    }
    if (kind == "lecture") {
        date = dateEdit(&dialog, resource.date); date->setObjectName("lectureDate");
        notes = new QPlainTextEdit(resource.notes, &dialog); notes->setObjectName("lectureNotes");
        form->addRow("Lecture date:", date); form->addRow("Notes:", notes);
    }
    if (kind == "bookmark") {
        url = new QLineEdit(resource.url, &dialog); url->setObjectName("bookmarkUrl"); url->setPlaceholderText("https://…");
        folder = new QComboBox(&dialog); folder->setObjectName("bookmarkFolder");
        folder->addItem("Course / library (no specific folder)", QString(""));
        for (const auto& f : snapshot_.folders) folder->addItem(f.path, f.path);
        if (!resource.folder.isEmpty() && folder->findData(resource.folder) < 0) folder->addItem(resource.folder + " [missing]", resource.folder);
        folder->setCurrentIndex(qMax(0, folder->findData(resource.folder)));
        category = categoryBox(&dialog, resource.category);
        hasExamDate = new QCheckBox("Exam date known", &dialog); hasExamDate->setChecked(resource.examDate.isValid()); hasExamDate->setObjectName("hasExamDate");
        examDate = dateEdit(&dialog, resource.examDate); examDate->setEnabled(hasExamDate->isChecked());
        connect(hasExamDate, &QCheckBox::toggled, examDate, &QWidget::setEnabled);
        form->addRow("URL:", url); form->addRow("Folder scope:", folder); form->addRow("Category:", category); form->addRow(hasExamDate, examDate);
    }
    layout->addLayout(form); buttons(dialog, layout, "Save " + kind);
    if (dialog.exec() != QDialog::Accepted) return;
    resource.title = title->text(); resource.courseId = course->currentData().toString();
    if (attachment) resource.materialId = attachment->currentData().toString();
    if (date) resource.date = date->date();
    if (notes) resource.notes = notes->toPlainText();
    if (url) { resource.url = url->text(); resource.folder = folder->currentData().toString(); resource.category = category->currentData().toString(); resource.examDate = hasExamDate->isChecked() ? examDate->date() : QDate{}; }
    controller_->saveResource(resource, selectedFile);
}

void MainWindow::deleteResource(const QString& kind)
{
    const QString id = selectedResourceId(kind);
    if (id.isEmpty()) return;
    if (QMessageBox::question(this, "Delete " + kind + " record", "Delete this saved record? Its attached file, if any, will stay in Materials.") == QMessageBox::Yes)
        controller_->removeResource(id);
}

void MainWindow::openResource(const QString& kind)
{
    const QString id = selectedResourceId(kind);
    for (const auto& resource : snapshot_.resources) {
        if (resource.id != id) continue;
        if (kind == "bookmark") {
            if (!QDesktopServices::openUrl(QUrl(resource.url))) showError("The desktop could not open this URL. Remote pages require internet access.");
        } else if (!resource.materialId.isEmpty()) controller_->resolveMaterial(resource.materialId);
        else showError("This lecture has no attachment. Use Edit to read its saved notes.");
        return;
    }
}

void MainWindow::chooseCatalog(QLineEdit* name, QLineEdit* code)
{
    const auto catalog = courseCatalog();
    QDialog dialog(this); dialog.setObjectName("catalogDialog"); dialog.setWindowTitle("Offline FINKI SEIS23 catalog"); dialog.resize(740, 520);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(explanation("Four-year English SEIS23 • verified 2026-09-27. Two contradictory-code rows are excluded. This snapshot fills course names/codes; it does not validate prerequisites. Manual entry always works.", &dialog));
    auto* search = new QLineEdit(&dialog); search->setObjectName("catalogSearch"); search->setPlaceholderText("Search name or code…"); layout->addWidget(search);
    auto* list = new QListWidget(&dialog); list->setObjectName("catalogCourses"); layout->addWidget(list);
    for (const auto& course : catalog) {
        auto* item = new QListWidgetItem(course.code + " · " + course.name + QString(" · semester %1 · %2").arg(course.semester).arg(course.required ? "required" : "elective"), list);
        item->setData(Qt::UserRole, course.code); item->setData(Qt::UserRole + 1, course.name);
    }
    connect(search, &QLineEdit::textChanged, &dialog, [list](const QString& query) {
        for (int row = 0; row < list->count(); ++row) list->item(row)->setHidden(!list->item(row)->text().contains(query, Qt::CaseInsensitive));
    });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [&dialog] { dialog.accept(); });
    auto* source = new QPushButton("Open official source page (internet required)", &dialog);
    connect(source, &QPushButton::clicked, &dialog, [] { QDesktopServices::openUrl(QUrl("https://finki.ukim.mk/en/program/SEIS23/")); });
    layout->addWidget(source); buttons(dialog, layout, "Use selected course");
    if (dialog.exec() == QDialog::Accepted && list->currentItem() && !list->currentItem()->isHidden()) {
        name->setText(list->currentItem()->data(Qt::UserRole + 1).toString()); code->setText(list->currentItem()->data(Qt::UserRole).toString());
    }
}
} // namespace virgil
