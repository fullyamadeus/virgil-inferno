#pragma once

#include "domain/records.h"
#include <QMainWindow>
#include <QSettings>
#include <QMap>

class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QStandardItemModel;
class QTableView;
class QTreeWidget;
class QCloseEvent;
class QEvent;
class QToolBar;
class QTabWidget;
class QLineEdit;
class QCheckBox;

namespace virgil {
class LibraryController;
class CalendarPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    LibraryController* controller() const { return controller_; }
    void openRememberedLibrary();

protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void chooseLibrary(bool create);
    void reviewDirectory(const DirectoryPreview& preview);
    void addSemester();
    void addCourse();
    void deleteCourse();
    void editOrganization(bool course);
    void editMaterialMetadata();
    void chooseCatalog(QLineEdit* name, QLineEdit* code);
    QWidget* buildResourcePage(const QString& kind, QWidget* parent);
    void renderResources();
    void editResource(const QString& kind, bool existing);
    void deleteResource(const QString& kind);
    void openResource(const QString& kind);
    QString selectedResourceId(const QString& kind) const;
    QString selectedSemesterId() const;
    bool matchesSearch(const QString& value) const;
    void importMaterial(const QString& dropped = {});
    void renderMaterials();
    QString currentFolder() const;
    QString selectedPath() const;
    void organizePath(bool rename);
    void newFolder();
    void trashSelection();
    void relinkSelection();
    void reviewRecovery(bool trash);
    void applySnapshot(const Snapshot& snapshot, const QString& message);
    void showCourse();
    void updateActions();
    void openSelection(bool folder = false);
    void showError(const QString& message);
    const Course* selectedCourse() const;
    QString selectedMaterialId() const;

    CalendarPage* calendar_ = nullptr;
    LibraryController* controller_ = nullptr;
    QSettings settings_;
    Snapshot snapshot_;
    bool closing_ = false;
    QListWidget* navigation_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QTreeWidget* courses_ = nullptr;
    QTableView* materials_ = nullptr;
    QStandardItemModel* materialModel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QLabel* dashboard_ = nullptr;
    QLabel* settingsLabel_ = nullptr;
    QLabel* courseHeading_ = nullptr;
    QLabel* materialHint_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QAction* createLibraryAction_ = nullptr;
    QAction* openLibraryAction_ = nullptr;
    QAction* semesterAction_ = nullptr;
    QAction* courseAction_ = nullptr;
    QAction* importAction_ = nullptr;
    QAction* refreshAction_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* folderAction_ = nullptr;
    QComboBox* browseFolders_ = nullptr;
    QToolBar* breadcrumbs_ = nullptr;
    QString browsingCourse_;
    QList<QAction*> organizeActions_;
    QAction* deleteCourseAction_ = nullptr;
    QAction* editCourseAction_ = nullptr;
    QAction* editSemesterAction_ = nullptr;
    QAction* metadataAction_ = nullptr;
    QLineEdit* search_ = nullptr;
    QCheckBox* searchAll_ = nullptr;
    QTabWidget* resourceTabs_ = nullptr;
    QListWidget* dashboardCourses_ = nullptr;
    struct ResourceView { QTableView* table = nullptr; QStandardItemModel* model = nullptr; QList<QAction*> actions; };
    QMap<QString, ResourceView> resourceViews_;
};
} // namespace virgil
