# Milestone 4 verification — 2026-09-27

## Scope and reference behavior

Implemented the calendar milestone from [roadmap.md](roadmap.md). Read only the
web app's `src/app/calendar-view.tsx`, `src/lib/calendar.ts`, `src/lib/time.ts`,
`src/lib/course-colors.ts` and `tests/calendar.test.ts` as behavior references.
No old-web files, credentials, accounts or runtime were modified or used.

The native implementation carries over Monday-first month/week views, semester
selection, course colors, five event types, optional course associations,
assignment completion, upcoming saved events, multi-weekday lecture/lab scheduling,
class editing/deletion, semester dates and three exam periods. It also enforces the
roadmap's non-overlap and explicit daylight-saving requirements, uses the saved
season instead of inferring it from a label, supports custom teaching dates and
exam-period editing, and allows Sunday in new schedules. It does not add later
milestones or copy the web app's hosted storage/runtime.

## Build and automated checks

Used Fedora 43, GCC 15.2.1, CMake 3.31.11, Ninja 1.13.1 and Qt 6.10.3. No packages
or dependencies were added. All libraries and preferences were disposable;
no real study library or protected Git metadata was changed. Builds stay outside
the source tree.

```sh
cmake -S . -B /tmp/virgil-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/virgil-build-debug --parallel 2
ctest --test-dir /tmp/virgil-build-debug --output-on-failure

cmake -S . -B /tmp/virgil-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVIRGIL_SANITIZERS=ON
cmake --build /tmp/virgil-build-asan --parallel 2
ctest --test-dir /tmp/virgil-build-asan --output-on-failure
```

**Passed: 3/3 Debug suites (43.77 seconds) and 3/3 sanitizer suites (57.38
seconds)**. AddressSanitizer, UndefinedBehaviorSanitizer and default leak checks
remained enabled. Sanitizer CTest ran outside the sandbox with approved permissions
so LeakSanitizer could inspect processes. Builds have no compiler warnings.
Expected offscreen `propagateSizeHints()` warnings remain.

After final calendar layout/compact-label changes, both configurations rebuilt
without warnings. The two affected Debug calendar widget functions passed (2.231
seconds), and the complete sanitizer widget suite passed again (1/1, 46.84
seconds). The library/calendar service code was unchanged after its passing full
runs. The final widget command was:

```sh
ctest --test-dir /tmp/virgil-build-asan --output-on-failure -R '^widgets$'
```


Coverage is **57 behavior test functions: 33 library, 10 calendar and 14
widget/process functions**. The recurrence function has 13 data rows, giving
**69 executed behavior cases**, excluding Qt Test setup/cleanup.

| Calendar behavior | Evidence/result |
| --- | --- |
| Month/week and Monday-first navigation | Headless widget test checks all seven headers, six-row November grid, cross-month leading dates and day-to-week activation |
| Event creation/editing/deletion | Service coverage for all five types; actual assignment/timed-exam forms, stable ID after edit and visible deletion controls pass |
| Assignment completion | Complete/incomplete persistence; only assignments accepted; completed events stay visible and leave upcoming results |
| Optional associations and filters | Independent events remain visible under another semester; course reassignment updates calendar semester associations without moving explicit dates/times |
| Weekly timetable | Multiple weekdays with separate lecture/lab types; actual slot edit/delete; invalid batch commits no slots; wall times, zones and IDs survive reopen |
| Three inclusive exam ranges | Shared endpoint overlap rejected, next-day adjacency allowed, fourth range refused, semester bounds and shrinking edits checked; actual period edit/delete pass |
| Academic recurrence | Sorted first-period pause/resume, second-period stop, third never restarts; boundary data rows, December/January, leap February, semester clipping and custom teaching dates pass |
| Saved events during recurrence suppression | Explicit exams remain visible during exam periods and outside default teaching windows; changing teaching policy affects only weekly classes |
| Clock-change gaps/repeats | Skopje spring gap rejected for every choice; autumn repeat requires explicit choice with exact early/late UTC expectations; actual editor reports errors without closing, saves later instant and retains it on edit |
| Other transition sizes and display zones | Lord Howe half-hour gap/repeat checks; weekly UTC/date projection across the date line; multiday timed events and exclusive midnight endpoint; all-day date stability |
| Weekly clock-change review | Invalid/unresolved occurrences appear with warnings; chosen repeat produces the expected UTC instant rather than guessing |
| Timezone and upcoming ordering | Persisted display setting; changing it does not move event instants; chronological all-day/timed ordering, filtering and completion exclusion |
| Course colors | Actual course editor checks ten named labels and corresponding unchanged hex values; shared names used by both new/edit forms |
| Schema migration | Reconstructed schema-3 fixture upgrades to 4 with same library/resource IDs and notes; pre-v4 snapshot is verified and opened read-only to check schema/data; older v1/v2 regression fixtures also pass |
| Existing library safety | Full library/widget suites retain import, browse/cancel, watcher/audit, locking, missing root, collision, backup rotation, relocation, relinking, trash and interrupted-operation coverage |

