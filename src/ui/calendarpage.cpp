#include "ui/calendarpage.h"
#include "services/librarycontroller.h"
#include "services/calendar.h"
#include "domain/error.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimeEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>

namespace virgil {
namespace {
const QStringList weekdays{"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
QLabel* label(const QString& text, QWidget* parent)
{
    auto* result = new QLabel(text, parent); result->setTextFormat(Qt::PlainText); result->setWordWrap(true); return result;
}
QPushButton* button(const QString& text, const QString& name, QWidget* parent, QHBoxLayout* row)
{
    auto* result = new QPushButton(text, parent); result->setObjectName(name); row->addWidget(result); return result;
}
QDateEdit* dateField(QDate date, const QString& name, QWidget* parent)
{
    auto* result = new QDateEdit(date.isValid() ? date : QDate::currentDate(), parent);
    result->setObjectName(name); result->setCalendarPopup(true); result->setDisplayFormat("yyyy-MM-dd"); return result;
}
QTimeEdit* timeField(QTime time, const QString& name, QWidget* parent)
{
    auto* result = new QTimeEdit(time, parent); result->setObjectName(name); result->setDisplayFormat("HH:mm"); return result;
}
QComboBox* zoneField(const QString& current, QWidget* parent)
{
    auto* box = new QComboBox(parent); box->setObjectName("calendarZone"); box->setEditable(true);
    for (const auto& id : QTimeZone::availableTimeZoneIds()) box->addItem(QString::fromUtf8(id));
    box->setCurrentText(current); return box;
}
QComboBox* resolutionField(const QString& current, const QString& name, QWidget* parent)
{
    auto* box = new QComboBox(parent); box->setObjectName(name);
    box->addItem("Ask if time occurs twice", "reject"); box->addItem("Earlier occurrence", "earlier"); box->addItem("Later occurrence", "later");
    box->setCurrentIndex(qMax(0, box->findData(current))); return box;
}
QComboBox* semesterField(const Snapshot& snapshot, const QString& current, bool optional, QWidget* parent)
{
    auto* box = new QComboBox(parent); box->setObjectName("eventSemester");
    if (optional) box->addItem("All semesters / independent", QString(""));
    for (const auto& semester : snapshot.semesters) box->addItem(semester.name, semester.id);
    box->setCurrentIndex(qMax(0, box->findData(current))); return box;
}
void fillCourses(QComboBox* box, const Snapshot& snapshot, const QString& semesterId, const QString& current)
{
    box->clear(); box->addItem("No course", QString(""));
    for (const auto& course : snapshot.courses)
        if (course.semesterId == semesterId) box->addItem(course.name, course.id);
    box->setCurrentIndex(qMax(0, box->findData(current)));
}
QDialogButtonBox* dialogButtons(QDialog& dialog, QVBoxLayout* layout)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("calendarDialogButtons"); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); return buttons;
}
QString timeText(const calendar::Occurrence& entry, const QTimeZone& zone)
{
    if (!entry.warning.isEmpty()) return "Clock change: needs review";
    if (entry.allDay) return "All day";
    const auto start = entry.startsAt.toTimeZone(zone), end = entry.endsAt.toTimeZone(zone);
    return start.toString("HH:mm") + "–" + end.toString(start.date() == end.date() ? "HH:mm" : "yyyy-MM-dd HH:mm");
}
QString courseName(const Snapshot& snapshot, const QString& id)
{
    for (const auto& course : snapshot.courses) if (course.id == id) return course.name;
    return "No course";
}
QColor courseColor(const Snapshot& snapshot, const QString& id)
{
    for (const auto& course : snapshot.courses) if (course.id == id) return QColor(course.color);
    return {};
}
void colorItem(QListWidgetItem* item, const Snapshot& snapshot, const QString& courseId)
{
    const auto color = courseColor(snapshot, courseId);
    if (!color.isValid()) return;
    const auto channel = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    const double luminance = 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
    item->setBackground(color); item->setForeground(luminance > 0.179 ? QColor("#17231f") : QColor(Qt::white));
}
void swatch(QTreeWidgetItem* item, const Snapshot& snapshot, const QString& id)
{
    const auto color = courseColor(snapshot, id);
    if (color.isValid()) { QPixmap pixmap(14, 14); pixmap.fill(color); item->setIcon(0, QIcon(pixmap)); }
}
QString existingResolution(QDateTime instant, const QString& zone)
{
    const auto local = instant.toTimeZone(calendar::timeZone(zone));
    try {
        (void)calendar::localToUtc(local.date(), local.time(), zone);
        return "reject";
    } catch (const Error&) {
        return calendar::localToUtc(local.date(), local.time(), zone, "earlier") == instant.toUTC() ? "earlier" : "later";
    }
}
}

