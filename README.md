<!-- SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# The Ranch

Corral your KDE Plasma desktop icons into titled, coloured, resizable groups.
Push one against a screen edge and it becomes a slide-out drawer.

- **Corrals:** draw a rectangle on the desktop and drop icons into it. Each corral
  has its own title, background (a colour or an image with transparency and an
  optional colour overlay), icon size, and lock. Corrals keep the size you drew;
  icons that don't fit go onto further pages.
- **Drawers:** drag a corral to a screen edge and it docks there as a drawer with a
  divider-style tab. Hover or click the tab to slide it out over your windows (or
  keep it below them, per drawer). Resize it in place, or tear it off again in one
  drag.
- **Hide anything:** individual corrals, or all of them, from the notification-area
  icon or a global shortcut.
- **Never in the way when closed:** whenever The Ranch isn't running (quit, logout,
  crash), every icon goes back onto your normal desktop, and returns to its corral
  next time.


---

## Requirements

### To run

- **KDE Plasma 6.8 or newer, in a Wayland session.** The Ranch uses
  `wlr-layer-shell` (via LayerShellQt) to sit between the desktop and your windows.
  X11 sessions aren't supported, and other compositors are untested.
- The runtime libraries below. On a normal Plasma install they're almost all present
  already; `qqc2-desktop-style` is the one most likely to be missing on a minimal
  system.

### To build

- CMake ≥ 3.20 and a C++20 compiler (GCC or Clang)
- Extra CMake Modules (ECM) ≥ 6.10
- Qt ≥ 6.10 (Core, Gui, Widgets, Qml, Quick, QuickControls2, Wayland)
- KDE Frameworks ≥ 6.10: CoreAddons, I18n, KIO, DBusAddons, GlobalAccel,
  StatusNotifierItem, Config, XmlGui, Kirigami
- LayerShellQt ≥ 6.8

Developed and tested with Qt 6.12, KDE Frameworks 6.31 and Plasma 6.8.2 on
CachyOS (Arch). Other distributions should work if their versions meet the minimums
above, but haven't been tried.

| Component | Needed for | Arch / CachyOS package |
|---|---|---|
| CMake | build | `cmake` |
| Extra CMake Modules | build | `extra-cmake-modules` |
| Qt base (Core, Gui, Widgets) | build + run | `qt6-base` |
| Qt Declarative (QML, Quick, Controls, Dialogs, Effects) | build + run | `qt6-declarative` |
| Qt Wayland | run | `qt6-wayland` |
| KIO | build + run | `kio` |
| KI18n | build + run | `ki18n` |
| KCoreAddons | build + run | `kcoreaddons` |
| KDBusAddons | build + run | `kdbusaddons` |
| KGlobalAccel | build + run | `kglobalaccel` |
| KStatusNotifierItem | build + run | `kstatusnotifieritem` |
| KConfig | build + run | `kconfig` |
| KXmlGui (About dialog) | build + run | `kxmlgui` |
| Kirigami | run | `kirigami` |
| QQC2 Desktop Style (KDE look for controls) | run (recommended) | `qqc2-desktop-style` |
| LayerShellQt | build + run | `layer-shell-qt` |

On Arch-based systems everything can be installed in one go (headers are included in
the normal packages):

```bash
sudo pacman -S --needed cmake extra-cmake-modules qt6-base qt6-declarative qt6-wayland \
    kio ki18n kcoreaddons kdbusaddons kglobalaccel kstatusnotifieritem kconfig kxmlgui \
    kirigami qqc2-desktop-style layer-shell-qt
```

On other distributions, install the *development* packages for the components in
the table (often named `…-devel` or `…-dev`). CMake reports anything missing,
including the run-time QML modules, when you configure.

---

## Build and install

The simplest way is the install script. It builds into `~/.local` (no root needed),
writes absolute paths into the menu entry, and turns on start-at-login:

```bash
./install.sh
~/.local/bin/theranch --new-corral
```

