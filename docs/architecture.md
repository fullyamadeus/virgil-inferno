# Architecture and learning guide

## Start reading here

1. `src/main.cpp`: creates `QApplication`, sets the application identity, creates
   the window and enters the event loop.
2. `src/domain/records.h`: simple value types for a semester, course, material
   and a snapshot of the library. These structs contain data, not widgets or SQL.
3. `src/ui/mainwindow.cpp`, `src/ui/resources.cpp` and `src/ui/calendarpage.cpp`: layouts, forms and actions. A button asks the
   controller to perform an operation. It does not execute SQL or copy files.
4. `src/services/librarycontroller.cpp`: sends work to one background thread
   and delivers results to the GUI thread.
5. `src/services/library.cpp`: library creation/opening, scans, imports and
   explicit creation of course directories, resource edits and metadata backups.
6. `src/storage/database.cpp`: schema initialization, prepared statements,
   transactions and record loading. `src/services/paths.cpp` enforces path policy.
7. `tests/library_test.cpp`, then `tests/widget_test.cpp`: executable examples
   of important behavior, always using temporary libraries.

## The first C++ build

A **header** (`.h`) declares types and operations other files may use. `#pragma
once` prevents repeated inclusion in one compilation. A **source file** (`.cpp`)
defines their implementations. Each source plus its included headers forms a
**translation unit**, compiled independently to an object file. The **linker**
combines those objects and Qt libraries into `virgil-desktop`.

CMake describes targets: `virgil_core`, `virgil_ui`, the application and tests.
Target dependencies carry include paths and library requirements. Ninja reads
the generated build graph and rebuilds only affected objects. CMake is not the
compiler; GCC is the compiler in this verified environment.

The pipeline is configure → compile/link → CTest → install. A Debug build keeps
debugger information and avoids release optimization. Release is suitable for
normal use. The optional sanitizer build checks memory access and undefined
behavior. Packaging into an RPM will follow a later milestone.

`CMAKE_AUTOMOC` asks Qt's **meta-object compiler** to generate support for classes
with `Q_OBJECT`. That generated C++ implements signal/slot metadata and is compiled
with our own source. Generated files stay in the build directory.

## A concrete import flow

```text
GUI thread                            Library worker thread
MainWindow::importMaterial()
    file picker and destination form
LibraryController::importFile()
    queue a callable ----------------> Library::importFile()
                                          check paths and collision
                                          save pending-import journal
                                          copy chunks into .virgil/staging
    progress <------------------------     check cancellation/source stability
                                          finalize without replacing a file
                                          save material UUID/path in SQLite
    snapshot <------------------------     remove staging link
MainWindow::applySnapshot()
    update course tree and file table
```

`QApplication::exec()` runs the **event loop**: it waits for input, painting,
timers and queued work results. A **signal** announces an event, such as an
action being triggered or a snapshot becoming ready. A connected **slot** or
lambda handles it. Queued calls across threads return results without touching
widgets on the worker thread.

Each Qt SQL connection must be created, used and destroyed on its owning thread.
The worker owns the `Library`, which owns its `Database`. The GUI receives plain
value snapshots instead of database handles. Only one job runs at a time, so
library switches cannot race an import. Another process cannot write the same
library because it must acquire the same `QLockFile`.

## Ownership and lifetime

Most widget allocations look like `new QLabel(text, parent)`. Qt's parent-child
tree owns these objects; deleting the parent deletes its children. Widget member
pointers are references to those owned objects, not separate owners. Do not wrap
one of these pointers in an owning `std::unique_ptr` as well.

The worker's `std::unique_ptr<Library>` means exactly one C++ owner manages the
library. When that pointer is reset or destroyed, the library destructor runs.
Its database connection closes before its lock is released because members are
destroyed in reverse declaration order. This is **RAII**: resource ownership is
tied to object lifetime instead of scattered cleanup calls.

