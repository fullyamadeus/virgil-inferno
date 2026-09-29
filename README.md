# Virgil Desktop

A personal, local study workspace in **C++20 and Qt 6 Widgets**. Milestones 1–4
provide libraries, folders, course resources, a calendar, metadata search and backups.
Document indexing and practice remain on the roadmap.

Materials stay in normal folders. Structured information lives in
`.virgil/library.sqlite`: an automatically managed local file, with no server,
account, subscription, cloud quota or database administration. Normal runtime
has no network dependency, credentials, telemetry or mandatory update check.

## Build and run on Fedora 43

Minimums: Qt **6.8**, CMake **3.24**, a C++20 compiler. Development setup can
download packages; running the installed application does not need downloads.
The actual verified versions/results are in [milestone 4 verification](docs/verification-m4.md).

```sh
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
~/.cache/virgil-desktop/build/debug/virgil-desktop
```

Presets put generated files outside this source tree, in the user's cache. For
an explicitly chosen build directory (also used in this development session):

```sh
cmake -S . -B /tmp/virgil-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/virgil-build-debug --parallel 2
ctest --test-dir /tmp/virgil-build-debug --output-on-failure
/tmp/virgil-build-debug/virgil-desktop
```

Without Ninja, omit `-G Ninja` and use an installed CMake generator. Debug builds
keep symbols for a debugger. `release` enables normal compiler optimization;
`asan` enables AddressSanitizer and UndefinedBehaviorSanitizer with tests.
On Fedora the optional sanitizer build also needs `libasan` and `libubsan`
(`sudo dnf install libasan libubsan`).

## First workflow

1. Choose **Create library…**, select/create a directory, inspect it, then
   confirm creation or adoption. Existing files are not moved or renamed.
2. Add a **New semester…** with dates and explicit winter/summer designation.
3. Add a **New course…**, with an optional code and a color. Virgil creates
   `Lectures`, `Books`, `Past Exams` and `Other Materials` beneath its directory.
4. Select the course and **Import file…** (`Ctrl+I`). Choose a destination and
   filename. The source stays intact. Collisions are reported; nothing is replaced.
5. Double-click the material, press Enter, or choose **Open file**. Your default
   desktop application opens it. The context menu also opens its containing folder.
6. Close and reopen Virgil. The last library is remembered. Use **Refresh** (`F5`)
   to reconcile immediately. File/directory watches update changes automatically,
   with a full reconciliation every 30 seconds to cover missed events.

Existing files anywhere in an adopted library appear under **Unassigned files**.
Files inside course folders are associated with that course, including nested
folders. The folder selector and clickable breadcrumbs navigate empty or nested
folders; double-click a folder to enter it. **Organize** offers new folders,
rename/move, relinking and trash. Case-only renames require a distinct intermediate
name. Drop local files onto Materials to copy into the current folder.

**Library → Restore from library trash** restores files without replacement.
**Delete course** is available beside the course heading, in the tree context menu,
and with Delete while the tree has focus. It works when the folder is already
missing. Existing files and resource records are kept and become unassigned. A confident
Linux filesystem identity match preserves IDs across external moves; ambiguous
files stay separate until you choose **Relink missing file**. Unsupported formats
remain visible and open with the desktop. The other pages explain deferred features.

## Course resources

- **Edit course / semester** changes display names, dates, colors and semester
  organization while keeping UUIDs and existing folder paths. Use Rename / Move
  separately for physical changes. The dashboard course list opens a course.
- New course offers a searchable, bundled **FINKI SEIS23 English, four-year**
  snapshot verified against the official page. Manual entry always works. See
  [catalog provenance and exclusions](docs/catalog-source.md). No runtime download.
- **Materials → Edit metadata** saves an independent display title, category and
  optional exam date, even for missing files. External files start Unclassified.
- **Books** saves a title with a local file. **Lectures** saves a
  title, date and plain-text notes with an optional attachment. **Browse files…**
  opens a file chooser for any readable local file on your computer. Saving copies
  outside files into the course’s Books/Lectures folder (or the library root for
  unassigned records), preserving originals. Files already in the library stay in
  place. You can also select an existing material from the list. Cancelling the
  form imports nothing; filename collisions never overwrite files. Delete record
  keeps the attachment in Materials. Missing attachments do not erase notes.
- **Bookmarks** saves a title/URL with a course or folder scope and category/exam
  date. Saving does not fetch content; Open URL invokes your browser explicitly.
- Search matches filenames, titles, course codes, categories, dates, notes and URLs.
  Check **All courses** to include other courses and unassigned records.

## Calendar

