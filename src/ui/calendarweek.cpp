#include "ui/calendarweek.h"
#include <QAbstractButton>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <algorithm>
#include <cmath>
#include <functional>

namespace virgil {
QString calendarEntryTitle(const QString& title, const QString& kind)
{
    if (kind != "lab" && kind != "auditory") return title;
    QString result = title.trimmed();
    const QString badge = kind == "lab" ? "[LAB]" : "[AUDITORY]";
    if (result.endsWith(badge)) result.chop(badge.size());
    result = result.trimmed();
    const QString suffix = " · " + eventKindTitle(kind);
    if (result.endsWith(suffix, Qt::CaseInsensitive)) result.chop(suffix.size());
    return result.trimmed() + " " + badge;
}
namespace {
constexpr int firstMinute = 8 * 60;
constexpr int lastMinute = 21 * 60;
constexpr int hourHeight = 54;
constexpr int gutter = 56;
constexpr int specialHeight = 50;
const QColor auditoryMarker("#808080");
using EntryAction = std::function<void(const QString&, const QString&)>;

QColor foreground(const QColor& color)
{
    const auto channel = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    const double luminance = 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
    return luminance > 0.179 ? QColor("#17231f") : QColor(Qt::white);
}
QFont smallFont(QFont font)
{
    if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * 0.8);
    else if (font.pixelSize() > 0) font.setPixelSize(qMax(1, qRound(font.pixelSize() * 0.8)));
    return font;
}
class WeekCard final : public QAbstractButton {
public:
    WeekCard(const calendar::Occurrence& entry, const Snapshot& snapshot, QWidget* parent,
             EntryAction activate) : QAbstractButton(parent), entry_(entry), activate_(std::move(activate))
    {
        setObjectName("calendarWeekEntry-" + (entry.classId.isEmpty() ? entry.eventId : entry.classId) + "-" + entry.date.toString(Qt::ISODate));
        setProperty("eventId", entry.eventId); setProperty("classId", entry.classId);
        setText(calendarEntryTitle(entry.title, entry.kind));
        setFocusPolicy(Qt::StrongFocus); setCheckable(true);
        const auto zone = calendar::timeZone(snapshot.calendarTimeZone);
        time_ = !entry.warning.isEmpty() ? "Needs review" : entry.allDay ? "All day"
            : entry.startsAt.toTimeZone(zone).toString("HH:mm") + "–" + entry.endsAt.toTimeZone(zone).toString("HH:mm");
        for (const auto& course : snapshot.courses) if (course.id == entry.courseId) color_ = QColor(course.color);
        QString details = text() + "\n" + entry.date.toString(Qt::ISODate) + " · " + time_;
        if (!entry.room.isEmpty()) details += "\nRoom: " + entry.room;
        if (!entry.warning.isEmpty()) details += "\n" + entry.warning;
        setToolTip(details); setAccessibleName(details);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this); painter.setClipRect(rect());
        const QColor background = color_.isValid() ? color_ : palette().color(QPalette::AlternateBase);
        const QColor ink = color_.isValid() ? foreground(background) : palette().color(QPalette::Text);
        painter.fillRect(rect(), background);
        painter.setPen(isChecked() || hasFocus() ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        painter.setPen(ink);
        const QFont small = smallFont(font()); const QFontMetrics smallMetrics(small);
        painter.setFont(small);
        int top = 3;
        if (!entry_.room.isEmpty()) {
            painter.drawText(QRect(4, top, qMax(0, width() - 16), smallMetrics.height()), Qt::AlignLeft | Qt::AlignTop,
                             smallMetrics.elidedText(entry_.room, Qt::ElideRight, qMax(0, width() - 16)));
            top += smallMetrics.height() + 1;
        }
        painter.drawText(QRect(4, top, qMax(0, width() - 8), smallMetrics.height()), Qt::AlignLeft | Qt::AlignTop,
                         smallMetrics.elidedText(time_, Qt::ElideRight, qMax(0, width() - 8)));
        top += smallMetrics.height() + 2;
        const QString badge = entry_.kind == "auditory" ? "[AUDITORY]" : entry_.kind == "lab" ? "[LAB]" : QString();
        QString title = text();
        if (!badge.isEmpty()) title.chop(badge.size());
        QFont badgeFont = small;
        const int availableWidth = qMax(1, width() - 8);
        const int badgeWidth = QFontMetrics(badgeFont).horizontalAdvance(badge);
        if (badgeWidth > availableWidth) {
            const double scale = double(availableWidth) / badgeWidth;
            if (badgeFont.pointSizeF() > 0) badgeFont.setPointSizeF(badgeFont.pointSizeF() * scale);
            else badgeFont.setPixelSize(qMax(1, static_cast<int>(std::floor(badgeFont.pixelSize() * scale))));
        }
        const int badgeHeight = badge.isEmpty() ? 0 : QFontMetrics(badgeFont).height() + 2;
        QFont titleFont = font(); titleFont.setStrikeOut(entry_.completed); painter.setFont(titleFont);
        painter.drawText(QRect(4, top, availableWidth, qMax(0, height() - top - 2 - badgeHeight)),
                         Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, title.trimmed());
        if (!badge.isEmpty()) {
            painter.setFont(badgeFont);
            painter.drawText(QRect(4, qMax(top, height() - badgeHeight - 2), availableWidth, badgeHeight),
                             Qt::AlignLeft | Qt::AlignTop, badge);
        }
        if (entry_.kind == "auditory") {
            painter.setPen(Qt::NoPen); painter.setBrush(auditoryMarker);
            const QPoint corner = rect().topRight();
            painter.drawPolygon(QPolygon{corner, corner + QPoint(-9, 0), corner + QPoint(0, 9)});
        }
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) { activate_(entry_.eventId, entry_.classId); event->accept(); }
        else QAbstractButton::mouseDoubleClickEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { activate_(entry_.eventId, entry_.classId); event->accept(); }
        else QAbstractButton::keyPressEvent(event);
    }
private:
    calendar::Occurrence entry_;
    EntryAction activate_;
    QColor color_;
    QString time_;
};
struct Placement {
    WeekCard* card;
    int day;
    double start;
    double end;
    int lane = 0;
    int lanes = 1;
    int special = -1;
};
class WeekCanvas final : public QWidget {
public:
    WeekCanvas(QDate monday, const QList<calendar::Occurrence>& entries, const Snapshot& snapshot,
               const QList<ExamRange>& periods, EntryAction select, EntryAction activate)
    {
        setObjectName("calendarWeekCanvas"); setMinimumWidth(500);
        const auto zone = calendar::timeZone(snapshot.calendarTimeZone);
        int specialCounts[6]{};
        for (int day = 0; day < 6; ++day) {
            const QDate date = monday.addDays(day);
            auto* heading = new QPushButton(date.toString("ddd d MMM"), this);
            heading->setObjectName("calendarDay-" + date.toString(Qt::ISODate));
            heading->setFlat(true); headings_.append(heading);
            for (const auto& period : periods) if (date >= period.start && date <= period.end) {
                periodDays_[day] = true; hasPeriods_ = true;
                heading->setToolTip("Exam period: " + period.start.toString(Qt::ISODate) + " – " + period.end.toString(Qt::ISODate));
            }
        }
        for (const auto& entry : entries) {
            const int day = static_cast<int>(monday.daysTo(entry.date));
            if (day < 0 || day >= 6) continue;
            const auto starts = entry.startsAt.toTimeZone(zone), ends = entry.endsAt.toTimeZone(zone);
            const double start = starts.date() < entry.date ? 0 : QTime(0, 0).msecsTo(starts.time()) / 60000.0;
            const double end = ends.date() > entry.date ? 1440 : QTime(0, 0).msecsTo(ends.time()) / 60000.0;
            const bool special = entry.allDay || !entry.warning.isEmpty() || end <= start || end <= firstMinute || start >= lastMinute;
            auto* card = new WeekCard(entry, snapshot, this, activate);
            placements_.append({card, day, qMax(start, double(firstMinute)), qMin(end, double(lastMinute)), 0, 1,
                                special ? specialCounts[day]++ : -1});
            connect(card, &QAbstractButton::clicked, this, [this, card, entry, select] {
                for (const auto& p : placements_) p.card->setChecked(p.card == card);
                select(entry.eventId, entry.classId);
            });
        }
        for (int count : specialCounts) specialRows_ = qMax(specialRows_, count);
        // Greedy interval partitioning within each connected overlap group. Touching
        // endpoints are not overlaps, so back-to-back classes can reuse a lane.
        for (int day = 0; day < 6; ++day) {
            QList<Placement*> timed;
            for (auto& p : placements_) if (p.day == day && p.special < 0) timed.append(&p);
            std::stable_sort(timed.begin(), timed.end(), [](const auto* a, const auto* b) {
                return a->start == b->start ? a->end < b->end : a->start < b->start;
            });
            QList<Placement*> group; QList<double> laneEnds; double groupEnd = -1;
            const auto finish = [&] { for (auto* p : group) p->lanes = static_cast<int>(laneEnds.size()); group.clear(); laneEnds.clear(); };
            for (auto* p : timed) {
                if (p->start >= groupEnd) finish();
                int lane = 0;
                while (lane < laneEnds.size() && laneEnds[lane] > p->start) ++lane;
                if (lane == laneEnds.size()) laneEnds.append(p->end); else laneEnds[lane] = p->end;
                p->lane = lane; group.append(p); groupEnd = qMax(groupEnd, p->end);
            }
            finish();
        }
        gridTop_ = 34 + (hasPeriods_ ? 20 : 0) + specialRows_ * specialHeight;
        setMinimumHeight(gridTop_ + (lastMinute - firstMinute) * hourHeight / 60 + 20);
    }
protected:
    void resizeEvent(QResizeEvent*) override
    {
        const double dayWidth = (width() - gutter) / 6.0;
        for (int day = 0; day < 6; ++day)
            headings_[day]->setGeometry(gutter + qRound(day * dayWidth), 0, qRound(dayWidth), 30);
        for (const auto& p : placements_) {
            const double laneWidth = dayWidth / p.lanes;
            const int left = gutter + qRound(p.day * dayWidth + p.lane * laneWidth);
            const int right = gutter + qRound(p.day * dayWidth + (p.lane + 1) * laneWidth);
            const int top = p.special >= 0 ? 34 + (hasPeriods_ ? 20 : 0) + p.special * specialHeight
                : gridTop_ + qRound((p.start - firstMinute) * hourHeight / 60);
            const int bottom = p.special >= 0 ? top + specialHeight
                : gridTop_ + qRound((p.end - firstMinute) * hourHeight / 60);
            p.card->setGeometry(left + 1, top + 1, qMax(1, right - left - 2), qMax(1, bottom - top - 2));
        }
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this); painter.fillRect(rect(), palette().brush(QPalette::Base));
        const double dayWidth = (width() - gutter) / 6.0;
        painter.setPen(palette().color(QPalette::Text));
        if (specialRows_ > 0) painter.drawText(QRect(0, 34 + (hasPeriods_ ? 20 : 0), gutter - 4, 30), Qt::AlignRight | Qt::AlignTop, "Other");
        for (int day = 0; day < 6; ++day) if (periodDays_[day])
            painter.drawText(QRect(gutter + qRound(day * dayWidth), 32, qRound(dayWidth), 20), Qt::AlignCenter, "Exam period");
        for (int hour = 8; hour <= 21; ++hour) {
            const int y = gridTop_ + (hour - 8) * hourHeight;
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(QRect(0, y, gutter - 5, 20), Qt::AlignRight | Qt::AlignTop, QString("%1:00").arg(hour, 2, 10, QChar('0')));
            painter.setPen(palette().color(QPalette::Mid)); painter.drawLine(gutter, y, width(), y);
        }
        for (int day = 0; day <= 6; ++day) {
            const int x = gutter + qRound(day * dayWidth);
            painter.drawLine(x, 0, x, gridTop_ + 13 * hourHeight);
        }
    }
private:
    QList<QPushButton*> headings_;
    QList<Placement> placements_;
    bool periodDays_[6]{};
    bool hasPeriods_ = false;
    int specialRows_ = 0;
    int gridTop_ = 34;
};
} // namespace
CalendarWeek::CalendarWeek(QWidget* parent) : QScrollArea(parent)
{
    setObjectName("calendarWeek"); setWidgetResizable(true); setFrameShape(QFrame::StyledPanel);
    setAccessibleName("Monday to Saturday timetable, 08:00 to 21:00");
}
void CalendarWeek::setWeek(QDate monday, const QList<calendar::Occurrence>& entries,
                           const Snapshot& snapshot, const QList<ExamRange>& periods)
{
    const int scroll = monday == monday_ ? verticalScrollBar()->value() : 0;
    monday_ = monday;
    auto* canvas = new WeekCanvas(monday, entries, snapshot, periods,
        [this](const QString& eventId, const QString& classId) { emit entrySelected(eventId, classId); },
        [this](const QString& eventId, const QString& classId) { emit entryActivated(eventId, classId); });
    setWidget(canvas); verticalScrollBar()->setValue(scroll);
}
} // namespace virgil
