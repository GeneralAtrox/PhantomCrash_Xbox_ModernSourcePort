# Repository Working Agreement

These rules are inherited from the Record of Lodoss War project
(`C:\Projects\Lodoss`) and adapted for the Original Xbox.

## Implementation workflow

- Proceed with the requested work without mandatory evidence contracts,
  provenance audits, or repository-wide preflight and test gates.
- Use checks appropriate to the change. Unrelated historical evidence or test
  failures do not block implementation, builds, or visual review.
- Do not change recorded hashes merely to make a check pass.
- Recreate the original’s observable behaviour and visual presentation so they
  are indistinguishable in equivalent play situations. Byte-for-byte
  implementation is unnecessary. Nuance is required functionality, not optional
  polish.
  Deterministic replay is not a goal. The user reviews visuals and behaviour
  personally; no automated side-by-side comparison against xemu is required.

## Agent usage

- Do not use subagents for implementation or other longer tasks; the main
  session does the work directly. Start a subagent only when the user asks for
  one.
- When the user does ask for subagents: at most 3 at a time, exactly one task
  each, no helper agents of their own, and a fresh agent with a written
  handoff (`docs/AGENT_HANDOFF.md`) rather than resuming a large-context agent.

## Context hygiene

- Filter command output before it reaches the context: `tail`, `grep`, `wc`,
  counts or pass/fail summaries. Never print whole build logs, test runs,
  Ghidra logs or generated JSON/CSV; write large results to a file and read a
  summary or the failing lines.
- Search first, then read only the needed line range of a file.
- Never open extracted assets, generated catalogues or function exports whole;
  query them with a short script that prints only the fields needed.
- Stay within the one task. Record unrelated findings in the final report
  instead of fixing them.
- Bound every command that can hang with a timeout. Prefer foreground,
  timeout-bounded commands over background ones.

## Code organization

- Keep new or refactored handwritten source and header files at or below 400
  physical lines. Use normal readable formatting; do not compress statements
  to satisfy the limit. Generated tables and content data are excluded.
- Split by responsibility and ownership, not arbitrary line ranges. Moving
  methods into another file alone does not remove their shared-state coupling.
- Follow `.clang-format` for C++ (Visual Studio's bundled clang-format at
  `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin`).
  See `docs/CODE_ORGANIZATION.md`.

## Test-build cleanup

- An agent that creates a test build, temporary runtime, capture run, or other
  generated test output must remove it before the final handoff. This includes
  agent-created content under `build/`, `research-output/`, `.tmp/`, `tmp/`, and
  `research-runtime-*` unless the user explicitly asks to retain it.
- The standard port build (`build/port`), the extraction (`artifacts/`) and the
  Ghidra project created by the xbox-re-tools `analyze-xbe.ps1` /
  `onboard-xbe.ps1` are working outputs, not test output; keep them. The same
  holds for the shared XDK artifacts under `C:\Projects\Xbox\XDK-tools`.
- Before the final handoff, verify that the agent-created paths were actually
  removed and report any cleanup failure as a blocker.

## Modern port requirements

- C++20, SDL3 host, Direct3D 11 renderer, FFmpeg for Sofdec movies; dependencies
  through vcpkg at the same baseline as Lodoss (`scripts/build-port.ps1`).
- Reuse Lodoss host, frame pacing, movie playback and UI behaviour code where it
  fits, instead of rewriting it. Dreamcast-specific code (AICA, PowerVR, Maple,
  VMU, capture contracts) does not carry over.
- Render at full native resolution with widescreen support; never limit output
  to 640x480. 2D UI is authored in the original 640x480 coordinates and drawn
  resolution-independently.
- Frame cap up to 144 Hz with an unlimited option. Game logic runs at a fixed
  rate independent of the presentation rate.
- Boot movies: tapping a button skips to the next movie; natural playback
  transitions smoothly between movies. There is no 50/60 Hz or language prompt.

## Source and moddability goals

- The port is readable, hand-written C++ that reimplements the game. It is not
  a static recompilation or a wrapper that runs original machine code; the
  xboxrecomp spike (`docs/RECOMP_SPIKE.md`) was evaluated and not adopted.
- The purpose is to change the game freely: swap intro movies, change menu
  backgrounds, add new robot types, mix robot parts, rebalance. Design for that:
  - Content and tuning live in data, not code: movie playlists, UI screens and
    their images, robot/part/weapon definitions, part compatibility, and every
    balance value (stats, prices, damage, AI parameters) load from editable
    text files. Name constants recovered from the original; no magic numbers.
  - Extraction converts the user's disc into open formats the port loads: PNG
    textures, glTF models with skeletons and animations, WAV/OGG audio, JSON or
    TOML tables. Understand formats well enough to write them, not only read
    them, so new robots can be authored in standard tools.
  - A mod folder overlays the extracted base data path-for-path, so changes
    never require editing the extraction.
  - Defaults reproduce the original game's behaviour; changes are opt-in data.

## Original Xbox reverse-engineering toolchain

- Static analysis: Ghidra 12.1.2 at
  `C:\Tools\ghidra_12.1.2_PUBLIC_20260605\ghidra_12.1.2_PUBLIC`, using the
  Java 21 runtime at `C:\Program Files\Microsoft\jdk-21.0.11.10-hotspot`. Do not
  rely on the default `java` on `PATH`, which may be an older runtime.
- The Ghidra scripts live in the shared toolkit repository
  `C:\Projects\Xbox\xbox-re-tools`: `setup-ghidra-tools.ps1` and
  `build-ghidra-xbe.ps1 -Install` prepare the pinned XBE loader;
  `onboard-xbe.ps1` analyses the XBE and names and types its SDK code from the
  shared XDK artifacts in `C:\Projects\Xbox\XDK-tools`. Fix toolkit bugs
  there, not with copies here. See `docs/GHIDRA_TOOLING.md`.
- Keep four name provenances distinct in research notes: names embedded in the
  shipped data (asset paths, middleware version strings), deterministic
  extractor filenames, XbSymbolDatabase SDK matches, and researcher-assigned
  semantic names. An SDK match is not an original game-source symbol.
- GhidraMCP may be used for focused interactive inspection. Use radare2 as an
  independent static cross-check.
- Runtime evidence comes from the xemu fork at `C:\Projects\Xemu\xemu_decomp`
  (github.com/GeneralAtrox/xemu_decomp). Use xemu as the reference emulator;
  Cxbx-Reloaded is not used as an emulator, though its D3D8/DirectSound source
  is useful reading for Xbox-versus-PC API differences.
- x64dbg (`x32dbg`) applies to Windows host executables and to a future 32-bit
  native test harness that runs original XBE functions; it does not debug the
  guest inside xemu.
- Never commit external tool checkouts, Ghidra project databases, the disc
  image, the XBE, extracted assets, BIOS/MCPX/EEPROM/HDD images, SDK libraries,
  or generated analysis logs.

## MCP placement contract

1. All MCP implementation, manifests, docs, dependency declarations, and MCP
   tests belong under `MCP/`.
2. MCP tools must reuse the project's Python functions directly; no
   arbitrary-shell escape hatch.
3. Launch tools accept allowlisted profile IDs only, never caller-provided
   paths or ports.
4. Enforce owned runtimes, bounded timeouts, process ownership, and cleanup.
