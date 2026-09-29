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
