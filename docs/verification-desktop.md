# Desktop installation verification — 2026-09-27

User-requested integration of the existing milestone 4 application on this Fedora
KDE Wayland laptop; no milestone 5 or 6 features were added.

Actual commands and results:

```sh
cmake -S . -B /tmp/virgil-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/home/AbbeFaria/.local
cmake --build /tmp/virgil-build-debug --parallel 2
ctest --test-dir /tmp/virgil-build-debug --output-on-failure
cmake -S . -B /tmp/virgil-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/home/AbbeFaria/.local
cmake --build /tmp/virgil-build-release --parallel 2
DESTDIR=/tmp/virgil-install-stage cmake --install /tmp/virgil-build-release
cmake --install /tmp/virgil-build-release
```

Both builds succeeded with GCC 15.2.1, Qt 6.10.3 and CMake 3.31.11. CTest passed
library, calendar and widgets (3/3, 43.23 seconds); widgets ran headlessly with
the offscreen Qt platform and disposable libraries/preferences.

The staged release executable printed `Virgil Inferno 0.1.0` with `--version`.
`ldd` resolved every shared library from system paths, with no build-directory
dependency. `desktop-file-validate` accepted the generated and installed desktop
entries. Installation placed the executable in `~/.local/bin`, the app-menu entry
in `~/.local/share/applications`, the provided SVG in the hicolor scalable icon
directory, and the PNG in `~/.local/share/pixmaps` (also embedded in the executable).

With permission to write outside the workspace, the desktop entry was copied to
`~/Desktop/Virgil.desktop` with mode 0755. `update-desktop-database` and
`kbuildsycoca6 --noincremental` succeeded. `gio launch` accepted the installed
desktop entry. A subsequent direct launch of `~/.local/bin/virgil-desktop` remained
running on the real desktop, confirmed by `ps -C virgil-desktop -o pid,args` outside
the restricted shell's process namespace. The installed executable matches the
release build byte for byte. Existing QSettings names and library formats remain
unchanged. No user material files were modified by the installer.

Desktop launch/process startup was checked, but clicking the desktop shortcut,
visual appearance of the menu/panel icon, pinning and scaling remain manual checks.
KDE may request confirmation the first time a copied desktop shortcut is opened.
This is a per-user installation using system Qt, not a portable binary or an RPM.
