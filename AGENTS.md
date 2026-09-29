# Virgil Desktop conventions

This is a separate C++20 / Qt 6 Widgets desktop project. Do not apply the old
web project's build instructions. Keep changes here; do not read credentials,
alter `/home/AbbeFaria/virgil`, publish, or push without a request.

- Work one milestone at a time. Milestone 1 is the first learning checkpoint;
  do not start milestone 2 without the user's next instruction.
- Read `README.md`, `docs/architecture.md`, and `docs/roadmap.md` when continuing.
- Use Qt >= 6.8, CMake >= 3.24, C++20; prefer Fedora packages and Ninja.
- Keep build directories outside the source tree. In restricted sessions use
  `/tmp/virgil-build-debug` (the presets default to the user's cache).
- No network, web runtime, accounts, telemetry, hosted database, or AI dependency
  in the desktop core. Materials are ordinary files; SQLite is local metadata.
- UI callbacks dispatch operations; filesystem writes and SQL belong in services
  and storage. One worker owns the library, lock, and SQL connection.
- Preserve originals. Check paths and errors, reject symlinks conservatively,
  never silently overwrite or delete materials, never reset a failed database.
- Use stable UUIDs and library-relative paths. Never infer deletion from an
  unavailable root. Do not promise external rename tracking before implemented.
- Use Qt parent ownership for widgets; do not also put them in owning smart
  pointers. Use RAII for non-widget resources and transactions.
- Test important behavior with disposable libraries and Qt Test/CTest. Report
  actual commands/results and distinguish headless checks from desktop testing.
- Keep README concise; record durability limits, implementation status, and
  a short code walkthrough. Explain a few concrete C++/Qt concepts at a time.
- No sub-agents are required. Do not add dependencies or abstractions speculatively.

The workspace initially contains protected empty `.git`, `.agents`, `.codex`
directories and is not a working Git repository. Do not modify those directories
or claim commits were made. Build and test do not depend on Git.
