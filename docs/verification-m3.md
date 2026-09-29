# Milestone 3 verification — 2026-09-27

Historical checkpoint. Current results: [milestone 4 verification](verification-m4.md).

## Build and automated checks

Used Fedora 43, GCC 15.2.1, CMake 3.31.11, Ninja 1.13.1 and Qt 6.10.3.
No packages or dependencies were added. Tests used disposable libraries and
preferences. No real study library, credentials, old web project, protected Git
metadata or publication was touched. Builds remain outside the source tree.

```sh
cmake --build /tmp/virgil-build-debug --parallel 2
ctest --test-dir /tmp/virgil-build-debug --output-on-failure

cmake -S . -B /tmp/virgil-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVIRGIL_SANITIZERS=ON
cmake --build /tmp/virgil-build-asan --parallel 2
ctest --test-dir /tmp/virgil-build-asan --output-on-failure
```

**Passed: 2/2 Debug suites (40.58 seconds) and 2/2 sanitizer suites (48.06 seconds)**,
including AddressSanitizer, UndefinedBehaviorSanitizer and default leak checking.
Sanitizer CTest ran outside the sandbox with approved permissions so LeakSanitizer
could inspect processes; no checks were disabled. Final builds have no compiler
warnings. Expected offscreen `propagateSizeHints()` warnings remain.

After those full runs, snapshot publication was tightened to reject preexisting
empty staging files and symlinks, and backup rotation was restricted to recognized
application-owned filenames. Both configurations were rebuilt and their complete
library suites rerun:

```sh
ctest --test-dir /tmp/virgil-build-debug --output-on-failure -R '^library$'
ctest --test-dir /tmp/virgil-build-asan --output-on-failure -R '^library$'
```

**Passed: 1/1 each (1.74 and 5.32 seconds)**. Final coverage is **43 behavior
tests: 32 library and 11 widget/process cases**, excluding Qt Test setup/cleanup.
The final core-only changes did not require repeating the unchanged widget suite.

| Milestone 3 behavior | Evidence/result |
| --- | --- |
| Delete course after its folder disappears | Service and visible-button tests remove the course record; material IDs, attachment references and resource notes remain, detached from the course |
| Semester/course editing | Forms and service tests preserve UUIDs and physical directory paths; course reassignment, code, name, color, season and dates persist |
| Material metadata | Display title, category and exam date survive scans, file replacement, identifiable rename and reopen |
| Books, lectures and bookmarks | Local attachment references, lecture dates/plain-text notes, bookmark URL/folder/category/date persist; record deletion keeps attachment bytes |
| Explicit relinking | Existing resource attachment follows the retained material UUID when the discovered record is joined |
| Metadata search and catalog UI | Actual forms, selected-course/global search and offline catalog selection pass; catalog choice fills the manual course form |
| Catalog provenance | Embedded snapshot contains 157 unique official codes; two contradictory rows are excluded; see [catalog-source.md](catalog-source.md) |
| Metadata backups | Seven verified snapshots rotate; unknown backup names remain untouched; blocked backup storage reports failure without undoing saved notes |
| Snapshot publication | Existing destinations, empty stages and symlinks are rejected without modifying their bytes |
| Schema upgrades | Reconstructed v1 and v2 fixtures upgrade in order to v3, preserving identity and verified pre-migration snapshots |
| Earlier safety and integration behavior | Full suites retain import, watcher/audit, cancellation, missing-root, collision, locking, relocation, trash/restore and interrupted-operation coverage |

## UI inspection and limits

The resource form test was also run with the offscreen platform and
`VIRGIL_M3_SCREENSHOT=/tmp/virgil-m3.png` plus
`VIRGIL_M3_FORM_SCREENSHOT=/tmp/virgil-m3-form.png`. Both captured images were
visually inspected after adjusting the resource table columns. The window shows
the prominent Delete course action and relevant resource fields; the lecture form
shows its date, attachment chooser and notes editor.

These are **headless checks**. File/HTTPS opening was captured through Qt URL
handlers; no real external reader or browser was launched. Native Wayland,
display scaling, a disconnected namespace, Release installation, Qt 6.8 runtime,
Windows and macOS were not retested for this milestone. Historical desktop/offline
checks are in [verification.md](verification.md).

Backups contain SQLite metadata, including notes and relationships, **not material
file bytes**. Only the latest seven managed automatic snapshots rotate;
pre-migration snapshots remain. There is no in-app restore yet. Crash-state tests
do not prove sudden-power-loss durability or protection against hostile path races.
Linux tmpfs remains the tested material filesystem; milestone 2 platform and
filesystem limits still apply. Search covers filenames and saved metadata, not
document contents. The catalog is a dated reference snapshot, not a prerequisite
validator or runtime updater.

## Short manual desktop checklist

1. Restart `/tmp/virgil-build-debug/virgil-desktop` with a disposable library.
   Select a course, remove its directory externally, then click Delete course.
   Confirm its organization disappears while its detached notes remain accessible.
2. Edit a semester and course, and reassign the course to another semester.
   Confirm labels change while UUIDs and existing directory paths remain stable.
3. Add a book, lecture with notes/attachment, and bookmark. Edit and reopen them;
   delete a lecture record and verify its attachment remains an ordinary file.
4. Edit a material title/category/exam date. Search within a course and across
   courses. Select an offline catalog row when creating a course.
5. Open a local attachment and a bookmark using the actual desktop default apps.
   Inspect Settings backup status and test keyboard navigation/display scaling.

Milestone 3 is the current checkpoint. Milestone 4 has not been started.

## Follow-up: browse files for books and lectures

Book and lecture forms now provide **Browse files…**, opening an Existing File
chooser with an All files filter and no library-directory restriction. Selection
is staged until Save. Originals remain; external files import into Books/Lectures
(or the library root for unassigned resources). Existing library files keep their
paths and material IDs. The existing material list remains available, and choosing
No attachment discards the pending selection.

Added service coverage for invalid forms before import, preserved source/copy
bytes, collision rejection, cancellation, existing-file reuse, editing and reopen.
A headless widget test drives the real nested chooser for both forms, navigates
outside the library, saves attachments and checks that cancelling the form imports
nothing. Final coverage is **45 behavior tests: 33 library and 12 widget/process
cases**. Native desktop/portal browsing has not been tested in this session.

Validation used the same build and CTest commands above, with sanitizer CTest
outside the sandbox and all sanitizer/leak checks enabled.
**Final results: 2/2 Debug suites passed (40.66 seconds), and 2/2 sanitizer suites
passed (49.53 seconds)**. Both final configurations built without compiler warnings.
