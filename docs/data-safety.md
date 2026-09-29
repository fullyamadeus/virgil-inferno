# Data ownership, safety and recovery — milestone 4

## Current guarantees and limits

- Opening an ordinary directory does not silently initialize it. **Create
  library** first previews up to 200 immediate entries and offers the file
  manager for deeper inspection. Adoption only adds `.virgil`; it does not move
  existing files. Those files are shown under Unassigned files.
- Semesters, courses and materials have stable UUIDs. Paths are relative to the
  library. Moving the whole library while Virgil is closed preserves relationships;
  reopen its new location to update the remembered preference.
- There is one writable Virgil instance per library. `QLockFile` detects a dead
  local owner; the app never forcibly removes a live lock or expires it by age.
- Rename/move/trash commands journal their intent first. Existing destinations are
  never replaced; even case-only collisions fail. Use a distinct intermediate name.
  Restore through the Library menu. There is no permanent erase command. Removing
  course records explicitly keeps their directories, files and detached resource
  records. Use the visible Delete course button/context menu even if its folder
  has disappeared. External disappearance does not silently delete organization.
- Folder creation can leave a newly created directory if a later metadata action
  fails. It is never removed as a speculative rollback.
- App-created names follow conservative cross-platform component rules and
  case/Unicode-normalization collision checks. Existing external files retain
  their names. No content-based deduplication occurs. An already-in-library file
  stays at its current path, even if a different import destination was selected.
- The root, path components and SQLite sidecars are checked for symbolic links.
  Scans exclude links and reserved `.virgil` directories. Sources must be regular
  files. This is a conservative personal-app policy, not a security boundary
  against an adversarial process swapping path components during an operation.
- An unavailable root, wrong identity marker or unreadable subtree aborts refresh
  before reconciliation. Last saved records are retained. After a complete scan,
  missing files are marked unavailable, not deleted. If a path disappears after
  the scan, opening it checks again and explains how to reconnect.
- A unique Linux device/inode/birth-time identity match at an unoccupied saved
  destination preserves file IDs/titles and folder/course paths across moves.
  Independent copies and ambiguous hard links are not merged. Empty identities,
  conflicting saved paths and cross-filesystem copies need explicit guided file
  relinking. For ambiguous folders, restore their recorded path first. Filesystem
  identity is local evidence, not a content hash or a portable identifier. A file
  must first have been observed by milestone 2 for external identity matching;
  renames made before the v1 upgrade cannot be reconstructed confidently.
- Automatic watching batches events for 750 ms and scans affected subtrees. Watches
  are refreshed after scans; failed registrations/the 4,096-watch cap show a notice.
  Full reconciliation runs every 30 seconds and on F5/startup. A file outside an
  incremental scope can retain its last saved state until a later event/audit;
  opening always checks its actual path. Scans do not classify a growing external
  copy as indexed or complete: files remain ordinary, unclassified materials.
- Local filesystems are the intended target. This session tests temporary libraries
  on tmpfs. Network filesystems, removable-drive failure during individual writes,
  hostile races and sudden power loss are not verified.

## Import protocol

1. Check source, destination, reserved paths and filename collision.
2. Save an operation UUID, destination and staging path in `pending_imports`.
3. Copy in 1 MiB chunks to a new `.virgil/staging/<UUID>.part` file. Report progress
   and check cancellation. Sources are never removed.
4. Check source size and modification time again; reject an observed changing
   source. These checks are practical detection, not an immutable source snapshot.
5. Flush the staged copy, mark the journal ready, and create a hard link at the
   destination. Hard-link creation cannot replace an existing destination. It
   deliberately fails if the filesystem lacks hard links or crosses a mount.
6. Save material metadata and remove the journal entry in one SQL transaction.
   Remove the staging link. The destination remains an ordinary independent copy
   of the original source, not a symlink to the source.

An import over 50 MB is tested. There is no cloud upload quota or file-size cap.
The application does not load the entire imported file in memory or extract its
text. Free disk space and the underlying filesystem still limit storage.

A handled cancellation/error before finalization removes only the operation's
own partial staging copy. A crash may leave the journal and staging file behind.
On next open, Virgil automatically finishes the metadata step **only** if the
staging and destination are provably the same filesystem object. A partial,
unfinalized or ambiguous copy stays in staging and a notice explains this.

Use **Library → Review interrupted operations** to see the operation paths/state
and inspect staging in your file manager. Import again from the unchanged original
when completion is uncertain. A `.part` file is never assumed complete or discarded
by the UI. For manual changes, close Virgil, copy the whole library first, and inspect
that separate copy. Unreferenced staging files after final cleanup interruption may
remain; they never appear as materials.

## Move and trash protocol

A durable SQL journal precedes a Linux atomic `renameat2(RENAME_NOREPLACE)` within
the same filesystem. The metadata/path transaction follows the move. Unsupported
rename primitives, absent birth-time identity and cross-mount moves fail safely.
Trash is `.virgil/trash/<operation UUID>`, with the original relative path retained
in SQLite; directory trees move intact. Their material IDs/titles and organization
remain saved but unavailable until restored. Trash consumes disk space; it is not
a backup. Restoring a nested entry requires its original parent directory to exist.

