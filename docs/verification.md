# Milestone 1 verification — 2026-09-25

Historical checkpoint. Current folder integration results are in
[milestone 2 verification](verification-m2.md).

## Environment and build

Actually used: Fedora 43 KDE, x86_64, GCC 15.2.1, CMake 3.31.11,
Ninja 1.13.1, Qt **6.10.3** Core/Gui/Widgets/SQL/Test. The workspace initially
had no source and no usable Git repository (its `.git` directory is protected
and empty). No web-project changes, credential reads, real-material tests or
Git commits were performed.

Initially CMake/Qt headers were absent. Fedora's approved installation of `cmake`
and `qt6-qtbase-devel` also updated the installed Qt package family from 6.10.1
to 6.10.3. `libasan`/`libubsan` were added for sanitizers. The SQLite driver is
`/usr/lib64/qt6/plugins/sqldrivers/libqsqlite.so`, supplied by `qt6-qtbase`.
These were development-time downloads; the app does not fetch runtime content.

```sh
cmake -S . -B /tmp/virgil-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/virgil-build-debug --parallel 2
ctest --test-dir /tmp/virgil-build-debug --output-on-failure
```

**Passed:** Debug build and both CTest suites. There are **20 behavior test
functions**: 16 core/library cases and 4 widget/process cases (Qt Test's setup and
cleanup hooks are additional reported entries). The final build has no compiler
warnings. The offscreen platform prints its expected `propagateSizeHints()`
limitation during modal dialogs; the Wayland workflow run produced no warnings.

```sh
cmake -S . -B /tmp/virgil-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build /tmp/virgil-build-release --parallel 2
cmake --install /tmp/virgil-build-release --prefix /tmp/virgil-install
```

**Passed:** optimized build and staged installation. The executable is
`/tmp/virgil-install/bin/virgil-desktop`. This is a CMake installation smoke
check, not an RPM installation or upgrade test. `/tmp` artifacts are disposable;
use the README's cache-directory presets for a lasting local build.

## Behavioral coverage

| Area | Result |
| --- | --- |
| Create, inspect/adopt, create semester/course, import, close/reopen | Passed core tests and actual forms/file-picker workflow |
| UUIDs/relationships/bytes across fresh processes | Passed subprocess create/verify fixture |
| Remembered library with fresh isolated QSettings | Passed reopen and missing-library recovery |
| Whole-library relocation | Passed, same material/course UUIDs and relative paths |
| Unicode/Cyrillic/spaces, case-insensitive collision checks | Passed |
| Copy leaves source unchanged; already-local file avoids duplicate | Passed |
| Distinct identical-content files remain separate | Passed |
| External discovery/nested files, save-by-replacement, external rename | Passed current M1 semantics; rename creates new + retained missing record |
| Unavailable root/empty mount point, unreadable subtree | Passed; existing availability/metadata retained on incomplete scan |
| Cancelled copy, cancelled scan and rollback during reconciliation | Passed |
| Source changes during import | Passed; changed source retained, partial copy not installed |
| Traversal/metadata paths and symlinks | Passed rejection/exclusion checks |
| Writable lock in same and separate processes | Passed, released on close |
| Newer/corrupt database | Passed rejection without reset |
| Interrupted import after finalization; partial import | Passed confident recovery and conservative retention |
| Destination no-overwrite finalization | Passed |
| Same named course in two semesters; invalid dates | Passed |
| 64 MiB import, GUI heartbeat, queued cancellation/shutdown | Passed; no 12/50 MB storage limit |
| External-open request | Passed local `file:` URL dispatch via a captured desktop handler |

The import-interruption fixture reconstructs journal/filesystem states; it is
not a power-loss test. Permission fixtures are Unix-specific. Core Qt warnings
cause test failures, helping catch SQL connection lifetime mistakes.

## Offline and GUI checks

```sh
unshare --user --map-current-user --net ctest --test-dir /tmp/virgil-build-debug --output-on-failure
```

**Passed: 2/2 suites**, including subprocesses, inside a separate network
namespace with no network connectivity. Tests create their own temporary
libraries and local preference directories and do not require cloud credentials.
The final run took about 0.8 seconds on this machine; this is not a performance
guarantee for large libraries or slower disks.

The installed Release executable was also launched with `QT_QPA_PLATFORM=offscreen`
inside that kind of network namespace, a new `/tmp/virgil-installed-offline-*`
profile and a minimal environment containing no cloud credentials. It remained
running for a three-second first-launch smoke check with no stderr output, then
was terminated by the harness. No library was opened in that process. The full
workflow is covered by the widget suite; this installed-process check covers
startup and runtime dependencies, not complete installed-app interaction.

```sh
QT_QPA_PLATFORM=wayland VIRGIL_TEST_SCREENSHOT=/tmp/virgil-wayland.png \
  /tmp/virgil-build-debug/widget_test firstLaunchThroughFormsAndRestart
```

**Passed on the actual KDE Wayland session**, with approved desktop access. It
briefly opened real widgets and file-picker/form windows against a disposable
library. A captured window image was inspected, a layout issue was corrected,
and the test/render was repeated successfully. This is more than a headless
widget test, but it does not verify every display scale or desktop theme.

External opening is deliberately intercepted by the automated widget test to
assert the exact local URL without leaving third-party viewers running. The
actual PDF/text reader and file-manager response remains a manual check.

## Sanitizers

```sh
cmake -S . -B /tmp/virgil-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVIRGIL_SANITIZERS=ON
cmake --build /tmp/virgil-build-asan --parallel 2
ctest --test-dir /tmp/virgil-build-asan --output-on-failure
```

**Passed: 2/2 suites, with AddressSanitizer, UndefinedBehaviorSanitizer and leak
checking enabled**, in the approved run outside the agent's sandbox. The first
build exposed missing sanitizer runtime packages, which were installed. The
first sandboxed test run could not complete LeakSanitizer's process inspection
(`LeakSanitizer does not work under ptrace`); that was an environment failure,
not counted as a passing run. The unrestricted rerun completed without findings
or disabling leak detection. System Qt libraries themselves are not rebuilt
with sanitizers.

## Short manual desktop checklist

1. Start `/tmp/virgil-install/bin/virgil-desktop`. Create a disposable library,
   then a semester/course; use a name containing Macedonian letters.
2. Import a small PDF and a TXT file. Open each and use Open containing folder.
   Confirm the default reader/file manager opens the expected local file/path.
3. Add another file with your file manager, press F5, and check it appears. Close
   Virgil, reopen it and verify the library/course/material are remembered.
4. Repeat ordinary reading/importing with internet access disabled. Try keyboard
   Tab navigation, Ctrl+I, F5, Enter, resizing and your usual display scaling.

Pending by milestone: live watching/rename relinking/trash; automatic/full
backups and restores; schema upgrades beyond the v1 bootstrap; calendar/DST;
practice/migration; RPM upgrades/uninstall and release packaging. No Windows,
macOS, Qt 6.8 runtime, network-filesystem, large-library scale or power-loss
behavior has been verified. Test libraries in this session used local tmpfs.