CalendarPage::CalendarPage(LibraryController* controller, QWidget* parent) : QWidget(parent), controller_(controller)
{
    setObjectName("calendarPage");
    auto* layout = new QVBoxLayout(this);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(label("Semester:", this));
    semesters_ = new QComboBox(this); semesters_->setObjectName("calendarSemester"); toolbar->addWidget(semesters_, 1);
    semesterEdit_ = button("Semester / teaching dates…", "calendarSemesterEdit", this, toolbar);
    timezoneEdit_ = button("Timezone…", "calendarTimezoneButton", this, toolbar);
    layout->addLayout(toolbar);
    auto* navigation = new QHBoxLayout;
    auto* previous = button("Previous", "calendarPrevious", this, navigation);
    auto* today = button("Today", "calendarToday", this, navigation);
    auto* next = button("Next", "calendarNext", this, navigation);
    anchor_ = dateField(QDate::currentDate(), "calendarDate", this); navigation->addWidget(anchor_);
    view_ = new QComboBox(this); view_->setObjectName("calendarView"); view_->addItems({"Month", "Week"}); navigation->addWidget(view_);
    heading_ = label({}, this); heading_->setObjectName("calendarHeading"); navigation->addWidget(heading_, 1);
    layout->addLayout(navigation);
    policy_ = label("Open a library to use the calendar.", this); policy_->setObjectName("calendarPolicy"); layout->addWidget(policy_);
    auto* eventButtons = new QHBoxLayout;
    newEvent_ = button("New event…", "calendarNewEvent", this, eventButtons);
    editEvent_ = button("Edit event…", "calendarEditEvent", this, eventButtons);
    deleteEvent_ = button("Delete event…", "calendarDeleteEvent", this, eventButtons);
    complete_ = button("Mark assignment complete", "calendarComplete", this, eventButtons);
    eventButtons->addStretch(); layout->addLayout(eventButtons);
    auto* mainSplit = new QSplitter(Qt::Vertical, this);
    auto* displaySplit = new QSplitter(mainSplit);
    grid_ = new QTableWidget(displaySplit); grid_->setObjectName("calendarGrid"); grid_->setColumnCount(7);
    grid_->setHorizontalHeaderLabels(weekdays); grid_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    grid_->verticalHeader()->hide(); grid_->verticalHeader()->setMinimumSectionSize(64);
    grid_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch); grid_->setEditTriggers(QAbstractItemView::NoEditTriggers); grid_->setSelectionMode(QAbstractItemView::NoSelection);
    auto* aside = new QWidget(displaySplit); auto* asideLayout = new QVBoxLayout(aside);
    asideLayout->addWidget(label("Coming up (saved events)", aside));
    upcoming_ = new QListWidget(aside); upcoming_->setObjectName("calendarUpcoming"); upcoming_->setWordWrap(true); asideLayout->addWidget(upcoming_);
    displaySplit->setStretchFactor(0, 4); displaySplit->setStretchFactor(1, 1); displaySplit->setSizes({850, 220});
    auto* management = new QTabWidget(mainSplit); management->setObjectName("calendarManagement");
    auto makeTab = [&](const QString& title, QTreeWidget*& tree, const QString& name) {
        auto* page = new QWidget(management); auto* box = new QVBoxLayout(page); box->setContentsMargins(3, 3, 3, 3);
        tree = new QTreeWidget(page); tree->setObjectName(name); tree->setRootIsDecorated(false); tree->setAlternatingRowColors(true);
        tree->setSelectionMode(QAbstractItemView::SingleSelection); box->addWidget(tree); management->addTab(page, title); return box;
    };
    makeTab("Events in view", agenda_, "calendarAgenda"); agenda_->setHeaderLabels({"Title", "Date", "Time", "Type", "Course", "Status"});
    auto* classLayout = makeTab("Weekly timetable", classes_, "calendarClasses"); classes_->setHeaderLabels({"Title", "Weekday", "Time", "Type", "Course", "Timezone"});
    auto* classButtons = new QHBoxLayout;
    newClasses_ = button("Add weekly classes…", "calendarNewClasses", this, classButtons);
    editClasses_ = button("Edit class…", "calendarEditClass", this, classButtons);
    deleteClasses_ = button("Delete class…", "calendarDeleteClass", this, classButtons); classButtons->addStretch(); classLayout->addLayout(classButtons);
    auto* rangeLayout = makeTab("Exam periods", ranges_, "calendarRanges"); ranges_->setHeaderLabels({"Exam period", "Inclusive start", "Inclusive end"});
    auto* rangeButtons = new QHBoxLayout;
    newRange_ = button("Add exam period…", "calendarNewRange", this, rangeButtons);
    editRange_ = button("Edit period…", "calendarEditRange", this, rangeButtons);
    deleteRange_ = button("Delete period…", "calendarDeleteRange", this, rangeButtons); rangeButtons->addStretch(); rangeLayout->addLayout(rangeButtons);
    mainSplit->setStretchFactor(0, 4); mainSplit->setStretchFactor(1, 1); mainSplit->setSizes({460, 140}); layout->addWidget(mainSplit, 1);
    connect(previous, &QPushButton::clicked, this, [this] { anchor_->setDate(view_->currentIndex() == 0 ? anchor_->date().addMonths(-1) : anchor_->date().addDays(-7)); });
    connect(next, &QPushButton::clicked, this, [this] { anchor_->setDate(view_->currentIndex() == 0 ? anchor_->date().addMonths(1) : anchor_->date().addDays(7)); });
    connect(today, &QPushButton::clicked, this, [this] { try { anchor_->setDate(QDateTime::currentDateTimeUtc().toTimeZone(calendar::timeZone(snapshot_.calendarTimeZone)).date()); } catch (const Error& e) { emit error(QString::fromUtf8(e.what())); } });
    connect(anchor_, &QDateEdit::dateChanged, this, &CalendarPage::render);
    connect(view_, &QComboBox::currentIndexChanged, this, &CalendarPage::render);
    connect(semesters_, &QComboBox::currentIndexChanged, this, [this] {
        selectedEvent_.clear(); selectedClass_.clear(); selectedRange_.clear();
        for (const auto& semester : snapshot_.semesters) if (semester.id == semesterId()) { const QSignalBlocker block(anchor_); anchor_->setDate(semester.start); }
        render();
    });
    connect(newEvent_, &QPushButton::clicked, this, [this] { editEvent(); });
    connect(editEvent_, &QPushButton::clicked, this, [this] { editEvent(selectedEvent_); });
    connect(deleteEvent_, &QPushButton::clicked, this, [this] { removeSelected("event"); });
    connect(complete_, &QPushButton::clicked, this, [this] { for (const auto& event : snapshot_.events) if (event.id == selectedEvent_) controller_->completeCalendarAssignment(event.id, !event.completed); });
    connect(newClasses_, &QPushButton::clicked, this, [this] { editClasses(); });
    connect(editClasses_, &QPushButton::clicked, this, [this] { editClasses(selectedClass_); });
    connect(deleteClasses_, &QPushButton::clicked, this, [this] { removeSelected("class"); });
    connect(newRange_, &QPushButton::clicked, this, [this] { editRange(); });
    connect(editRange_, &QPushButton::clicked, this, [this] { editRange(selectedRange_); });
    connect(deleteRange_, &QPushButton::clicked, this, [this] { removeSelected("range"); });
    connect(semesterEdit_, &QPushButton::clicked, this, &CalendarPage::editSemester);
    connect(timezoneEdit_, &QPushButton::clicked, this, &CalendarPage::editTimeZone);
    connect(agenda_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (const auto* item = agenda_->currentItem()) { selectedEvent_ = item->data(0, Qt::UserRole).toString(); selectedClass_ = item->data(0, Qt::UserRole + 1).toString(); }
        updateActions();
    });
    connect(agenda_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { activateEntry(item->data(0, Qt::UserRole).toString(), item->data(0, Qt::UserRole + 1).toString()); });
    connect(classes_, &QTreeWidget::itemSelectionChanged, this, [this] { if (classes_->currentItem()) selectedClass_ = classes_->currentItem()->data(0, Qt::UserRole).toString(); updateActions(); });
    connect(classes_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { editClasses(item->data(0, Qt::UserRole).toString()); });
    connect(ranges_, &QTreeWidget::itemSelectionChanged, this, [this] { if (ranges_->currentItem()) selectedRange_ = ranges_->currentItem()->data(0, Qt::UserRole).toString(); updateActions(); });
    connect(ranges_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { editRange(item->data(0, Qt::UserRole).toString()); });
    connect(upcoming_, &QListWidget::itemSelectionChanged, this, [this] { if (upcoming_->currentItem()) selectedEvent_ = upcoming_->currentItem()->data(Qt::UserRole).toString(); updateActions(); });
    connect(upcoming_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { editEvent(item->data(Qt::UserRole).toString()); });
    connect(controller_, &LibraryController::busyChanged, this, &CalendarPage::updateActions);
    updateActions();
}