`Transaction` is another RAII example. It starts a transaction when constructed,
commits only when explicitly asked, and rolls back on an exception. SQLite's
atomicity covers metadata only; it cannot roll back an ordinary filesystem write.
The import journal bridges that gap conservatively.

`const` marks operations or values that should not change through that access.
Snapshots are copied by value when crossing threads. Qt containers share their
underlying data until changed, making this convenient without sharing writable
widget state. The cancellation flag is an atomic boolean so the GUI can request
cancellation while the worker is busy.

Closing during work requests cancellation and waits for orderly completion.
Thread termination is never used. A blocked filesystem call must return before
cooperative cancellation can take effect.

## Models and views

`QTableView` displays material rows. `QStandardItemModel` holds the presentation
data, including a hidden UUID role used to open the selected material. Sorting
changes the view's order without changing file names or database identity.
This model is a modest starting point; paging or a dedicated table model can
replace it if measured library size makes full snapshots expensive.

The course tree displays every semester. Colors appear as swatches alongside
native theme text, so yellow courses do not force low-contrast yellow labels.
There is no custom theme or web rendering engine.

## What lives where

```text
Virgil Library/
  Winter 2026/
    Mathematics/
      Lectures/
      Books/
      Past Exams/
      Other Materials/notes.txt
  .virgil/
    library.sqlite       structured metadata, schema version 4
    library.id           same UUID as the database; detects the wrong mount/root
    library.lock         present while a process owns the library
    staging/             hidden import working files; never listed as materials
    trash/<UUID>         recoverable moved files/folders
    backups/automatic-<UTC>-<UUID>.sqlite  latest seven metadata-only snapshots
    library.sqlite.before-v2-<UUID>.sqlite  verified schema 1 snapshot
    library.sqlite.before-v3-<UUID>.sqlite  verified schema 2 snapshot
    library.sqlite.before-v4-<UUID>.sqlite  verified schema 3 snapshot
```

The filesystem is authoritative for file bytes and locations. SQLite is
authoritative for identifiers, semester/course organization and display titles.
It also owns lecture notes and book/bookmark records; it owns personal events and weekly class schedules;
later it will own questions and progress. These cannot all be recreated by rescanning PDFs. A future text-search cache is rebuildable.
UI preferences, geometry and the last library path are separate `QSettings` data.

On open and manual Refresh, a worker enumerates paths and file information,
without reading document contents. GUI-thread `QFileSystemWatcher` signals queue
750 ms batches; the worker scans the affected subtrees. A 30-second full audit
covers missing events or watch limits. Watchers only request work; the library,
SQL connection and mutations still belong to the single worker. Complete scans
update changed rows transactionally. Incomplete/cancelled scans keep saved data.
Files outside an incremental scope keep their saved state until an event/audit.

Linux `statx` supplies device/inode/birth time. A unique old/new identity match at
an unoccupied path preserves the UUID/title; hard-link ambiguity never merges
records. Matching folders also relocates saved descendant/course/semester paths.
Path-based editor replacements retain their record. Unsupported filesystems leave
identity empty and require guided relinking. Relinking explicitly chooses a missing
record and an available replacement; no file bytes change. Folder/course display
names remain metadata, independent of renamed directory names.

## Milestone 2: transactions across two resources

`Library::movePath` saves an operation UUID, source, destination, object identity
and kind before calling Linux `renameat2(RENAME_NOREPLACE)`. Only then does one SQL
transaction change stored paths and remove the journal. Trash uses this same flow,
with a UUID destination under `.virgil/trash`; restoration refuses collisions.
Course removal changes metadata only and keeps all ordinary files.

On open, recovery compares the journal against actual identities. A proven completed
move finishes metadata; a proven unstarted move clears only its journal. Ambiguous
outcomes keep both files and the journal. Further mutations and scans pause until
review resolves that move, preventing speculative identities from accumulating.
The recovery UI offers inspect, review/retry and abandon a proven unstarted move.
Staged import copies remain for manual inspection when completion is uncertain.

