# PS Dashboard

A PlayStation-style game library for the PS3: your PS1, PS2 and PS3 games as cover grids, in the classic PS3 blue.

**Stage 1 (this build)**
- PS1 / PS2 / PS3 libraries read from the internal HDD (`/dev_hdd0/PSXISO`, `PS2ISO`, `PS3ISO`, `GAMES`, `GAMEZ`)
- Cover grid with case shapes (PS1 square, PS2 DVD, PS3 Blu-ray), covers never stretched
- Covers: `<game name>.JPG` next to each ISO/BIN (made by the PS Dashboard Cover Art Tool), multiMAN `covers/<ID>.JPG`, or the game's `ICON0.PNG`
- Game info panel (Triangle)
- Play (Cross) - webMAN MOD mounts and starts the game in the background

**Controls**: D-pad move · Cross play · Triangle info · Circle back · L1/R1 switch console · START exit

## Install
Download `PS_Dashboard.pkg` from the **Releases → latest** page, copy it to a USB stick, and install it with *Install Package Files* on the PS3. Needs webMAN MOD for launching games.

## Build
GitHub Actions builds the PKG on every push (`.github/workflows/build.yml`) with the ps3dev toolchain, PSL1GHT and ps3libraries (Tiny3D, FreeType).

Fonts: uses the PS3's own system font; falls back to the bundled DejaVu Sans (see `data/DEJAVU-LICENSE.txt`).

## Previewing the UI on a PC
`tools/preview/preview.sh "<buttons>" "<frames>"` compiles the real UI code against PC stand-ins for Tiny3D/libfont and renders frames to PNG (needs gcc, FreeType, Python + Pillow + numpy). Buttons, one per frame: `U D L R X O T S [ ]`, `.` = none. Example: `tools/preview/preview.sh "....RR....T" "5,15"`.

Note: the PKG must be built with the June 2026 toolchain (GCC 7.2). The newer nightly (GCC 13 + binutils 2.42) hangs the RSX on real hardware.