At startup, a completed move is recovered only when its destination identity matches
and its source is absent. A proven unstarted move clears only the journal. Ambiguous
outcomes retain everything and block new mutations/reconciliation pending review.
The UI can review/retry, or abandon a proven unstarted move. If an unrelated file
occupies the destination, inspect it and move it yourself to a separate location;
Virgil never deletes it to complete recovery. Missing/unreadable roots never produce
mass missing-record reconciliation. Symlink components are rejected conservatively.

Metadata-only course removal and guided file relinking each use a SQL transaction;
neither deletes ordinary file bytes. Guided relinking keeps the chosen missing
record's UUID/title and removes the explicitly selected discovered metadata record.

The filesystem and SQLite are not one atomic transaction. SQLite uses FULL
synchronization; the copy uses a checked flush. This protocol protects originals
and reconciles process interruption, but does not claim power-loss durability
for just-imported or just-moved files, directory entries, trash, or schema backups.
No directory fsync/power-loss testing has been done. Keep originals and backups.

## Database versions and backups

`PRAGMA application_id` identifies Virgil metadata; `PRAGMA user_version=3`
identifies its schema. Migration 001 initializes a new, empty database inside a
new `.virgil` directory in a transaction. Existing unknown, zero-version, corrupt
or newer databases are rejected with an explanation, never reset.

A v1-to-v2 upgrade creates `library.sqlite.before-v2-<UUID>.sqlite` using SQLite
`VACUUM INTO`, opens that snapshot read-only, verifies its integrity, then migrates
in a transaction. The snapshot retains schema 1 and can be used in a separate copy
with the matching older app. Do not replace a live database or ignore its sidecars.
The ordered v2-to-v3 step similarly preserves a verified schema 2 snapshot before
adding resource records and material category/exam dates. v1 libraries pass through
both steps; each migration is transactional. Unsupported schemas are never reset.

Automatic metadata-only snapshots run after successful in-app imports, metadata
edits, moves, trash/restore and guided relinking, plus once daily on open/refresh.
The latest seven verified `automatic-<UTC>-<UUID>.sqlite` copies rotate inside
`.virgil/backups`. Pre-migration snapshots are not rotated. Unknown backup names,
symlink paths and copies with a different library identity are not deleted. Failed
`.part` stages are retained for inspection. Backup errors are shown; a committed
metadata change stays committed. Settings offers an immediate metadata snapshot.

These snapshots protect structured metadata only: lecture notes, resource records,
IDs, organization and saved file references. They contain **no material file bytes**.
After seven later snapshots, an older deleted record may no longer be recoverable.
The application does not offer in-app snapshot restore; inspect/restore only in a
separate full-library copy, with a compatible schema/app version. Coordinated
complete backups, verified restoration and structured export/import are milestone 5.

For now: close every Virgil instance and any external editor writing this library,
then copy the **entire directory, including `.virgil`**. A full closed-library copy
includes materials and metadata. A metadata-only copy does not protect your PDFs.
Restore into a separate directory and open that directory before replacing
anything. Never synchronize an actively written SQLite file to merge libraries.

The schema is readable with ordinary SQLite tools:

| Table | Information owned by Virgil |
| --- | --- |
| `library` | Library UUID and UTC creation timestamp |
| `semesters` | UUID, name, explicit season, ISO dates, relative directory |
| `courses` | UUID, semester UUID, name, code, color, relative directory |
| `materials` | UUID, optional course UUID, display title/category/exam date, relative path, byte size, UTC modification time, availability, optional filesystem identity |
| `resources` | Book/lecture/bookmark UUID, optional course/material references, title/date/notes, URL/folder scope, category/exam date |
| `pending_imports` | Operation UUID, relative destination/stage, copying/ready state |
| `folders` | Relative paths, filesystem identities and last observed availability |
| `operations` | Move/trash/restore UUID, source, destination, identity and kind |
| `trash` | UUID, original path, retained trash path and filesystem identity |

This documents today's storage; it is not a promised interchange format. Portable
JSON export/import and old-web export fixtures will be versioned separately later.

## Recovery states

If the remembered library cannot open, use **Open library** after reconnecting
the drive or choosing the moved directory. The preference and metadata are not
erased. If the schema is newer, use a compatible newer application. If integrity
checks fail, preserve the original directory and restore a separate backup.
If bootstrap was interrupted before writing `library.id`, preserve `.virgil`
for inspection; the application deliberately does not guess or reinitialize it.

## Calendar storage and time changes

Schema 4 upgrades schema 3 only after publishing a verified `.before-v4` SQLite
snapshot. Existing notes, materials and organization retain their IDs. Calendar
records, completion, timezone settings and recurrence policies are included in
metadata-only backups; ordinary file bytes still need a full manual backup.

All-day dates have no timezone shift. Timed events persist UTC instants; weekly
classes persist local wall times and their own timezone. The default is
Europe/Skopje. Unknown/unavailable zones are reported, never silently substituted
when resolving times. Repeated civil times require an explicit earlier/later
choice; nonexistent times are rejected. A weekly occurrence at a clock gap or
unresolved repeat appears as a review warning, not an invented instant.

Exam ranges are inclusive, non-overlapping, limited to three and wholly inside
semester dates. Their chronological order controls recurrence: pause at the first,
resume after it, stop at the second. Editing/removing a period changes that order;
separately saved events are unaffected. Custom teaching dates can override season
defaults but must stay inside the semester. Completion applies only to assignments.
Calendar deletion removes the chosen structured record, never materials or notes.
Course deletion detaches calendar records; reassignment moves their semester
association without changing IDs, explicit event dates or saved times.