`QHash` maps identities/paths to records, avoiding repeated all-record searches in
normal file reconciliation. `QSet` records the paths actually discovered. These
containers hold values, never widgets or SQL connections. A defaulted C++20
`operator==` compares material/folder values for audit detection; it does not decide
whether two different files should be merged.

Schema 2 adds file identities, retained folder identities, move operations and trash
records. A v1 upgrade first uses SQLite `VACUUM INTO` for a consistent metadata
snapshot and verifies it with a separate read-only connection. Schema changes run
in an RAII transaction. This is an upgrade safeguard, not an automatic full backup.

## Milestone 3: records, references and snapshots

`Resource` is a plain C++ value with a finite `kind`: book, lecture or bookmark.
`Library::saveResource` checks required dates, local material IDs and URL policy,
then performs a prepared SQL upsert. The GUI gets a copied `Snapshot`; resource
forms and search never receive database handles. `QPlainTextEdit` stores lecture
notes as plain text, with no HTML interpretation or network request.

A book/lecture attachment stores a material UUID instead of duplicating its path.
Confident moves update that material path; resource references keep working.
Guided relinking transfers resource references before merging the explicitly
chosen discovered record. Foreign keys with `ON DELETE SET NULL` detach resources
when a course is deleted. A missing course folder therefore does not prevent
deleting its course record, and ordinary files plus resource notes remain saved.
Deleting a resource record never deletes its material.

Book/lecture forms also offer a local `QFileDialog` with an All files filter. The
chosen path stays in the form until Save. `saveResourceWithFile` validates the
record first, then reuses the journaled, cancellable worker import and attaches
the saved material UUID. Existing library files keep their paths. If the file
import succeeds but saving the record fails, the error explains that its file
remains in Materials; filesystem and SQL changes are not one atomic transaction.

Organization edits change names/colors/dates/semester membership without changing
UUIDs or deriving folder paths from display names. Filename and display title are
separate data. Scans update only filesystem information, preserving user categories
and exam dates. Search is a case-insensitive presentation filter over filename and
metadata values, with an explicit All courses toggle. It does not read PDF content.

The offline catalog is factual JSON in `assets/finki-seis23.json`, bundled by Qt's
resource compiler through `assets/catalog.qrc`. `courseCatalog()` reads the embedded
resource, so an installed executable does not need a source checkout or internet.
Its official source, verification date and contradictory rows are documented.
The core still has no networking dependency. URLs open only via an explicit UI
action and the desktop's browser.

`Database::createSnapshot` uses SQLite `VACUUM INTO` for a consistent staged copy,
verifies integrity, relationships and identity through a separate read-only SQL
connection, then publishes without replacement. Incomplete `.part` stages remain
identifiable. Existing libraries run ordered migrations with a verified snapshot
before each step. Automatic snapshots run after in-app metadata/import/move changes
and once per UTC day on open/refresh; the latest seven verified app snapshots are
retained. Rotation never deletes materials or pre-migration snapshots. Backup
failures become visible notices and do not undo committed changes. This is still
metadata-only protection; complete coordinated restore belongs to milestone 5.

## Milestone 4: dates, instants and recurrence

`CalendarEvent`, `WeeklyClass` and `ExamRange` are plain values in a `Snapshot`.
`CalendarPage` owns native widgets, emits errors and dispatches controller jobs.
`librarycalendar.cpp` checks associations, times and range limits before writing
SQL on the library worker. Bulk weekday additions validate every slot first and
commit together with an RAII transaction. No calendar rendering writes to storage.

`QDate` represents an all-day civil date, with no timezone conversion. A timed
event stores two UTC `QDateTime` instants, plus its entry timezone. Weekly classes
store an ISO weekday (Monday=1), two `QTime` wall times and an IANA timezone.
They are rules, not a collection of saved event copies. Changing calendar display
timezone therefore preserves saved instants and projects class times into the
chosen view, including dates that differ across zones.

