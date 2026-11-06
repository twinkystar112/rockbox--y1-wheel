# Y1 Wheel + SONORA: install and roll back

Built from the `y1-wheel` branch by `.github/workflows/build-wheel.yml`. The process is the same
as for the Rockbox-Y1 nightlies (same toolchain, same scripts, same file layout).

**Status: beta, not yet tested on a real Y1.** The code compiles for the Y1 and has been run in the
Rockbox simulator at 480×360. Back up before installing, and keep the official files at hand so you
can go back.

## Files

| File | What it is for |
|---|---|
| `update_360p.zip` | Normal install/update on a Y1 that already runs Rockbox-Y1 |
| `rockbox.apk` | Only for manual (adb) installs; it is also inside the update zip |
| `rom_360p.zip`, `rom_type_b_360p.zip` | Full flash with MTKClient / SP Flash Tool |
| `SHA256SUMS.txt` | Checksums of the files above |

## Before you start: back up

1. Connect the Y1 by USB and copy the whole `.rockbox` folder to your computer. It holds your
   settings (`config.cfg`), SONORA progress and bookmarks, the Last.fm queue and your saved looks.
2. Download the official Rockbox-Y1 `update_360p.zip` (and, to be safe, its `rom_360p.zip`) from
   <https://github.com/rockbox-y1/rockbox/releases>. That is your way back.

The update never deletes your music, audiobooks, podcasts or anything else on the card. It copies
the new program files and replaces the bundled themes, fonts and languages in `.rockbox`.

## Install (same as Rockbox-Y1)

1. Copy `update_360p.zip` into the `.rockbox` folder on the Y1 and rename it to `update.zip`.
2. Eject the Y1 safely and unplug it.
3. On the Y1: **Main menu › Extras › System › Firmware Update**. On plain Rockbox-Y1 it's
   **Main Menu › System › Firmware Update**.
4. The update runs in the background and the Y1 restarts by itself when it is done.
5. If you came from plain Rockbox-Y1, your old `config.cfg` keeps the old theme. To start with
   the Wheel defaults, delete `.rockbox/config.cfg` (you backed it up) and restart.

First install on a Y1 without Rockbox, or no "Firmware Update" entry: use the full flash below.

## Full flash (fresh install)

Follow Rockbox-Y1's `INSTALLATION.md` (MTKClient or SP Flash Tool) with `rom_360p.zip`, or
`rom_type_b_360p.zip` for type B units. Use the same type as the official ROM you would flash on
your unit. A full flash rewrites the system and app-data partitions.

## Roll back

* **Back to plain Rockbox-Y1:** install the official `update_360p.zip` exactly as above. Then put
  your backed-up `config.cfg` back in `.rockbox`. The leftover Wheel themes and fonts don't get in
  the way.
* **Back to the Innioasis player:** **Extras › System › Switch to Stock Firmware**, the same
  option as in Rockbox-Y1.
* **If Rockbox no longer starts:** flash the official `rom_360p.zip` with MTKClient or SP Flash
  Tool, as described in Rockbox-Y1's `INSTALLATION.md`.