QString CalendarPage::semesterId() const { return semesters_->currentData().toString(); }
void CalendarPage::setSnapshot(const Snapshot& snapshot)
{
    const bool different = snapshot_.libraryId != snapshot.libraryId;
    const QString previous = different ? QString() : semesterId();
    snapshot_ = snapshot;
    if (different) { selectedEvent_.clear(); selectedClass_.clear(); selectedRange_.clear(); }
    {
        const QSignalBlocker block(semesters_);
        semesters_->clear(); semesters_->addItem("All semesters", QString(""));
        for (const auto& semester : snapshot.semesters) semesters_->addItem(semester.name, semester.id);
        if (different && !snapshot.semesters.isEmpty()) {
            int index = 1;
            QDate today = QDate::currentDate();
            try { today = QDateTime::currentDateTimeUtc().toTimeZone(calendar::timeZone(snapshot.calendarTimeZone)).date(); }
            catch (const Error& e) { emit error(QString::fromUtf8(e.what())); }
            for (const auto& semester : snapshot.semesters) if (today >= semester.start && today <= semester.end) index = semesters_->findData(semester.id);
            semesters_->setCurrentIndex(index);
            const QSignalBlocker dateBlock(anchor_);
            for (const auto& semester : snapshot.semesters) if (semester.id == semesterId()) anchor_->setDate(semester.start);
        } else semesters_->setCurrentIndex(qMax(0, semesters_->findData(previous)));
    }
    render();
}

void CalendarPage::activateEntry(const QString& eventId, const QString& classId)
{
    if (!eventId.isEmpty()) editEvent(eventId);
    else if (!classId.isEmpty()) editClasses(classId);
}