Or by hand:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
cmake --install build
```

CMake's summary may list *Vulkan* or *Qt6QmlCompilerPlusPrivateTools* under "optional
packages not found". Those are optional parts of Qt itself, not something The Ranch
needs, so they can be ignored.

When installing by hand to a prefix whose `bin` isn't on `PATH` for your session,
edit `Exec=` in the installed `org.theranch.desktop` to the full path, or the desktop
portal won't recognise the app. `install.sh` does this for you.

### Uninstall

```bash
./uninstall.sh            # returns all icons to the desktop, then removes the app
./uninstall.sh --purge    # …and also deletes settings and imported background images
```

---

## Use

| Do this | To |
|---|---|
| Tray icon → **New Corral…** (or `theranch --new-corral`) then drag a rectangle | Create a corral |
| Drag icons from the desktop / Dolphin onto it | Put them in it (desktop files move to `~/Desktop/.ranch/<id>/`; files from elsewhere are linked, never moved) |
| Drag an icon back out, or right-click → **Remove from Corral** | Return it to the desktop |
| Drag the title bar (or the top grip when the title is hidden) | Move |
| Drag an edge or corner | Resize |
| Double-click the title | Rename |
| ⋯ button or right-click empty space | Background, show/hide background or title, lock, icon size, dock, hide, delete |
| ⋯ → **Background…** | Colour or image background, with transparency, fit, a colour overlay and the ideal image size for this corral |
| Drag until the pointer hits a screen edge, then release | Dock as a drawer |
| Hover or click a drawer's tab | Open it (click pins it open) |
| Drag a drawer's tab along the edge | Reposition it |
| Drag a drawer's tab (or open title bar) away from the edge | Tear it off; drop anywhere, or on another edge to re-dock |
| Drag an open drawer's inner edge, ends or inner corners | Resize it without undocking |
| Drawer ⋯ → **Drawer Stacking** | Above Windows / Below Windows, per drawer |
| Mouse wheel, ‹ › arrows or page dots | Change page |

Deleting a corral never deletes files. Its contents go back onto the desktop.

### Tray icon (notification area)

| Action | Does |
|---|---|
| Left-click | Hide / show all corrals |
| Middle-click | Draw a new corral |
| Hover | Tooltip with corral count and how many are hidden |
| Right-click | The menu below |

```
New Corral…
───────────
☑ Games                 ← one checkbox per corral: show/hide it
☑ Work (drawer)
☐ Recovered
───────────
Show All Corrals        ← also brings back individually hidden corrals
Hide All Corrals
───────────
☐ Lock All Corrals
Put All Drawers Above Windows  ← sets every drawer at once
Put All Drawers Below Windows
☑ Start at Login
Open Corrals Folder
───────────
About The Ranch         ← version, author, licence and where to report bugs
Quit
```

Global shortcuts for **Show/Hide All Corrals** and **Draw New Corral** can be bound in
*System Settings → Keyboard → Shortcuts → The Ranch*.

### When The Ranch isn't running

Your desktop looks exactly as if corrals didn't exist. Whenever The Ranch stops,
whether by quitting, logging out, a crash or being killed, every corral's icons go
back onto the desktop. The next time it starts, they're gathered back into their
corrals. Files you create on the desktop in the meantime are left alone, even if they
share a name with something that was in a corral.

To put everything back on the desktop by hand (this also quits a running instance):

```bash
~/.local/bin/theranch --restore-desktop
```

---

## Where things are stored

| Path | What |
|---|---|
| `~/Desktop/.ranch/<id>/` | Each corral's files while The Ranch is running (Dolphin shows hidden folders with Ctrl+H) |
| `~/.config/theranch/corrals.json` | Corral settings: titles, positions, sizes, backgrounds, docking |
| `~/.local/share/theranch/backgrounds/` | Copies of background images you've chosen |
| `~/.local/state/theranch/` | Which corral each icon belongs to while The Ranch is closed (`parked.json`, plus a `.prev` backup) |
| `~/.config/autostart/org.theranch.desktop` | Start at login (toggle from the tray menu) |

## Troubleshooting

- **What happened at start-up or shutdown?** Each run logs how many icons it put back
  on the desktop or collected into corrals:
  `journalctl --user | grep "theranch:"`
- **Icons missing from the desktop?** Run `theranch --restore-desktop`, or look in
  `~/Desktop/.ranch/` in Dolphin.
- **Plain-looking buttons and menus?** Install `qqc2-desktop-style`.
- **Nothing appears at all?** Check you're in a Plasma *Wayland* session
  (`echo $XDG_SESSION_TYPE` should print `wayland`).

## Reporting bugs

Please open an issue at <https://github.com/localzuk/TheRanch/issues>. It helps to
include your Plasma and Qt versions and the output of
`journalctl --user | grep "theranch:"`.

## Known limitations

- At login, icons show on the desktop for a moment until The Ranch starts and gathers
  them into their corrals.
- Turning on *Show hidden files* for the desktop reveals the `.ranch` folder.
- Icons show their icon, not file thumbnails. They're sorted by name; you can't
  arrange them by hand within a corral yet.
- Inside drawers that sit above windows, the colour and image pickers are Qt's own
  (KDE's native pickers would open hidden behind the drawer), so they don't fully
  match the Plasma theme.

## Copilot use

- Copilot is only used for the desktop app to push the code to GitHub, for convenience.

## Licence

Copyright © 2026 Tony Ayre <me@tonyayre.net>

The Ranch is free software, licensed under the **GNU General Public License,
version 3 or (at your option) any later version** (GPL-3.0-or-later). Every file
carries an SPDX licence identifier saying so. The full licence text is in
[LICENSE](LICENSE).