## UI inspection and limits

```sh
QT_QPA_PLATFORM=offscreen \
  VIRGIL_M4_SCREENSHOT=/tmp/virgil-m4.png \
  VIRGIL_M4_MONTH_SCREENSHOT=/tmp/virgil-m4-month.png \
  /tmp/virgil-build-debug/widget_test calendarTimetableExamPeriodsNavigationAndSemesterFilter
```

Both captured views were visually inspected. Month cells use compact titles with
full title/time tooltips; week cells and the agenda show explicit times. These are **headless checks**, not a
native desktop/portal/Wayland session. Calendar cells and the agenda scroll when
needed; the agenda lists every occurrence in the displayed range. No real external
reader/browser, disconnected namespace, Release installation, Qt 6.8 runtime,
Windows or macOS testing was performed for this milestone. Historical native
checks remain in [verification.md](verification.md).

Metadata backups include events, class rules, exam periods and calendar settings,
not material bytes. There is no in-app backup restore. Clock rules use installed
Qt/system timezone data; unknown zones are reported, not silently substituted when
resolving local times. Power-loss and hostile path-race durability remain unproven.
Calendar rendering derives occurrences without inserting/deleting event copies.
There is no reminder daemon, account sync or calendar import/export in this scope.

## Short native desktop checklist

1. Restart `/tmp/virgil-build-debug/virgil-desktop` with a disposable library.
   Open Calendar, use Month/Week, Previous/Next/Today, day activation and semester
   filters; check keyboard navigation and your display scaling.
2. Create an all-day assignment, mark it complete/incomplete, and edit/delete an
   exam, review session, lecture and lab. Try independent and course-associated
   events, including times that span midnight.
3. Add several weekly class days, edit one and delete another. Add/edit/delete
   three exam periods and confirm inclusive pause/resume/stop behavior. Verify
   a separately saved event remains visible during each period.
4. Edit semester and custom teaching dates, then restore season defaults. Change
   display timezone and verify explicit event instants remain stable. Try a
   Skopje clock gap/repeat and the explicit earlier/later selectors.
5. Reopen the library and verify notes, completion and calendar records. Inspect
   named course colors, the verified migration snapshot and metadata backup status.

Milestone 4 is the current checkpoint; milestone 5 has not been started.

## Weekly auditory exercises (2026-10-10)

Added the third weekly class type, Auditory Exercises, with an AUDITORY label
and black top-right triangle in Month and Week entries. Schema 5 upgrades weekly
class constraints transactionally and keeps a verified pre-v5 metadata snapshot.
This is a calendar extension, not the start of roadmap milestone 5.

Commands and results:

- `cmake -S . -B /tmp/virgil-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`
  and `cmake --build /tmp/virgil-build-debug --parallel 2`: passed.
- `ctest --test-dir /tmp/virgil-build-debug --output-on-failure -R '^(calendar|library)$'`:
  both suites passed. Includes preservation of a schema-4 class, upgrade backup,
  auditory edit/reopen/projection/delete, and rejection of invalid class types.
- `ctest --test-dir /tmp/virgil-build-debug --output-on-failure -R '^widgets$'`:
  timed out after 90 seconds in `firstLaunchThroughFormsAndRestart`, before
  calendar checks ran.
- `QT_QPA_PLATFORM=offscreen timeout 45s /tmp/virgil-build-debug/widget_test calendarTimetableExamPeriodsNavigationAndSemesterFilter`:
  passed (3 Qt Test results including setup/cleanup). Covers choosing auditory
  exercises in the form, label/tooltip text and black triangle rendering in both
  calendar views, alongside existing recurrence and navigation checks.