void CalendarPage::render()
{
    if (snapshot_.root.isEmpty()) return;
    try {
        const auto zone = calendar::timeZone(snapshot_.calendarTimeZone);
        const QDate anchor = anchor_->date(); const bool month = view_->currentIndex() == 0;
        const QDate first = month ? calendar::mondayOf(QDate(anchor.year(), anchor.month(), 1)) : calendar::mondayOf(anchor);
        const QDate monthLast(anchor.year(), anchor.month(), anchor.daysInMonth());
        const int rows = month ? static_cast<int>((first.daysTo(monthLast) + 7) / 7) : 1;
        const QDate last = first.addDays(rows * 7 - 1);
        heading_->setText(month ? anchor.toString("MMMM yyyy") : first.toString("dd MMM yyyy") + " – " + last.toString("dd MMM yyyy"));
        QString policy = "Display timezone: " + snapshot_.calendarTimeZone + ". ";
        for (const auto& semester : snapshot_.semesters) if (semester.id == semesterId()) {
            policy += semester.start.toString(Qt::ISODate) + " – " + semester.end.toString(Qt::ISODate) + ". Teaching: ";
            policy += semester.teachingStart.isValid() ? semester.teachingStart.toString(Qt::ISODate) + " – " + semester.teachingEnd.toString(Qt::ISODate)
                : semester.season == "winter" ? "October 1–December 31" : "February 18–May 31";
            policy += "; clipped to semester. Classes pause in period 1, resume after it, and stop at period 2. Saved events remain visible.";
        }
        policy_->setText(policy);
        const auto entries = calendar::occurrences(snapshot_, first, last, semesterId());
        const auto periods = semesterId().isEmpty() ? snapshot_.examRanges : calendar::semesterRanges(snapshot_, semesterId());
        grid_->clearContents(); grid_->setRowCount(rows);
        for (int row = 0; row < rows; ++row) {
            grid_->setRowHeight(row, month ? 110 : 300);
            for (int column = 0; column < 7; ++column) {
                const QDate date = first.addDays(row * 7 + column);
                auto* cell = new QWidget(grid_); auto* box = new QVBoxLayout(cell); box->setContentsMargins(3, 3, 3, 3); box->setSpacing(2);
                auto* day = new QPushButton(date.toString(month ? "d" : "ddd d MMM"), cell);
                day->setObjectName("calendarDay-" + date.toString(Qt::ISODate));
                day->setToolTip(date.toString(Qt::ISODate)); box->addWidget(day);
                if (date == QDateTime::currentDateTimeUtc().toTimeZone(zone).date()) { auto font = day->font(); font.setBold(true); day->setFont(font); }
                if (month && date.month() != anchor.month()) day->setFlat(true);
                connect(day, &QPushButton::clicked, this, [this, date] { const QSignalBlocker block(anchor_); anchor_->setDate(date); view_->setCurrentIndex(1); render(); });
                for (const auto& period : periods) if (date >= period.start && date <= period.end) {
                    const auto ordered = calendar::semesterRanges(snapshot_, period.semesterId);
                    int number = 0; for (qsizetype i = 0; i < ordered.size(); ++i) if (ordered[i].id == period.id) number = static_cast<int>(i + 1);
                    auto* marker = label("Exam period " + QString::number(number), cell); marker->setToolTip(period.start.toString(Qt::ISODate) + " – " + period.end.toString(Qt::ISODate)); box->addWidget(marker);
                }
                auto* list = new QListWidget(cell); list->setObjectName("calendarEntries-" + date.toString(Qt::ISODate)); list->setWordWrap(!month);
                list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); box->addWidget(list, 1);
                for (const auto& entry : entries) if (entry.date == date) {
                    auto* item = new QListWidgetItem((entry.completed ? "✓ " : "") + (month ? QString() : timeText(entry, zone) + " · ") + entry.title + (entry.kind == "lab" ? " [LAB]" : ""), list);
                    item->setData(Qt::UserRole, entry.eventId); item->setData(Qt::UserRole + 1, entry.classId);
                    item->setToolTip(entry.title + "\n" + timeText(entry, zone) + " · " + eventKindTitle(entry.kind) + " · " + courseName(snapshot_, entry.courseId) + "\n" + entry.warning);
                    colorItem(item, snapshot_, entry.courseId);
                    if (entry.completed) { auto font = item->font(); font.setStrikeOut(true); item->setFont(font); }
                }
                connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { selectedEvent_ = item->data(Qt::UserRole).toString(); selectedClass_ = item->data(Qt::UserRole + 1).toString(); updateActions(); });
                connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { activateEntry(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toString()); });
                grid_->setCellWidget(row, column, cell);
            }
        }
        {
            const QSignalBlocker block(agenda_); agenda_->clear();
            for (const auto& entry : entries) {
                auto* item = new QTreeWidgetItem(agenda_, {entry.title, entry.date.toString(Qt::ISODate), timeText(entry, zone), eventKindTitle(entry.kind), courseName(snapshot_, entry.courseId),
                    !entry.warning.isEmpty() ? "Needs clock-change review" : entry.completed ? "Complete" : entry.classId.isEmpty() ? "Saved event" : "Weekly class"});
                item->setData(0, Qt::UserRole, entry.eventId); item->setData(0, Qt::UserRole + 1, entry.classId); item->setToolTip(0, entry.warning); swatch(item, snapshot_, entry.courseId);
                if ((!selectedEvent_.isEmpty() && entry.eventId == selectedEvent_) || (!selectedClass_.isEmpty() && entry.classId == selectedClass_)) agenda_->setCurrentItem(item);
            }
        }
        {
            const QSignalBlocker block(classes_); classes_->clear();
            for (const auto& slot : snapshot_.weeklyClasses) if (semesterId().isEmpty() || slot.semesterId == semesterId()) {
                auto* item = new QTreeWidgetItem(classes_, {slot.title, weekdays.value(slot.weekday - 1), slot.start.toString("HH:mm") + "–" + slot.end.toString("HH:mm"), eventKindTitle(slot.kind), courseName(snapshot_, slot.courseId), slot.timeZone});
                item->setData(0, Qt::UserRole, slot.id); item->setToolTip(0, "Repeated time policy: " + slot.ambiguity + ". Nonexistent times are flagged, never shifted."); swatch(item, snapshot_, slot.courseId);
                if (slot.id == selectedClass_) classes_->setCurrentItem(item);
            }
        }
        {
            const QSignalBlocker block(ranges_); ranges_->clear();
            for (const auto& period : periods) {
                const auto ordered = calendar::semesterRanges(snapshot_, period.semesterId);
                int number = 0; for (qsizetype i = 0; i < ordered.size(); ++i) if (ordered[i].id == period.id) number = static_cast<int>(i + 1);
                QString name = "Exam period " + QString::number(number);
                if (semesterId().isEmpty()) for (const auto& semester : snapshot_.semesters) if (semester.id == period.semesterId) name += " · " + semester.name;
                auto* item = new QTreeWidgetItem(ranges_, {name, period.start.toString(Qt::ISODate), period.end.toString(Qt::ISODate)}); item->setData(0, Qt::UserRole, period.id);
                if (period.id == selectedRange_) ranges_->setCurrentItem(item);
            }
        }
        {
            const QSignalBlocker block(upcoming_); upcoming_->clear();
            for (const auto& event : calendar::upcoming(snapshot_, semesterId(), QDateTime::currentDateTimeUtc())) {
                const QString date = event.allDay ? event.date.toString(Qt::ISODate) : event.startsAt.toTimeZone(zone).toString("yyyy-MM-dd HH:mm");
                auto* item = new QListWidgetItem(event.title + "\n" + eventKindTitle(event.kind) + " · " + date, upcoming_); item->setData(Qt::UserRole, event.id); colorItem(item, snapshot_, event.courseId);
                if (event.id == selectedEvent_) upcoming_->setCurrentItem(item);
            }
        }
        for (auto* tree : {agenda_, classes_, ranges_}) { tree->resizeColumnToContents(0); tree->resizeColumnToContents(1); }
        updateActions();
    } catch (const Error& e) { emit error(QString::fromUtf8(e.what())); }
}

