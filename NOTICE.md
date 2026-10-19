# Technical references and attribution

This project contains an original, purpose-built implementation. The following
projects are used as external tools or technical references:

- [Record of Lodoss War modern port](https://github.com/GeneralAtrox/RecordOfLodossWar_DC_ModernSourcePort), same
  author: project conventions, and host, frame-pacing and movie-playback code
  reused as the port grows.
- [xemu](https://github.com/xemu-project/xemu) via the
  [xemu_decomp](https://github.com/GeneralAtrox/xemu_decomp) fork,
  GPL-2.0-or-later: reference emulator for runtime observation.
- [ghidra-xbe](https://github.com/XboxDev/ghidra-xbe): XBE loader for Ghidra,
  fetched into the ignored `.tools` directory at a pinned commit and built
  locally; not vendored.
- [XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase):
  Xbox SDK signature database, fetched at a pinned commit and built locally;
  not vendored.
- [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded): reading
  reference for Xbox D3D8/DirectSound behaviour only.

XDVDFS and XBE layouts are implemented from their publicly documented
structures (see the [Xbox Dev Wiki](https://xboxdevwiki.net)). No game assets
from any of these projects are included.

## Not affiliated

This is an unofficial fan project, not affiliated with, endorsed or sponsored
by Genki, Phantagram or any other rights holder of *Phantom Crash*, by
Microsoft, or by the rights holders of the other games the optional importers
read (*S.L.A.I.* and *MechAssault*). Their names, characters and trademarks
belong to their owners; Xbox is a trademark of Microsoft.

## Game content

The game's data, pictures, models, sounds, movies and text are installed from
the player's own disc and are not part of the repository or release builds.
The data files hold the game's text only as references (`{xbe:0x...}`) to
strings in the player's `default.xbe`, which the extractor reads into
`tables/text.json` during installation. What the data files do record is the
port's own description of how the original works: layout coordinates, timing,
tuning values and addresses found by analysing the executable, plus short menu
and stat labels (such as BUY or Lock On Range) that the port draws in place of
the original's lettering. The port-added CHIPs, their help text and their face
pictures are the project's own.

## Executable icon

The executable icon (`port/res/phantomcrash.ico`, with its preview
`phantomcrash.png`) contains a render of game artwork: the Plain Holy robot's
model and textures from Phantom Crash, rendered by the port from an extracted
disc (`--render-robot`) and laid out by `tools/make_icon.py`. That artwork
belongs to the game's rights holders. The tile, the "PC" lettering and the
layout are this project's. It is the only game artwork in the repository and
in release builds; all other game data is installed from the player's own
disc.

## Included rendering library

[SMAA](https://github.com/iryoku/smaa), by Jorge Jimenez, Jose I. Echevarria,
Belen Masia, Fernando Navarro and Diego Gutierrez, is included under its MIT
license. The pinned shader and precomputed area/search lookup headers are in
`port/third_party/smaa`, together with the unmodified license and revision.

## Optional S.L.A.I. installer

The Windows installer executable bundles Python, NumPy and Pillow, packaged
with PyInstaller. Their copyright notices and license terms, including the
PyInstaller bootloader exception and NumPy's bundled library notices, are
included in release packages under `licenses/slai-installer`. Build versions
are pinned in `tools/slai-installer-requirements.txt`.
