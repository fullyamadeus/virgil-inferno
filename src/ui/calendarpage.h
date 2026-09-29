#pragma once
#include "domain/records.h"
#include <QWidget>

class QComboBox;
class QDateEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace virgil {
class LibraryController;
class CalendarPage : public QWidget {
    Q_OBJECT
public:
    CalendarPage(LibraryController* controller, QWidget* parent = nullptr);
    void setSnapshot(const Snapshot& snapshot);
signals:
    void error(const QString& message);
private:
    void render();
    void updateActions();
    void editEvent(const QString& id = {});
    void editClasses(const QString& id = {});
    void editRange(const QString& id = {});
    void editSemester();
    void editTimeZone();
    void removeSelected(const QString& type);
    QString semesterId() const;
    void activateEntry(const QString& eventId, const QString& classId);
    LibraryController* controller_;
    Snapshot snapshot_;
    QComboBox* semesters_;
    QComboBox* view_;
    QDateEdit* anchor_;
    QLabel* heading_;
    QLabel* policy_;
    QTableWidget* grid_;
    QTreeWidget* agenda_;
    QTreeWidget* classes_;
    QTreeWidget* ranges_;
    QListWidget* upcoming_;
    QPushButton* newEvent_;
    QPushButton* editEvent_;
    QPushButton* deleteEvent_;
    QPushButton* complete_;
    QPushButton* newClasses_;
    QPushButton* editClasses_;
    QPushButton* deleteClasses_;
    QPushButton* newRange_;
    QPushButton* editRange_;
    QPushButton* deleteRange_;
    QPushButton* semesterEdit_;
    QPushButton* timezoneEdit_;
    QString selectedEvent_;
    QString selectedClass_;
    QString selectedRange_;
};
} // namespace virgil