void CalendarPage::updateActions()
{
    const bool idle = !controller_->busy(); const bool open = !snapshot_.root.isEmpty();
    bool eventExists = false, assignment = false, completed = false, classExists = false, rangeExists = false;
    for (const auto& event : snapshot_.events) if (event.id == selectedEvent_) { eventExists = true; assignment = event.kind == "assignment"; completed = event.completed; }
    for (const auto& slot : snapshot_.weeklyClasses) if (slot.id == selectedClass_) classExists = true;
    for (const auto& range : snapshot_.examRanges) if (range.id == selectedRange_) rangeExists = true;
    newEvent_->setEnabled(idle && open); editEvent_->setEnabled(idle && eventExists); deleteEvent_->setEnabled(idle && eventExists);
    complete_->setEnabled(idle && assignment); complete_->setText(completed ? "Mark assignment incomplete" : "Mark assignment complete");
    newClasses_->setEnabled(idle && open && !semesterId().isEmpty()); editClasses_->setEnabled(idle && classExists); deleteClasses_->setEnabled(idle && classExists);
    newRange_->setEnabled(idle && open && !semesterId().isEmpty() && calendar::semesterRanges(snapshot_, semesterId()).size() < 3);
    editRange_->setEnabled(idle && rangeExists); deleteRange_->setEnabled(idle && rangeExists);
    semesterEdit_->setEnabled(idle && open && !semesterId().isEmpty()); timezoneEdit_->setEnabled(idle && open);
    semesters_->setEnabled(open); grid_->setEnabled(idle && open); agenda_->setEnabled(idle && open); classes_->setEnabled(idle && open); ranges_->setEnabled(idle && open); upcoming_->setEnabled(idle && open);
}