These are headless checks with Qt 6.10.3. Native desktop appearance has not been
manually tested, and the installed application was not updated.

## Weekly class rooms (2026-10-10)

Each weekday slot now has an optional room field. Month and Week entries render
it in smaller text at the top left, above the title; the auditory triangle stays
at the top right. Long room names are elided visually and retained in tooltips
and accessible text. Month rows have a minimum height that fits the room/title.
Schema 6 adds a non-null room column with an empty default, after a verified
pre-v6 metadata backup. Existing classes keep their IDs and type.

Commands and results:

- `cmake -S /home/AbbeFaria/virgil-inferno -B /tmp/virgil-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/home/AbbeFaria/.local`
  and `cmake --build /tmp/virgil-build-debug --parallel 2`: passed. The generated
  desktop launcher points to `/home/AbbeFaria/.local/bin/virgil-desktop`.
- `ctest --test-dir /tmp/virgil-build-debug --output-on-failure -R '^(calendar|library)$'`:
  both suites passed. Covers schema-5 upgrades preserving auditory records, empty
  room defaults, backup creation, separate weekday rooms, Unicode, trimming,
  editing, clearing, reopening, projection and rejection of multiline rooms.
- `QT_QPA_PLATFORM=offscreen VIRGIL_M4_SCREENSHOT=/tmp/virgil-rooms-week.png VIRGIL_M4_MONTH_SCREENSHOT=/tmp/virgil-rooms-month.png timeout 45s /tmp/virgil-build-debug/widget_test calendarTimetableExamPeriodsNavigationAndSemesterFilter`:
  passed, including after the minimum-height adjustment. Covers room form
  creation/editing, tooltip/accessibility text, and top-left room/top-right
  triangle painting in both views. Headless screenshots were visually inspected.

Qt 6.10.3; no native desktop testing. The full widget suite was not repeated
because of the first-launch timeout recorded above. The installed executable
has not been replaced. The prepared build can now be installed using
`cmake --install /tmp/virgil-build-debug` from any directory.

## Calendar space and time layout (2026-10-10)

Removed the shared Library banner and calendar policy banner. Month and Week
show Monday–Saturday. New weekly forms offer those six weekdays; existing Sunday
records stay in storage and remain accessible through agenda/timetable editing.
Week view now uses a shared 08:00–21:00 scale with proportional start positions
and durations, and separate columns for overlapping entries. Back-to-back entries
reuse a column. All-day/review/out-of-hours entries remain above the time grid.
The room stays at the top left; auditory markers are gray (#808080). Generated
Lab/Auditory Exercises suffixes are normalized for display so the bracketed label
appears once. Saved titles and metadata are preserved.

Validation on Qt 6.10.3:

- `cmake --build /tmp/virgil-build-debug --parallel 2`: passed.
- `ctest --test-dir /tmp/virgil-build-debug --output-on-failure -R '^(calendar|library)$'`:
  both suites passed (2/2).
- `QT_QPA_PLATFORM=offscreen VIRGIL_WEEK_LAYOUT_SCREENSHOT=/tmp/virgil-week-layout.png timeout 45s /tmp/virgil-build-debug/widget_test calendarWeekAlignsTimesAndSharesOverlaps calendarTimetableExamPeriodsNavigationAndSemesterFilter calendarEventFormsHandleClockChangesCompletionAndColorNames`:
  passed (5 Qt Test results including setup/cleanup). Checks cross-day alignment,
  empty time gaps, fractional starts, 20:00–21:00 placement, overlapping/nested
  intervals, reused columns, clipping, all-day/out-of-hours access, gray marker,
  labels, selection and double-click editing, existing forms and recurrence.
- Headless Week and Month screenshots were inspected; Month capture used
  `VIRGIL_M4_MONTH_SCREENSHOT=/tmp/virgil-six-day-month.png` with the same targeted
  tests before the final Week badge typography adjustment.

Native desktop testing was not performed. The full widget suite's earlier
first-launch timeout remains uninvestigated; that suite was not repeated here.
The installed executable is unchanged. Close Virgil and run
`cmake --install /tmp/virgil-build-debug` to install this prepared build.
