# Milestone 2 verification — 2026-09-27

Historical checkpoint. Current results: [milestone 3 verification](verification-m3.md).

## Build and automated checks

Used Fedora 43, GCC 15.2.1, CMake 3.31.11, Ninja 1.13.1 and Qt 6.10.3.
No packages/dependencies were added. All libraries/preferences were disposable;
no real study library, credentials, old web project, Git metadata or publication
was touched. Build directories remain outside the source tree.

```sh
cmake -S . -B /tmp/virgil-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/virgil-build-debug --parallel 2
ctest --test-dir /tmp/virgil-build-debug --output-on-failure

cmake -S . -B /tmp/virgil-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVIRGIL_SANITIZERS=ON
cmake --build /tmp/virgil-build-asan --parallel 2
ctest --test-dir /tmp/virgil-build-asan --output-on-failure
```

**Passed: 2/2 Debug suites and 2/2 sanitizer suites**, including AddressSanitizer,
UndefinedBehaviorSanitizer and default leak checking, without disabling checks.
The runs took 36.51 and 41.45 seconds; most elapsed time waits for the real
30-second audit timer. There are **31 behavior tests: 24 library and 7 widget/process
cases**, in addition to Qt Test setup/cleanup. The final builds have no compiler
warnings. Expected offscreen `propagateSizeHints()` warnings remain.
A later direct sandboxed sanitizer launch passed its Qt assertions but exited with
LeakSanitizer’s ptrace/process-inspection restriction. That launch is not counted
as a passing sanitizer run; the complete suite was rerun outside the sandbox
with the previously approved CTest permissions and checks still enabled.

| Milestone 2 behavior | Evidence/result |
| --- | --- |
| Empty/nested folders and breadcrumbs | Headless widget navigation and activated folder rows passed |
| File chooser and drag-and-drop import | Existing picker workflow plus local drop fixture passed; source bytes remain |
| External edits, new directories, nested moves, disappearance | Real filesystem events update snapshots without F5; watcher test passed |
| Watch budget/fallback and missed events | 4,100-file library hits the 4,096-watch budget; an unwatched edit is found by the real 30-second audit and reported |
| Incremental scopes | Subtree refresh leaves another subtree's saved state unchanged until full reconciliation |
| Editor save by replacement | Same path retains ID/title and updates size/identity |
| External folder/course rename while closed | Same course/material IDs and updated paths after reopen |
| Ambiguous hard links, independent identical copies | No automatic record merge |
| Explicit guided relinking | Missing ID/title kept, chosen discovered record joined, file bytes unchanged |
| Rename/move, case-only and destination collisions | Rejected safely; atomic no-replace primitive tested against an occupied destination |
| Trash/restore and course removal | Directory contents/IDs retained; restore collision refuses replacement; organization removal keeps files |
| Interrupted move/trash/restore | Reconstructed post-filesystem/pre-metadata boundaries recover on reopen |
| Unstarted or ambiguous journals | Proven unstarted entry clears without file mutation; ambiguity retains files/journal and blocks further mutation; explicit retry tested |
| v1-to-v2 schema migration | Same material ID; separate read-only check of the preserved v1 snapshot and its integrity |
| Symlinks, incomplete scans, missing roots and cancellation | Existing safety tests plus move/trash symlink rejection passed |
| Larger scans/copies and event-loop responsiveness | 4,000-file scan cancellation/rollback; 4,100-file worker scan heartbeat; existing 64 MiB copy heartbeat/cancellation coverage |
| Selection across automatic refresh | Selected material ID survives snapshot/table reconstruction |

The interruption tests construct journal/filesystem states. They are process
recovery tests, not power-loss tests or adversarial path-race tests.

## UI inspection and limits

```sh
QT_QPA_PLATFORM=offscreen VIRGIL_M2_SCREENSHOT=/tmp/virgil-m2.png \
  /tmp/virgil-build-debug/widget_test nestedNavigationBreadcrumbsAndDroppedImports
```

Passed and the captured window was visually inspected. The breadcrumb toolbar
uses explicit text buttons. A final targeted navigation/selection/drop test and
watch-limit/scan-heartbeat test passed after the test/build refinements (32.27
seconds combined); the full sanitizer suite then passed outside the sandbox.
These are **headless checks**; milestone 2 did not run a native Wayland session or
launch a real external reader. The milestone 1 desktop/offline results remain
historical in [verification.md](verification.md). No new network runtime was added,
but a disconnected namespace run, Release install and platform testing were not
repeated for milestone 2.

Linux tmpfs is the tested material filesystem. Birth-time identity and no-replace
rename are required for app moves/trash; unsupported or cross-mount moves fail.
External identity tracking begins after the first milestone 2 observation.
Ambiguous folders need restoration to their recorded paths. Full metadata
snapshots remain; no guarantee is made for very large libraries, removable-drive
failure mid-write, network filesystems, sudden power loss, Qt 6.8 runtime,
Windows or macOS. Staging review does not guess partial imports complete or erase
them. Automatic/full backups, document indexing and permanent trash erasure
remain outside this milestone.

## Short manual desktop checklist

1. Launch `/tmp/virgil-build-debug/virgil-desktop` with a disposable library.
   Choose a folder, create a nested folder, enter it and use the breadcrumbs.
2. Import/drop a TXT and PDF; confirm the original remains. Rename and move a file;
   try an occupied name and a case-only change, then use an intermediate name.
3. Add/edit/move files in your file manager while Virgil is open; confirm updates.
   Repeat a move while closed, reopen, and verify the file/course identity.
4. Trash a nested folder and restore through Library. Verify bytes and IDs; try
   restoring into an occupied path. Remove course organization and verify all
   files remain in the library.
5. Inspect the interrupted-operation dialog, missing-file relinking, default
   reader/file-manager opening, keyboard navigation, theme and display scaling.

Stop at this checkpoint. Milestone 3 has not been started.