void CalendarPage::editEvent(const QString& id)
{
    if (controller_->busy()) return;
    CalendarEvent event; event.semesterId = semesterId(); event.date = anchor_->date(); event.timeZone = snapshot_.calendarTimeZone;
    if (!id.isEmpty()) {
        bool found = false; for (const auto& item : snapshot_.events) if (item.id == id) { event = item; found = true; }
        if (!found) return;
    }
    QDialog dialog(this); dialog.setObjectName("calendarEventDialog"); dialog.setWindowTitle(id.isEmpty() ? "New event" : "Edit event"); dialog.resize(620, 520);
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    auto* title = new QLineEdit(event.title, &dialog); title->setObjectName("eventTitle");
    auto* kind = new QComboBox(&dialog); kind->setObjectName("eventKind"); for (const auto& value : eventKinds) kind->addItem(eventKindTitle(value), value); kind->setCurrentIndex(kind->findData(event.kind));
    auto* semester = semesterField(snapshot_, event.semesterId, true, &dialog);
    auto* course = new QComboBox(&dialog); course->setObjectName("eventCourse"); fillCourses(course, snapshot_, event.semesterId, event.courseId);
    connect(semester, &QComboBox::currentIndexChanged, &dialog, [&, course] { fillCourses(course, snapshot_, semester->currentData().toString(), course->currentData().toString()); });
    auto* allDay = new QCheckBox("All-day event", &dialog); allDay->setObjectName("eventAllDay"); allDay->setChecked(event.allDay);
    auto* complete = new QCheckBox("Assignment complete", &dialog); complete->setObjectName("eventCompleted"); complete->setChecked(event.completed); complete->setEnabled(event.kind == "assignment");
    connect(kind, &QComboBox::currentIndexChanged, &dialog, [kind, complete] { const bool assignment = kind->currentData().toString() == "assignment"; complete->setEnabled(assignment); if (!assignment) complete->setChecked(false); });
    QTimeZone zone;
    try { zone = calendar::timeZone(event.timeZone); }
    catch (const Error& e) { emit error(QString::fromUtf8(e.what())); return; }
    const auto localStart = event.startsAt.toTimeZone(zone), localEnd = event.endsAt.toTimeZone(zone);
    auto* startDate = dateField(event.allDay ? event.date : localStart.date(), "eventDate", &dialog);
    auto* startTime = timeField(event.allDay ? QTime(12, 0) : localStart.time(), "eventStartTime", &dialog);
    auto* endDate = dateField(event.allDay ? event.date : localEnd.date(), "eventEndDate", &dialog);
    auto* endTime = timeField(event.allDay ? QTime(13, 30) : localEnd.time(), "eventEndTime", &dialog);
    auto* zoneBox = zoneField(event.timeZone, &dialog);
    auto* startChoice = resolutionField(event.allDay ? "reject" : existingResolution(event.startsAt, event.timeZone), "eventStartResolution", &dialog);
    auto* endChoice = resolutionField(event.allDay ? "reject" : existingResolution(event.endsAt, event.timeZone), "eventEndResolution", &dialog);
    auto* timing = new QWidget(&dialog); auto* timingForm = new QFormLayout(timing); timingForm->setContentsMargins(0, 0, 0, 0);
    timingForm->addRow("Start time:", startTime); timingForm->addRow("End date:", endDate); timingForm->addRow("End time:", endTime);
    timingForm->addRow("Timezone:", zoneBox); timingForm->addRow("Repeated start time:", startChoice); timingForm->addRow("Repeated end time:", endChoice);
    timing->setEnabled(!event.allDay); connect(allDay, &QCheckBox::toggled, timing, [timing](bool checked) { timing->setEnabled(!checked); });
    connect(startDate, &QDateEdit::dateChanged, &dialog, [endDate, startDate] { if (endDate->date() < startDate->date()) endDate->setDate(startDate->date()); });
    form->addRow("Title:", title); form->addRow("Type:", kind); form->addRow("Semester:", semester); form->addRow("Course (optional):", course);
    form->addRow(allDay); form->addRow("Date / start date:", startDate); form->addRow(timing); form->addRow(complete); layout->addLayout(form);
    layout->addWidget(label("All-day dates do not move with timezones. Timed events use the selected timezone and are saved as UTC instants. Choose earlier/later explicitly for a repeated local time; nonexistent times are rejected.", &dialog));
    auto* problem = label({}, &dialog); problem->setObjectName("calendarFormError"); layout->addWidget(problem);
    auto* buttons = dialogButtons(dialog, layout);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (title->text().trimmed().isEmpty()) throw Error("Enter an event title.");
            event.title = title->text(); event.kind = kind->currentData().toString(); event.semesterId = semester->currentData().toString(); event.courseId = course->currentData().toString();
            event.allDay = allDay->isChecked(); event.completed = complete->isChecked(); event.timeZone = zoneBox->currentText().trimmed();
            (void)calendar::timeZone(event.timeZone);
            if (event.allDay) { event.date = startDate->date(); event.startsAt = {}; event.endsAt = {}; }
            else {
                event.startsAt = calendar::localToUtc(startDate->date(), startTime->time(), event.timeZone, startChoice->currentData().toString());
                event.endsAt = calendar::localToUtc(endDate->date(), endTime->time(), event.timeZone, endChoice->currentData().toString());
                if (event.endsAt <= event.startsAt) throw Error("End time must be after start time.");
                event.date = {};
            }
            dialog.accept();
        } catch (const Error& e) { problem->setText(QString::fromUtf8(e.what())); }
    });
    if (dialog.exec() == QDialog::Accepted) controller_->saveCalendarEvent(event);
}