- **Calendar** has Monday-first Month/Week views, Previous/Next/Today controls,
  a date picker, semester filtering, course colors and a Coming up list. Click a
  day to enter its week; double-click an entry to edit it. Events in view lists
  every entry, including those that need scrolling inside a day cell.
- Add/edit/delete exams, assignments, review sessions, individual lectures and
  labs, with an optional course. Assignments can be marked complete or incomplete.
  Independent events appear in every semester filter; completed events remain
  visible but leave Coming up.
- **Weekly timetable** adds several weekdays with separate times and lecture/lab
  types, including Sunday. Edit/delete individual slots. Each stores local wall
  times and its timezone; the calendar projects occurrences without saving copies.
- **Exam periods** supports up to three inclusive, non-overlapping ranges inside
  each semester. Adjacent dates are allowed. Classes pause during the first,
  resume after it, and stop from the second onward; the third never restarts them.
  Editing/deleting periods recalculates their chronological order.
- **Semester / teaching dates…** edits semester dates and optional custom teaching
  dates. Defaults use the explicit season: winter October 1–December 31, summer
  February 18–May 31, clipped to semester dates. The semester name does not decide
  the rule. These rules never suppress separately saved events.
- Default timezone is **Europe/Skopje**, configurable through **Timezone…**.
  All-day dates stay fixed; timed events save UTC instants. Repeated local times
  require an explicit earlier/later choice. Nonexistent times are rejected;
  weekly occurrences needing clock-change review show a warning instead of moving.
  Changing display timezone keeps saved event instants and weekly wall times intact.

Course creation and editing both show color names with swatches. Reassigning a
course moves its associated events/classes to the new semester without changing
IDs, event dates or times. Deleting a course keeps those calendar records unassigned.

To open a moved or different library:

```sh
virgil-desktop --library "/path/to/Virgil Library"
```

## Installation

```sh
cmake --preset release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build --preset release
cmake --install ~/.cache/virgil-desktop/build/release
update-desktop-database ~/.local/share/applications
```

Launch **Virgil** from the desktop's application menu and pin it to the panel if
desired. The executable stays at `~/.local/bin/virgil-desktop`; deleting the build
folder does not affect it. The launcher and your provided SVG/PNG icons are
installed under `~/.local/share`. To add a desktop shortcut:

```sh
cp -i ~/.local/share/applications/virgil-desktop.desktop "$(xdg-user-dir DESKTOP)/Virgil.desktop"
chmod +x "$(xdg-user-dir DESKTOP)/Virgil.desktop"
```

KDE may ask you to confirm the first launch of a desktop shortcut. Choose the
install prefix when configuring, since the launcher records the absolute executable
path. Reconfigure before installing to a different prefix. Rebuilding and installing
again updates the application without moving libraries or changing preferences.

This installs the executable, not Qt itself. Fedora runtime packages are
`qt6-qtbase` (includes the SQLite plugin), `qt6-qtbase-gui`, and `qt6-qtwayland`
for Wayland. They must be installed before disconnected use. No interface assets
are downloaded; this milestone uses native Qt widgets and locally drawn swatches.
The launcher/icon are implemented; an RPM and upgrade tests remain milestone 7 work.
See [desktop installation verification](docs/verification-desktop.md) for checks
and remaining manual desktop checks.

Libraries live wherever you choose, outside the source/build/install directories.
Removing the executable does not remove them. UI preferences use `QSettings`
(normally `~/.config/Virgil/Virgil Inferno.conf` on Fedora).

## Data safety and next steps

Close Virgil before making a manual backup, and copy **the entire library,
including hidden `.virgil`**. Keep materials unchanged during that copy.
Verified **metadata-only** backups run after in-app imports, edits and moves, and
once daily on open/refresh. The latest seven are retained in `.virgil/backups`;
Settings can create one immediately. They do **not** protect material files. Full
backup restore is not implemented. Imports currently
require a local filesystem supporting hard links, with the destination on the
same filesystem as `.virgil/staging`; an unsupported destination fails safely.

Existing v1/v2/v3 libraries upgrade to schema 4 after verified metadata snapshots in
`.virgil`. Moves/trash require Linux birth-time identities and atomic no-replace
rename support on the same filesystem. There is no permanent erase command.

Read [data safety and recovery](docs/data-safety.md) before using valuable data.
Read the [architecture and learning guide](docs/architecture.md) for a walkthrough,
the [roadmap](docs/roadmap.md) for all remaining scope, and
[milestone 4 verification](docs/verification-m4.md) for current results and a short desktop checklist.
Windows/macOS builds and behavior are not verified.
