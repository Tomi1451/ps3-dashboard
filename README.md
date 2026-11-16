# PS Dashboard

**Your whole PlayStation collection in one app on your PS3.** PS Dashboard puts your PS1, PS2 and PS3 games in one place, with their covers. It starts them straight from your hard drive or USB drive and installs games from disc. It has PlayStation-themed animated backgrounds and is built to be used with the controller.

It's a single app. It has its own game loader and FTP server, so you don't need to install webMAN MOD, multiMAN or anything else alongside it.

> Not affiliated with or endorsed by Sony Interactive Entertainment. "PlayStation", "PS1", "PS2" and "PS3" are trademarks of Sony Interactive Entertainment. No Sony code, BIOS, fonts or game files are included.

---

## Requirements

* **A PS3 with custom firmware (CFW) or PS3HEN, with Cobra enabled.** Cobra is the part of the firmware that loads game images; most CFW and HEN setups have it on already.
* **PS2 ISOs need CFW.** PS3HEN can't play PS2 ISOs; that's a limit of HEN, not of this app.

## Install

1. Download `PS_Dashboard.pkg` from the [Releases](../../releases) page.
2. Copy it to a USB stick, put the stick in the PS3, and install it with **Game → Install Package Files**.
3. Start **PS Dashboard** from the Game column of the XMB.

## Where to put your games

PS Dashboard looks on the internal hard drive (`/dev_hdd0`) and on USB drives (`/dev_usb000`–`/dev_usb007`), in these folders:

| Console | Folder | Formats |
|---|---|---|
| PS1 | `PSXISO` | `.bin` + `.cue`, `.iso`, `.img` |
| PS2 | `PS2ISO` | `.iso`, `.bin` |
| PS3 | `PS3ISO` | `.iso` (split `.iso.0`, `.iso.1`… on FAT32 drives) |
| PS3 | `GAMES`, `GAMEZ` | game folders (with `PS3_GAME` inside) |

Games can sit straight in the folder or in their own subfolder. Multi-disc games (Disc 1, Disc 2…) are grouped into one entry, and you pick the disc when you start it.

## Features

**Library**
* PS1, PS2 and PS3 tabs, plus **Home** with *Recently played* and *Favourites*.
* Three views: cover grid, list, or one large cover at a time.
* Covers drawn at each console's real case shape, never stretched.
* Search with an on-screen keyboard, A–Z / Z–A sorting, jump between letters, and favourites.
* A game info page with synopsis, developer, publisher, release date, genre and players. The info comes from the PC tool below.

**Playing**
* **PS3 games** (ISOs and game folders) start straight from the dashboard, using a game's installed update if it has one.
* **PS1 games** start straight from the dashboard too, using your own PS1 memory cards from the XMB's Memory Card Utility. You can choose which card in Settings.
* **PS2 games** are loaded, and you start them from the disc icon on the XMB.

**Discs**
* Put a disc in and choose what to do:
  * **PS3 discs:** play, or **install to the hard drive** (copied into `GAMES`, with progress and a free-space check).
  * **PS2 discs:** play, or **make an ISO** in `PS2ISO`.
  * **PS1 discs:** play. Making a PS1 ISO is blocked on some consoles.
* Eject from Settings.

**Look and feel**
* Three animated background styles:
  * **PS1:** shaded spheres.
  * **PS2:** floating cubes and light trails.
  * **PS3:** the XMB wave with sparkles.
* Background colours:
  * **Classic:** each style's own colours.
  * **Original:** changes with the date and gets darker at night, like the real XMB.
  * **12 fixed colours.**
* Brightness and animation level for the background.
* The PS3's own system fonts (Seurat, Rodin, Matisse, VAG Rounded and more).
* Optional navigation sounds, and dimming when idle.

**Settings** (SELECT): tabs for Appearance, Library, Starting games, Discs, System and Power, switched with L1/R1. The System tab shows:
* free space
* CPU and RSX temperatures
* firmware and Cobra version
* the FTP address
* game counts and USB drives

Power lets you restart or turn off the console.

**Built-in FTP server**: runs on port 21 while PS Dashboard is open, so you can copy games and covers over your home network.

## Controls

| Button | Action |
|---|---|
| D-pad | Move |
| ✕ | Play |
| ○ | Back / close |
| △ | Game info |
| □ | Add or remove favourite |
| L1 / R1 | Switch tab (Home, PS1, PS2, PS3), or switch tab in Settings |
| L2 / R2 | Jump to the next / previous letter |
| L3 | Search |
| R3 | Change view |
| SELECT | Settings |
| START | Exit to the XMB |

## PC tool: covers and game info

The `PS_Dashboard_PC_Tool` folder (Windows, needs Python 3 and Pillow) works over the network while PS Dashboard is open on the PS3. Find the PS3's address in **Settings → System → FTP server**.

* **GET_COVERS.bat:** finds every game on the PS3, downloads its cover and uploads it.
* **REPLACE_COVERS.bat:** the same, but replaces covers you already have.
* **GET_GAME_INFO.bat:** adds synopses, developers and release dates from the LaunchBox Games Database.

Everything is downloaded on your own PC; no artwork or game data comes with the app.

## Known limitations

* **PS1 Make ISO:** some consoles refuse the raw disc reads PS1 discs need, so it fails there. Playing PS1 discs works.
* **PS2 Classics (`.BIN.ENC`)** aren't supported yet; use the game's ISO.
* **PS2 games** start from the XMB disc icon rather than straight from the dashboard.
* **The FTP server only runs while PS Dashboard is open.**

## Troubleshooting

PS Dashboard writes a log to `/dev_hdd0/tmp/ps_dashboard.log`. If something doesn't work, please attach it to a [GitHub issue](../../issues), with your console model and firmware (CFW or HEN, and version).

## Building

GitHub Actions builds the PKG on every push (`.github/workflows/build.yml`), using the ps3dev toolchain, PSL1GHT and ps3libraries. The PKG must be built with the June 2026 toolchain (GCC 7.2); the newer nightly (GCC 13 + binutils 2.42) hangs the RSX on real hardware.

To preview the screens on a PC, run `tools/preview/preview.sh "<buttons>" "<frames>"`. It compiles the real UI code against PC stand-ins for Tiny3D/libfont and saves frames as PNGs (needs gcc, FreeType, Python, Pillow and numpy). Buttons are one per frame: `U D L R X O T S [ ]`, with `.` for none.

## Credits

* **PSL1GHT:** the open-source PS3 SDK (controllers, sound, network, files, graphics, JPG/PNG decoding).
* **Tiny3D and libfont** by Hermes and wargio: graphics and text.
* **FreeType:** font rendering. **zlib:** used by the PNG decoder.
* **ps3toolchain / ps3libraries:** the compiler and libraries used to build the PKG.
* **Cobra** (Team Cobra, maintained in CFWs by aldostools, Evilnat and others): PS Dashboard loads games through Cobra's published syscall interface.
* **IRISMAN:** PS1 direct start uses the same launch settings as IRISMAN. IRISMAN isn't needed and none of its code is included.
* **Cover sources** used by the PC tool: aldostools' resources (PS3), xlenore's ps2-covers and psx-covers.
* **LaunchBox Games Database:** game info used by the PC tool.

## Licence

*To be chosen before release (for example MIT or GPL-3.0); see the `LICENSE` file once added.*
