# European baseline

Identity and first observations for the user-supplied European disc. Values
come from `python tools\phantomcrash.py extract` (`artifacts\europe\metadata`)
and the first headless Ghidra analysis.

## Disc image

| Field | Value |
|---|---|
| File | `C:\Projects\Xbox\PhantomCrash\Phantom Crash (Europe).xiso.iso` |
| Size | 2,724,462,592 bytes |
| SHA-256 | `85a8fc3bf2cc7dddbe285574b386ba7639dd1a73b4cd0d6cf9abf323f869664f` |
| Layout | Trimmed xiso; XDVDFS partition at offset 0 |
| Files | 309 files, 2,723,440,970 bytes |

## default.xbe

| Field | Value |
|---|---|
| SHA-256 | `bdbe2c3eb2361395babfe6095d1a2da72f6581dccb34812b8d45e084c6c903c6` |
| Title | `phantomcrash`, title ID `0x504C0001` (`PL-001`), region flags `0x4`, version `0x4` |
| Built | 2002-10-16 21:51 UTC, retail |
| Base / entry | `0x00010000` / `0x000d8799` |
| Kernel thunk table | `0x0017a780` |
| Debug path | `K:\Release\citydiver.exe` (release build, no PDB, no RTTI) |
| XDK | build 4721: XAPILIB, D3D8, D3DX8, XGRAPHC, DSOUND, XBOXKRNL, LIBCMT |

Sections: `.text` (1.05 MiB), `D3D`, `D3DX`, `XGRPH`, `DSOUND`, `PSGSFD00`,
`PSGSFD_I`, `PSGSFD_B`, `PSGSFD_P`, `XPP`, `.rdata`, `.data` (11.4 MiB virtual,
2.2 MiB raw), `DOLBY`, `EVENTSEG` (1.5 MiB; marked executable but holds
16-bit data tables, see [the recompilation spike](RECOMP_SPIKE.md)),
`$$XTIMAGE`, `$$XSIMAGE`.

Statically linked middleware identified by version strings (shipped-data
names): CRI ADX/Sofdec for Xbox (ADXT 8.11, MWSFD 2.16, SFD 1.802, built
July 2002), IJG libjpeg, libpng and zlib.

## First Ghidra analysis

`analyze-xbe.ps1` (xbox-re-tools) with the ghidra-xbe loader found 6,503
functions. XbSymbolDatabase named 294 of them (D3D8 132, DSOUND 112, XAPILIB
45, XGRAPHC 5) plus 9 kernel imports; the standalone CLI reports 349 SDK
symbols including variables.

| Section | Functions | SDK-identified | Bytes |
|---|---:|---:|---:|
| `.text` | 5,203 | 35 | 998,023 |
| `D3D` | 263 | 129 | 53,868 |
| `D3DX` | 399 | 0 | 101,649 |
| `DSOUND` | 397 | 112 | 40,311 |
| `XPP` | 193 | 13 | 25,621 |
| `XGRPH` | 32 | 5 | 7,358 |
| `PSGSFD*` | 13 | 0 | 6,520 |

`.text` still contains the C runtime, CRI middleware, libjpeg, libpng and zlib
alongside the game code. Separating those is the next triage step.

### XDK Function ID

