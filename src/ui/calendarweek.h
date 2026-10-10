#pragma once
#include "services/calendar.h"
#include <QScrollArea>

namespace virgil {
// Presentation formatting also used by the month view; saved titles stay intact.
QString calendarEntryTitle(const QString& title, const QString& kind);

class CalendarWeek : public QScrollArea {
    Q_OBJECT
public:
    explicit CalendarWeek(QWidget* parent = nullptr);
    void setWeek(QDate monday, const QList<calendar::Occurrence>& entries,
                 const Snapshot& snapshot, const QList<ExamRange>& periods);
signals:
    void entrySelected(const QString& eventId, const QString& classId);
    void entryActivated(const QString& eventId, const QString& classId);
private:
    QDate monday_;
};
} // namespace virgil
