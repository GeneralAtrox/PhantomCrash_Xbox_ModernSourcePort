# Static-recompilation spike and prior-art survey (2026-09-26)

**Decision: not adopted.** The goal is modifiable source (new robots, part
mixing, rebalancing, replaceable movies and menus), which generated C does not
provide. The spike build was deleted; `.tools\xboxrecomp` stays only as an MIT
reading reference for texture formats, swizzling and NV2A shader translation.
The facts about the executable below still stand.

Question: can existing Original Xbox projects save work on this port? This
records a time-boxed run of [xboxrecomp](https://github.com/sp00nznet/xboxrecomp)
against the European `default.xbe` and a survey of related projects.

## Result in one paragraph

xboxrecomp v0.11.0 (commit `766ecefcd7fb2a9b344de8ec891f6fe9ea14261b`, MIT +
LGPL-2.1 for xemu-derived files) lifted the whole executable to C with no
failed functions, built a native x64 Windows executable, and ran the game's own
code through CRT and XAPI start-up, D3D device creation, loading of every
vertex/pixel shader, fonts, 2D packs, every `binlink` model/stage/texture pack
(copied to the `Z:` cache as on hardware), sound banks, and into the frame loop,
where it opens `Media\movie\` to start the first Sofdec movie. The NV2A path
executes real push buffers (clears and draws) but nothing visible yet. Reaching
that point took about two hours including tool setup. Incremental rebuilds take
~8 s; a full rebuild of the 1.1 M generated lines takes ~10 min.

## Pipeline numbers

| Stage | Result |
|---|---|
| XBE parse | 121 kernel imports; `tools.kernel_audit.coverage`: 121/121 handled by the runtime |
| Disassembly | 7,881 functions after seeding (Ghidra found 6,503) across `.text`, `D3D`, `D3DX`, `DSOUND`, `XPP`, `XGRPH`, `PSGSFD*` |
| Names | 410 names merged from our Ghidra/XbSymbolDatabase export (SDK, CRT, middleware) |
| Lift | 7,881/7,883 lifted, 0 failed, 2 hand-written; 1,119,157 lines of C; 135 unresolved direct-call targets stubbed |
| Build | MSVC 19.38, 18 MB `phantomcrash_recomp.exe` |

## Bring-up steps that were needed

1. `merge_names.py` renamed the entry function to `entry`; the template's
   `main.c` calls `xbe_entry_point`. Call `entry` instead.
2. `DOLBY` and `EVENTSEG` are marked executable but are data; clear their
   `executable` flag in the analysis JSON before disassembly.
3. The CRT `memcpy`/`memmove` at `0x00114A00` uses three jump tables the lifter
   does not recover; replaced by a native `memmove` (`--manual-functions`).
4. `D3D_BlockOnTime` (`0x00124E20`) waits for an NV2A fence nothing advances;
   stubbed for the probe (the real fix is D3D8 interception, below).
5. Indirect-only functions missing from both Ghidra and xboxrecomp were seeded:
   library static initialisers, the worker-thread routine `0x000B24B0`, 22
   functions found by scanning for 16-byte-aligned starts after `ret`+padding,
   and 18 Ghidra-only starts. Three CRI thread routines (`0x000E2850`,
   `0x000E28C0`, `0x000E93C0`) are in the seed list but not yet rebuilt.
6. Runtime switches: `RECOMP_VBLANK=1` (vblank interrupt; without it the frame
   loop blocks in `D3DDevice_BlockUntilVerticalBlank`), `RECOMP_PB_EXEC=1`
   (execute push buffers), `RECOMP_WATCHDOG_SECS`, `RECOMP_KERNEL_LOG_BUDGET`.

Current stop: the CRI movie server loops on `ObReferenceObjectByHandle`,
`KeSetBasePriorityThread` and `NtResumeThread` after its thread routines failed
to resolve (step 5).

## New facts about the executable

- `EVENTSEG` (1.5 MiB, flags writable+executable, not preloaded) is data: 73 %
  zero bytes, 16-bit values, no prologues. It is a candidate for motion/event
  tables and worth examining for the animation work.
- The boot copies the `binlink` packs to `\Device\Harddisk0\Partition5` (the
  `Z:` cache) and reopens them from there.
- Boot asset order: `ascii_font_sys_tex`, `common_2d_tex`, `fight_chara00/01_tex`,
  `grid_mdl`, all `.xvu`/`.xpu`, `binlink` packs, `systemdata` textures,
  `point_mdl`/`point_tex`, `sound\sys.bin`, `battle*.bin`, then `movie\`.
- Game initialisation calls `D3DDevice_Reset` from `sub_000B2B70`; the main
  loop is `sub_000A1973`, reached from `sub_000D872A` (the thread routine
  passed to `PsCreateSystemThreadEx` by `entry`).

## What it would take to use it

Rendering is the remaining large piece. The statically linked XDK D3D8 code is
lifted too, so it writes NV2A push buffers. Either interpret those (the
toolkit's xemu-derived NV2A path; resolution locked to the guest's surfaces) or
intercept the D3D8 API at the function level and draw natively. The second is
what the playable projects do, and our XbSymbolDatabase export already locates
132 D3D8 functions by address (161 D3D8 symbols including variables). Interception is also where widescreen, full
resolution and frame-rate control can be added.

## Survey

| Project | Kind | State (2026-09) | Relevance |
|---|---|---|---|
| [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) | Title-agnostic XBE→C recompiler + runtime (kernel, D3D8→D3D11, DirectSound, NV2A/APU from xemu) | v0.11.0, active, many forks | Spike above |
| [doaxbv-re](https://github.com/NoRain211/doaxbv-re) | DOAXBV on xboxrecomp; intercepts statically linked D3D8/DirectSound | "Fully playable" 0.4 beta; resolution, MSAA, SMAA options | Proof that D3D8 interception gives a playable, high-resolution result. GPL-3.0: read only |
| [jsrf-recomp](https://github.com/andeecollard/jsrf-recomp) | Jet Set Radio Future on xboxrecomp (macOS) with a D3D8 "lift" | Playable into chapter 2 | Closest analogue: 2002 Japanese custom engine. MIT |
| [hl2-recomp](https://github.com/sp00nznet/hl2-recomp), [xboxdashboard](https://github.com/sp00nznet/xboxdashboard), [fifastreet2-recomp](https://github.com/jv36/fifastreet2-recomp), [xboxrecomp-conker](https://github.com/codepdbh/xboxrecomp-conker), [black-macos-recomp](https://github.com/elpetese/black-macos-recomp) | Other xboxrecomp titles | Booting to early rendering | Pattern library for bring-up problems |
| [halo-re/halo](https://github.com/halo-re/halo) | Functional reimplementation patched into the XBE, tested in xemu | Main loop reimplemented | Incremental-replacement workflow; [csplit](https://github.com/punpckhdq/csplit) needs a PDB, which we lack |
| [nv2a_vsh_asm](https://github.com/abaire/nv2a_vsh_asm), [nv2a_vsh_cpu](https://github.com/abaire/nv2a_vsh_cpu), [nv2a-trace](https://github.com/XboxDev/nv2a-trace) | NV2A vertex-shader assembler, CPU reference, GPU tracer | Maintained | Offline `.xvu` conversion and verification |

No existing reverse-engineering, format documentation or decompilation of
Phantom Crash, its PS2 sequel *S.L.A.I.*, or other Genki titles was found.