void CalendarPage::editClasses(const QString& id)
{
    if (controller_->busy()) return;
    WeeklyClass previous; previous.semesterId = semesterId(); previous.timeZone = snapshot_.calendarTimeZone;
    if (!id.isEmpty()) {
        bool found = false; for (const auto& slot : snapshot_.weeklyClasses) if (slot.id == id) { previous = slot; found = true; }
        if (!found) return;
    }
    QDialog dialog(this); dialog.setObjectName("weeklyClassDialog"); dialog.setWindowTitle(id.isEmpty() ? "Add weekly classes" : "Edit weekly class"); dialog.resize(680, 560);
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    auto* semester = semesterField(snapshot_, previous.semesterId, false, &dialog);
    auto* course = new QComboBox(&dialog); course->setObjectName("eventCourse"); fillCourses(course, snapshot_, previous.semesterId, previous.courseId);
    connect(semester, &QComboBox::currentIndexChanged, &dialog, [&, course] { fillCourses(course, snapshot_, semester->currentData().toString(), course->currentData().toString()); });
    auto* title = new QLineEdit(previous.title, &dialog); title->setObjectName("classTitle"); title->setPlaceholderText("Optional for a course; required without a course");
    auto* zone = zoneField(previous.timeZone, &dialog); auto* ambiguity = resolutionField(previous.ambiguity, "classResolution", &dialog);
    form->addRow("Semester:", semester); form->addRow("Course:", course); form->addRow("Title:", title); form->addRow("Timezone:", zone); form->addRow("Repeated times:", ambiguity); layout->addLayout(form);
    auto* grid = new QGridLayout;
    grid->addWidget(label("Weekday", &dialog), 0, 0); grid->addWidget(label("Starts", &dialog), 0, 1); grid->addWidget(label("Ends", &dialog), 0, 2); grid->addWidget(label("Type", &dialog), 0, 3);
    QList<QCheckBox*> days; QList<QTimeEdit*> starts, ends; QList<QComboBox*> kinds;
    for (int day = 1; day <= 7; ++day) {
        auto* enabled = new QCheckBox(weekdays[day - 1], &dialog); enabled->setObjectName(QString("classDay%1").arg(day)); enabled->setChecked(!id.isEmpty() && previous.weekday == day); days.append(enabled);
        auto* start = timeField(previous.start, QString("classStart%1").arg(day), &dialog); starts.append(start);
        auto* end = timeField(previous.end, QString("classEnd%1").arg(day), &dialog); ends.append(end);
        auto* kind = new QComboBox(&dialog); kind->setObjectName(QString("classKind%1").arg(day)); kind->addItem("Lecture", "lecture"); kind->addItem("Lab", "lab"); kind->setCurrentIndex(kind->findData(previous.kind)); kinds.append(kind);
        for (QWidget* widget : {static_cast<QWidget*>(start), static_cast<QWidget*>(end), static_cast<QWidget*>(kind)}) {
            widget->setEnabled(enabled->isChecked()); connect(enabled, &QCheckBox::toggled, widget, &QWidget::setEnabled);
        }
        grid->addWidget(enabled, day, 0); grid->addWidget(start, day, 1); grid->addWidget(end, day, 2); grid->addWidget(kind, day, 3);
    }
    layout->addLayout(grid);
    layout->addWidget(label("New classes can use several weekdays with separate times/types. Editing changes one slot. Teaching dates and exam periods control recurrence; saved events are independent. Clock-change gaps are flagged without shifting the class.", &dialog));
    auto* problem = label({}, &dialog); problem->setObjectName("calendarFormError"); layout->addWidget(problem);
    QList<WeeklyClass> result; auto* buttons = dialogButtons(dialog, layout);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            result.clear(); (void)calendar::timeZone(zone->currentText().trimmed());
            for (int day = 1; day <= 7; ++day) if (days[day - 1]->isChecked()) {
                WeeklyClass slot = previous; slot.semesterId = semester->currentData().toString(); slot.courseId = course->currentData().toString(); slot.weekday = day;
                slot.kind = kinds[day - 1]->currentData().toString(); slot.title = title->text().trimmed();
                if (slot.title.isEmpty() && !slot.courseId.isEmpty()) slot.title = course->currentText() + " · " + eventKindTitle(slot.kind);
                if (slot.title.isEmpty()) throw Error("Enter a title or choose a course.");
                slot.start = starts[day - 1]->time(); slot.end = ends[day - 1]->time(); slot.timeZone = zone->currentText().trimmed(); slot.ambiguity = ambiguity->currentData().toString();
                if (slot.end <= slot.start) throw Error("Every class must end after it starts.");
                result.append(slot);
            }
            if (result.isEmpty() || (!id.isEmpty() && result.size() != 1)) throw Error(id.isEmpty() ? "Choose at least one weekday." : "Choose exactly one weekday when editing a slot.");
            dialog.accept();
        } catch (const Error& e) { problem->setText(QString::fromUtf8(e.what())); }
    });
    if (dialog.exec() == QDialog::Accepted) controller_->saveWeeklyClasses(result);
}

void CalendarPage::editRange(const QString& id)
{
    if (controller_->busy()) return;
    ExamRange range; range.semesterId = semesterId(); range.start = anchor_->date(); range.end = anchor_->date();
    if (!id.isEmpty()) {
        bool found = false; for (const auto& item : snapshot_.examRanges) if (item.id == id) { range = item; found = true; }
        if (!found) return;
    }
    const Semester* semester = nullptr; for (const auto& item : snapshot_.semesters) if (item.id == range.semesterId) semester = &item;
    if (!semester) return;
    QDialog dialog(this); dialog.setObjectName("examRangeDialog"); dialog.setWindowTitle(id.isEmpty() ? "Add exam period" : "Edit exam period");
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    auto* start = dateField(range.start, "rangeStart", &dialog); auto* end = dateField(range.end, "rangeEnd", &dialog);
    start->setDateRange(semester->start, semester->end); end->setDateRange(semester->start, semester->end);
    form->addRow("Inclusive start:", start); form->addRow("Inclusive end:", end); layout->addLayout(form);
    layout->addWidget(label("Up to three non-overlapping periods, inside " + semester->name + ". Dates are inclusive. Adjacent periods are allowed. Classes resume after the first and stop from the second onward; deleting a period recalculates that order.", &dialog));
    auto* problem = label({}, &dialog); problem->setObjectName("calendarFormError"); layout->addWidget(problem); auto* buttons = dialogButtons(dialog, layout);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        range.start = start->date(); range.end = end->date();
        if (range.end < range.start) { problem->setText("End date must not precede start date."); return; }
        int count = 0;
        for (const auto& previous : snapshot_.examRanges) if (previous.semesterId == range.semesterId && previous.id != range.id) {
            ++count;
            if (range.start <= previous.end && range.end >= previous.start) { problem->setText("This period overlaps an existing inclusive range."); return; }
        }
        if (count >= 3) { problem->setText("This semester already has three exam periods."); return; }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) controller_->saveExamRange(range);
}

