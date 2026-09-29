# Roadmap and checkpoint

Implement one compiling milestone at a time. **Milestones 1–4 are complete; stop at the milestone 4 checkpoint.
Milestone 5 needs a later user instruction.**
Reliability, local offline operation and real files take priority over visual polish. This is a separate
C++20/Qt 6 Widgets implementation, with no React, webview or JavaScript runtime.

## 1 — Foundation and first usable workflow

**Completed as the first learning checkpoint on 2026-09-25.** Full-release features
remain deferred below; external-reader behavior still has a short manual checklist.

Implemented:

- CMake targets, Ninja presets, Debug/Release/sanitizer configurations, installation
  target, native main window, separate core/UI, Qt Test/CTest.
- Create/open/switch libraries, explicit inspection/adoption of ordinary directories,
  remember successful location with QSettings, actionable recovery for missing roots.
- Create semesters with dates and explicit winter/summer designation. Create manual
  courses with optional code and the requested ten-color palette; ordinary directories
  and four initial subfolders. Browse all saved semesters.
- Background file imports, progress/cancellation, preserved source, no upload cap,
  collision rejection, no duplicate when already inside the library.
- Startup/manual scans of filenames and stat information, nested files shown with
  folder paths, unassigned files, native sorting, external opening/containing folder,
  metadata persistence and whole-library relocation.
- Safety prerequisites brought forward: UUIDs, relative paths, reserved metadata,
  path/symlink policy, schema identity/version, integrity checks, process lock,
  import staging/journal, retained missing records and incomplete-scan rollback.
- Tests and teaching/recovery documentation. See `verification.md` for actual results
  and the boundaries of GUI, offline and platform verification.

Deliberate limits at the milestone 1 checkpoint: manual refresh, flat material table, no folder editing/deletion,
no rename identity matching, full metadata snapshots, no catalog, no content indexing,
no calendar/practice records, no automatic backup or RPM. Some later pages contain
only clear availability explanations. No user data from the old web app was migrated.

## 2 — Reliable folder integration

**Completed on 2026-09-27.** See [verification-m2.md](verification-m2.md) for
actual checks and platform/durability limits. Implemented:

- Debounced filesystem watchers with changing directory watches, missed-event and
  watch-limit detection, startup/incremental reconciliation and manual refresh fallback.
- Interactive nested-folder navigation/breadcrumbs, create/rename/move operations,
  filename and case-only collision behavior, file chooser plus drag-and-drop imports.
- Handle external edits, replacements, incomplete copies, and disappearance while
  open or closed. Keep newly found and unsupported files visible before indexing.
- Preserve metadata across confidently identifiable renames/moves. Never merge
  identical-content duplicates automatically; provide guided relinking otherwise.
- Recoverable app trash. Distinguish removing course organization from deleting
  its files; never silently erase a course directory.
- Strengthen the small operation journal and interrupted-operation review UI;
  verify recovery around rename/move/delete boundaries. Keep symlink traversal
  conservative. Validate responsiveness/cancellation with larger scans and copies.

The watcher batch waits 750 ms, scans affected subtrees, and updates changed
metadata in one worker transaction. Directory/file watch sets change with scans;
a 4,096-watch budget and detected registration failures expose a fallback notice.
Full 30-second audits detect changes missed by watches; startup and F5 scan fully.

Moves/trash use Linux atomic no-replace rename and device/inode/birth-time identity.
Case-only changes require an intermediate distinct name. Cross-filesystem moves
and filesystems without the required primitives fail safely. Hard-link ambiguity
never merges records. Guided file relinking is explicit; ambiguous folder identity
requires restoring its recorded path. Trash has restore, with no permanent erase.
Import staging can be inspected from recovery UI but is never guessed complete or
automatically discarded. A verified pre-migration SQLite snapshot protects the
v1-to-v2 schema upgrade. Full snapshots/paging, power-loss guarantees and larger
scale measurements remain future work; 4,000-file scans and 64 MiB copies are tested.

## 3 — Course resources

**Completed on 2026-09-27.** See [verification-m3.md](verification-m3.md) for actual
checks. Implemented:

- Edit semesters/courses and their organization without destabilizing record IDs.
  Searchable offline FINKI SEIS23 catalog only after verifying reference data.
  Manual course entry always works without a catalog or network.
- Material display titles, categories (past exam, single question, study guide,
  other, lecture material), exam dates; unclassified external files.
- Book records with title/local file; lecture title/date/notes/optional attachment.
  Deleting a lecture record can keep its attachment as an ordinary material.
- Titled course/folder bookmarks, category/date metadata. Saving a URL does not
  download its content; remote opening needs connectivity.
- Filename/metadata search, improved dashboard navigation, consistent course colors.
- Automatic rotating **metadata-only** backups using consistent SQLite snapshots.
  They do not protect the material files. Add migrations with pre-migration snapshots
  as the schema grows; never reset an unsupported database.

The offline catalog contains 157 rows from the official English four-year SEIS23
page; two contradictory-code rows are excluded and documented. It is a dated
snapshot, not a prerequisite/admissions validator or an automatic updater. Manual
entry always works. Course deletion is prominent and works for missing folders;
files and detached books/lectures/bookmarks are kept. Metadata organization edits
keep existing directory paths; physical changes use the separate M2 operations.

