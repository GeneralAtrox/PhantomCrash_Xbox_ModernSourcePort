# Phantom Crash — Original Xbox modern port

A native Windows port of *Phantom Crash*, rebuilt in modern C++ with SDL3 and
Direct3D 11. You supply your own game disc: this repository contains no game
files, executable, text or Xbox firmware.

This is an unofficial fan project, not affiliated with, endorsed or sponsored
by Genki, Phantagram or any other rights holder of *Phantom Crash*, nor by
Microsoft. *Phantom Crash* and the names of its companies, characters and
robots belong to their owners; Xbox is a trademark of Microsoft.

## Requirements

**To play**, download the Windows x64 ZIP from
[Releases](https://github.com/GeneralAtrox/PhantomCrash_Xbox_ModernSourcePort/releases)
and run `Start Game.bat`. You need:

- Windows 10 or newer, with a Direct3D 11 graphics card
- your own *Phantom Crash* (Europe) Xbox disc image, trimmed XISO or full
  Redump. The game installs from it on first start (about 3.5 GB) and does
  not need it again.
- optional: your European *S.L.A.I.* PS2 disc image (SLES-52940), for the
  S.L.A.I. content pack
- optional, for LAN play: the game allowed through the firewall on private
  networks (UDP 29641 and 29642)

Nothing else to install: no Python, and the Visual C++ runtime comes with the
game. Setup, updating and release notes: [Windows release setup](docs/WINDOWS_RELEASE.md).

**To build from source:** Visual Studio 2022 with the C++ workload,
CMake 3.25+, PowerShell 7.4+ and Python 3.11+ (`pytest` for the tests).
The research tools also need Ghidra 12.1.2 and Java 21.

```powershell
pwsh -NoProfile -File .\scripts\build-port.ps1 -Test   # build and test
.\scripts\extract-game.ps1                             # install the game data from your disc
.\launch.bat                                           # rebuild if needed and play
```

## Improvements over the original

**Picture**
- Any resolution, windowed or fullscreen; widescreen 3D with a choice of 4:3
  or widescreen HUD (menus and movies stay 4:3)
- Frame-rate cap from 30 to 144 fps, or unlimited; VSync
- Optional dynamic lighting and shadows (with light beams on night stages),
  SMAA anti-aliasing and an "original softness" look; the original's depth
  blur, distortion and colour tint can be switched off

**Controls**
- Keyboard and mouse aiming as well as controllers; every key, button, stick
  and trigger can be rebound in Settings

**Multiplayer**
- LAN co-op: one player hosts from COOP, the others pick the game from a list
- Local split-screen co-op for up to four players (experimental, off by default)

**Game**
- A live RUMBLING: each area's contest fights on while you choose, and you
  join the battle as it stands (up to 16 robots an area)
- The weapon stats the original hides (fire rate, range, spread, homing and
  more) on a second page in the shops and ASSEMBLE
- New CHIPs (three cats and seven specialists) and three new money tiers in
  the supply drops
- The optional S.L.A.I. content pack, installed from your PS2 disc
- Cheats (all off by default) and a level select under Tools

**Modding**
- The screens, timings and rules are editable JSON in `data\`; a file in
  `mods\` replaces the original at the same path
- Experimental: MechAssault's weapon effects, converted from your own
  MechAssault disc

## Documentation

- [Using and developing the port](docs/PORT_GUIDE.md): extracting the disc,
  data and mods, command-line options, controls and every ported screen
- Features: [widescreen](docs/WIDESCREEN.md),
  [dynamic lighting](docs/DYNAMIC_LIGHTING.md),
  [post-processing](docs/POST_PROCESSING.md), [local co-op](docs/LOCAL_COOP.md),
  [S.L.A.I.](docs/SLAI.md), [custom CHIPs](docs/CUSTOM_CHIPS.md),
  [supply boxes](docs/SUPPLY_BOXES.md), [cheats](docs/CHEATS.md),
  [MechAssault effects](docs/MECHASSAULT_VFX.md)
- Research: [Ghidra tooling](docs/GHIDRA_TOOLING.md),
  [European baseline](docs/EUROPE_BASELINE.md); the tools and references used,
  including the [xemu_decomp](https://github.com/GeneralAtrox/xemu_decomp)
  emulator fork, are in [NOTICE.md](NOTICE.md)

## Legal boundary

Only process a disc you are entitled to use. Never commit or distribute the
disc image, `default.xbe`, extracted assets, the game's text,
BIOS/MCPX/EEPROM/HDD images, or Xbox SDK libraries. The ignore rules exclude
the default locations; game text in `data\` goes in as `{xbe:0x...}`
references (see [Data and mods](docs/PORT_GUIDE.md#data-and-mods)).