void CalendarPage::editSemester()
{
    Semester semester; for (const auto& item : snapshot_.semesters) if (item.id == semesterId()) semester = item;
    if (semester.id.isEmpty() || controller_->busy()) return;
    QDialog dialog(this); dialog.setObjectName("calendarSemesterDialog"); dialog.setWindowTitle("Semester and teaching dates");
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    auto* name = new QLineEdit(semester.name, &dialog); name->setObjectName("calendarSemesterName");
    auto* season = new QComboBox(&dialog); season->setObjectName("calendarSeason"); season->addItem("Winter", "winter"); season->addItem("Summer", "summer"); season->setCurrentIndex(season->findData(semester.season));
    auto* start = dateField(semester.start, "semesterStart", &dialog); auto* end = dateField(semester.end, "semesterEnd", &dialog);
    auto* custom = new QCheckBox("Custom teaching dates", &dialog); custom->setObjectName("customTeaching"); custom->setChecked(semester.teachingStart.isValid());
    auto* teachingStart = dateField(semester.teachingStart.isValid() ? semester.teachingStart : semester.start, "teachingStart", &dialog);
    auto* teachingEnd = dateField(semester.teachingEnd.isValid() ? semester.teachingEnd : semester.end, "teachingEnd", &dialog);
    for (auto* field : {teachingStart, teachingEnd}) { field->setEnabled(custom->isChecked()); connect(custom, &QCheckBox::toggled, field, &QWidget::setEnabled); }
    form->addRow("Name:", name); form->addRow("Season:", season); form->addRow("Semester starts:", start); form->addRow("Semester ends:", end);
    form->addRow(custom); form->addRow("Teaching starts:", teachingStart); form->addRow("Teaching ends:", teachingEnd); layout->addLayout(form);
    layout->addWidget(label("Default winter teaching is October 1–December 31; summer is February 18–May 31, clipped to semester dates. The explicit season controls this, not the name. Exam periods must remain inside the semester. Custom teaching dates do not move or remove saved events.", &dialog));
    auto* problem = label({}, &dialog); problem->setObjectName("calendarFormError"); layout->addWidget(problem); auto* buttons = dialogButtons(dialog, layout);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name->text().trimmed().isEmpty() || end->date() < start->date()) { problem->setText("Enter a name and valid semester date range."); return; }
        for (const auto& range : snapshot_.examRanges) if (range.semesterId == semester.id && (range.start < start->date() || range.end > end->date())) { problem->setText("Semester dates must include every existing exam period."); return; }
        if (custom->isChecked() && (teachingStart->date() < start->date() || teachingEnd->date() > end->date() || teachingEnd->date() < teachingStart->date())) { problem->setText("Custom teaching dates must be inside the semester."); return; }
        semester.name = name->text(); semester.season = season->currentData().toString(); semester.start = start->date(); semester.end = end->date();
        semester.teachingStart = custom->isChecked() ? teachingStart->date() : QDate{}; semester.teachingEnd = custom->isChecked() ? teachingEnd->date() : QDate{}; dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) controller_->editSemester(semester);
}

void CalendarPage::editTimeZone()
{
    QDialog dialog(this); dialog.setObjectName("calendarTimezoneDialog"); dialog.setWindowTitle("Calendar timezone");
    auto* layout = new QVBoxLayout(&dialog); auto* zone = zoneField(snapshot_.calendarTimeZone, &dialog); layout->addWidget(zone);
    layout->addWidget(label("Changes calendar display and the default for new events/classes. Existing timed instants and each weekly class's saved wall time/timezone remain unchanged. All-day dates stay fixed.", &dialog));
    auto* problem = label({}, &dialog); problem->setObjectName("calendarFormError"); layout->addWidget(problem); auto* buttons = dialogButtons(dialog, layout);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { try { (void)calendar::timeZone(zone->currentText().trimmed()); dialog.accept(); } catch (const Error& e) { problem->setText(QString::fromUtf8(e.what())); } });
    if (dialog.exec() == QDialog::Accepted) controller_->setCalendarTimeZone(zone->currentText().trimmed());
}

void CalendarPage::removeSelected(const QString& type)
{
    const QString id = type == "event" ? selectedEvent_ : type == "class" ? selectedClass_ : selectedRange_;
    if (id.isEmpty() || controller_->busy()) return;
    const QString question = type == "event" ? "Delete this saved event?"
        : type == "class" ? "Delete this weekly class slot? Separately saved events will stay."
        : "Delete this exam period? Recurring classes will be recalculated from the remaining periods. Saved events will stay.";
    if (QMessageBox::question(this, "Delete calendar record", question) != QMessageBox::Yes) return;
    if (type == "event") controller_->removeCalendarEvent(id);
    else if (type == "class") controller_->removeWeeklyClass(id);
    else controller_->removeExamRange(id);
}
} // namespace virgil