A Function ID database hashed from XDK 5849 libraries
([tooling](GHIDRA_TOOLING.md#phantom-crash-project)) named 760 more functions. The
SDK build differs from the game's 4721, yet all 246 functions that both
XbSymbolDatabase and Function ID identified carry the same name. The five
apparent conflicts are spellings of one function: `entry` for
`mainCRTStartup`, `??2@YAPAXI@Z` for `operator new`, XbSymbolDatabase's
`CDevice_SetStateVB`/`SetStateUP` for `LazySetStateVB`/`LazySetStateUP`, and
a constructor. 102 further matches had several candidate names and were left
unnamed.

| Section | Functions | SDK-identified before | After |
|---|---:|---:|---:|
| `.text` | 5,203 | 35 | 179 |
| `D3D` | 263 | 129 | 198 |
| `D3DX` | 399 | 0 | 245 |
| `DSOUND` | 397 | 112 | 279 |
| `XPP` | 193 | 13 | 131 |
| `XGRPH` | 32 | 5 | 25 |

New names by library: D3DX8 243, XAPILIB 186, DSOUND 167, D3D8 71, LIBCMT 61,
XGRAPHC 31, LIBCPMT 1. The `D3DX` section includes D3DX's own copy of IJG
libjpeg (`jpeg_std_error`, `D3DX_decode_mcu`). Only 61 LIBCMT functions
matched in `.text`; the rest of the C runtime there does not hash like the
5849 library.

A second database from XDK 4627 (June 2002, closer to the game's 4721) then
named 407 functions 5849 had missed: D3DX8 115, DSOUND 107, XAPILIB 84,
D3D8 61, LIBCMT 34, XGRAPHC 6. SDK-identified functions rose from 1,057 to
1,465 of 6,503, and in `.text` from 179 to 261. Of the functions both builds
identified, 1,083 carry the same name and 17 differ:
- spellings (`entry`, `operator new`, `D3D_MakeSpace`);
- compiler-generated `$L` labels whose numbers changed between builds;
- identical functions the linker merged, where either name fits (`png_chunk_warning`/`png_chunk_error`, `XInitDevices`/`USBD_Init`).

Existing names were kept. 93 matches stayed ambiguous.

### XDK header prototypes

The 5849 headers parse into 10,144 data types and 2,317 function prototypes
([tooling](GHIDRA_TOOLING.md#phantom-crash-project)). 240 of the 1,047
SDK-identified functions are public API with a header prototype (D3D8 105,
XAPILIB 73, DSOUND 35, LIBCMT 12, D3DX8 8, XGRAPHC 7); the other 807 are
library internals such as `D3D_CommonSetViewport` and `CMcpxAPU_*`. For 236 of
the 237 whose `RET n` is known, the prototype's stack cleanup matches the
compiled code. The exception is `D3DDevice_SetVertexShaderConstant`: the
game's copy is `__stdcall` with 12 bytes of arguments, while the 5849 header
declares it without that convention. It was skipped; the other 239 prototypes
were applied.

The 4627 headers (9,917 data types, 2,268 prototypes) then retyped the
SDK-named functions after the 4627 Function ID pass: 271 prototypes applied,
and `D3DDevice_SetVertexShaderConstant` again skipped, so 4627 declares it the
same way.

## Disc layout

| Directory | Files | Contents |
|---|---:|---|
| `/` | 1 | `default.xbe` |
| `Media/movie` | 11 | Sofdec `.sfd`: `phantagram`, `dolby`, `kudryavka`, `opening`, `opening02`, `phantomcrash`, `cloud`, `staff`, `ending`, `epilogue`, `the_end` |
| `Media/scroll` | 86 | 2D texture packs for menus and screens (`title_tex`, `logo_tex`, `system_tex`, ...) |
| `Media/binlink` | 16 | Linked model/stage packs: robots `a00_holy`, `a01_photon`, `a02_aeron`, `a03_drones`; stages `b00_shinjuku`, `b01_shibuya`, `b02_umihotaru`; effects; enemies `d00_tolomeo`, `d01_slime`, `d03_charga`; texture banks `z_robotex_*`; `zz_emblemtex`; `zzz_music` |
| `Media/model`, `Media/texture` | 17, 15 | Standalone models and textures (trophies, record cup, viewer) |
| `Media/sound` | 94 | Music as plain 16-bit 44.1 kHz stereo PCM `.wav`; effect banks `sys.bin`, `battle.bin`, `battle_player.bin`; DirectSound DSP image `dsstdfx.bin` |
| `Media/Shaders` | 53 | 28 `.xvu` vertex and 25 `.xpu` pixel shaders |
| `Media/Fonts` | 2 | `FONT.bin`, `FONT.res` |
| `Media/systemdata` | 8 | Small `.tga`/`.bmp` effect textures |
| `Media/rbdata`, `Media/enepath`, `Media/diskcheck` | 4, 1, 1 | Robot arm data, an enemy path, save image `SaveImage.xbx` |

The `scroll` texture packs and the `binlink` model packs share the same leading
bytes `02 01 00 80`, so one container parser is expected to cover both. The
executable refers to `binlink` files through the `Z:\` cache drive and to all
other files through `D:\Media\`.