Schema 3 adds material categories/exam dates and local resource records. Ordered
v1→v2→v3 steps create verified pre-migration snapshots before each existing-library
change. The latest seven automatic metadata-only snapshots rotate after in-app
changes and daily on open/refresh; backup failure is reported without undoing
already committed notes. They do not contain file bytes or provide in-app restore.
Full backup/restore and content search remain in milestone 5.

## 4 — Calendar

**Completed on 2026-09-27.** See [verification-m4.md](verification-m4.md) for actual
checks and native-desktop limitations. Implemented:

- Monday-first month/week views, semester filters, course colors, event editing and
  deletion for exams/assignments/review sessions/individual lectures and labs.
  Optional course associations, all-day/timed events and appropriate completion.
- Weekly lecture/lab schedules store weekday + local wall times + timezone.
  All-day events store dates; timed instants store unambiguous timestamps.
  Default Europe/Skopje, with explicit handling and tests for ambiguous/nonexistent
  local times during daylight-saving transitions. Do not silently guess.
- Up to **three inclusive, non-overlapping exam ranges**, wholly inside the semester.
- Explicit default academic recurrence policy, independent of the semester name:
  winter teaching October 1–December 31; summer February 18–May 31; intersect with
  semester dates. Recurrences pause during the first exam period and resume after
  it, then stop from the start of the second onward. The third never restarts
  classes. Separately saved events are not deleted or hidden by this policy.
- Keep this policy configurable in design and regression-test boundaries, leap
  years, period adjacency, semester clipping, timezone and DST cases.

Implementation: native month/week grids and agenda, semester filters, upcoming
saved events, five event types, assignment completion, weekly lecture/lab CRUD
with multiple weekdays, and exam-period CRUD. Schema 4 stores local recurrence
rules separately from UTC event instants. Teaching dates can override the explicit
season defaults. Clock gaps are never shifted; unresolved repeated times produce
visible weekly warnings. Course color selectors use names in both forms.
Verification is recorded in [verification-m4.md](verification-m4.md).

## 5 — Library durability and document indexing (not started)

- Full backups coordinate metadata snapshots and material mutations. Verify a
  restore in a separate directory before any replacement. Test relationships,
  attachments, schema upgrades, relocation and interrupted recovery.
- Versioned, documented structured export/import for app-specific information;
  never trap personal notes or calendar/practice data in an undocumented format.
- Background PDF/TXT extraction and content search, cached by change information.
  No rereading every PDF each startup. Preserve page references where feasible;
  identify image-only PDFs without pretending OCR is present. Indexing failures
  never block normal file storage, listing or external opening.
- Settings for library location/switching, timezone, appearance, backups/exports
  and practical diagnostics. Native light/dark adjustments after core workflows.

## 6 — Saved practice and web-export migration (not started)

- Offline manual creation/import/editing of quizzes and flashcards. Preserve title,
  type, topic, difficulty, English/Macedonian language, source docs and creation date.
- Quizzes: four distinct options, exactly one correct answer; flashcards: prompt
  and answer. Save attempts/scores/remembered/missed progress, explanations and flags.
- Preserve quotes/explanations when originals disappear. Resolve document/page
  references locally where supported; explain unavailable originals and that not
  every external PDF reader can open a requested page.
- One-time importer for an old-web export containing downloaded materials/books
  and relevant metadata. Define and test fixtures when a real export is unavailable;
  never describe fixture import as migrating the user's real data.
- AI generation is separate optional work, never required for saved practice.
  Any future generation validates quotes against selected sources and keeps page
  citations. Do not label deterministic placeholder questions as AI output.

## 7 — Fedora release (not started)

The user-requested desktop integration is implemented separately: a permanent
per-user executable installation, application-menu launcher, SVG/PNG icons and
Wayland desktop identity. This does not start milestones 5–6 or complete the
remaining Fedora release work below.

- Installable RPM, runtime requirements and upgrade checks. Verify
  installed first launch and routine operation with internet disabled and a fresh
  local profile. Ensure uninstall/upgrade preserves libraries and preferences.
- Test the release build on Fedora 43, desktop rendering/default-app opening,
  shortcuts and scaling. Keep unverified platforms explicitly listed.
- Consider build/test CI, without making local builds depend on a hosted service.
  Windows/macOS builds and installers remain future work until actually verified.

## Optional work that must not delay the offline release

- Phone file access via external folder sync; phone platform and editing needs are
  undecided. A paired local web interface or full synchronization is separate work.
  Ordinary file sync must not merge actively written SQLite databases.
- Optional accounts only for explicitly enabled sync; local startup/read/write
  continue on provider/account/network failure.
- Optional online AI requires configuration and consent before sending excerpts.
  Local AI needs a separate hardware evaluation. No bundled large model/API-key
  requirement in the desktop core.
- Embedded PDF viewer, OCR, additional extraction formats, richer theming.

## Reference material and assets

The starter specification is sufficient for milestone 1; no old-web source or
credentials were copied. The public reference files under `/home/AbbeFaria/virgil`
may be read later for exact catalog data, calendar/citation tests and SQL export
mapping. Needed later: the actual FINKI SEIS23 catalog, any approved logo/icon,
and a documented web export plus original downloaded documents/books. Do not
invent an official catalog. Never read `.env` secrets or adopt Next.js build rules.