The pure functions in `services/calendar.cpp` calculate week starts, recurrence,
upcoming events and visible occurrences. Default annual teaching windows depend
on `Semester::season`, not its label, and intersect semester dates. An optional
pair of custom teaching dates overrides that window. Chronologically sorted,
inclusive exam ranges pause classes in the first and stop them at the second;
the third never restarts them. This affects only weekly rules. Every separately
saved event remains independently visible. Removing a period recalculates order.
Semester edits cannot leave existing exam ranges outside their bounds.

`localToUtc` uses [Qt transition resolution](https://doc.qt.io/qt-6.10/qdatetime.html#transitionresolution-enum)
to construct candidates on both sides of a clock change
and verifies that each round-trips to the exact requested civil date/time. No
candidate means a nonexistent time; two distinct candidates mean a repeated time.
The caller must select earlier/later for a repeat, or receive an error. Weekly
rules persist that choice and produce a visible warning for an unresolved repeat
or a gap, rather than silently shifting or dropping the occurrence. Editing an
existing ambiguous timed event preserves its chosen instant. Tests also cover a
half-hour transition so the code does not assume all changes are one hour.

Schema 4 adds calendar settings/events/classes/ranges and custom teaching dates.
Existing schemas migrate in order with verified pre-migration snapshots. Automatic
metadata backups include calendar data; they still do not contain material bytes.
Deleting a course detaches calendar records through foreign keys without erasing
them. Reassigning a course updates its associated calendar semester IDs in the same
transaction, leaving record IDs, event dates/times and weekly wall times intact.

## Debugging and extending

Use a disposable library. Put a debugger breakpoint in `Library::importFile` to
follow a whole operation, or run one Qt Test function:

```sh
/tmp/virgil-build-debug/library_test completeWorkflowAndRelocation
QT_QPA_PLATFORM=offscreen /tmp/virgil-build-debug/widget_test firstLaunchThroughFormsAndRestart
```

Exceptions from checked service operations become readable error states in the
window. Diagnostics avoid document contents. Test assertions target data and
behavior instead of incidental widget geometry.

Milestone 2 keeps this separation. Browse `refresh`, `movePath`, `finishOperation`
and `reviewOperation`, then the corresponding tests. Do not add more threads or
a plugin system without a concrete need. Milestone 4 adds calendar value types and pure projection functions;
read `localToUtc`, `recurringClassVisible`, `saveCalendarEvent` and
`calendar_test.cpp` next. Stop before milestone 5.

## Documentation consulted

- [Qt 6.10 SQL connection lifetime and threads](https://doc.qt.io/qt-6.10/qsqldatabase.html)
- [Qt 6.10 worker objects and QThread](https://doc.qt.io/qt-6.10/qthread.html)
- [Qt 6.10 process locking](https://doc.qt.io/qt-6.10/qlockfile.html)
- [Qt 6.10 safe whole-file writes](https://doc.qt.io/qt-6.10/qsavefile.html)
- [Qt 6.10 default application opening](https://doc.qt.io/qt-6.10/qdesktopservices.html)
- [Qt Test](https://doc.qt.io/qt-6.10/qtest.html)
- [Qt date/time transitions](https://doc.qt.io/qt-6.10/qdatetime.html#transitionresolution-enum)
- [Qt timezones](https://doc.qt.io/qt-6.10/qtimezone.html)

The installed version is 6.10.3. The declared 6.8 baseline avoids APIs introduced
after 6.8, but a separate 6.8 build has not been performed.

## Desktop integration

`assets/application.qrc` embeds the PNG icon in the executable. Qt's resource
compiler makes `:/virgil/virgil-logo.png` readable without the source folder.
`main.cpp` sets a themed window icon with this embedded fallback, and a desktop
file identity matching `virgil-desktop.desktop`, so KDE can associate the running
Wayland window with its launcher. The existing QSettings identity is unchanged.
CMake installs the executable, desktop entry and SVG/PNG icons. The desktop entry
records the configured absolute install path, so GUI launch does not rely on PATH.
