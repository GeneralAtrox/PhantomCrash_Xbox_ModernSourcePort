# Ghidra and reverse-engineering toolchain

The Ghidra tooling is shared by every Xbox game repository and lives in its
own repository, `C:\Projects\Xbox\xbox-re-tools`; its README documents the
scripts. SDK-wide outputs (the XDK libraries and headers, the library
project, the Function ID database and the header archive) are shared under
`C:\Projects\Xbox\XDK-tools\<build>`. This repository keeps only the Phantom
Crash project and its results, in ignored `research-output\ghidra`.

Game data, external source checkouts, Ghidra projects and analysis logs stay
ignored; only reproducible setup, version pins and conclusions belong in Git.

## Baseline

| Component | Location / version | Role |
|---|---|---|
| Ghidra | `C:\Tools\ghidra_12.1.2_PUBLIC_20260605\ghidra_12.1.2_PUBLIC` / 12.1.2 | Static analysis and decompilation of the x86 guest code |
| Java | `C:\Program Files\Microsoft\jdk-21.0.11.10-hotspot` / 21 | Explicit Ghidra runtime; the default `java` on `PATH` is Java 8 |
| xbox-re-tools | `C:\Projects\Xbox\xbox-re-tools` | Shared scripts; pins ghidra-xbe and XbSymbolDatabase in `toolkit.json` |
| XDK 4627 and 5849 artifacts | `C:\Projects\Xbox\XDK-tools\<build>` | SDK libraries and headers, `xdk<build>.fidb`, `xdk<build>.gdt` |
| GhidraMCP | installed Ghidra extension | Optional interactive inspection |
| radare2 | `C:\Tools\radare2-6.1.8-w64\bin\radare2.exe` | Independent cross-check |
| xemu_decomp | `C:\Projects\Xemu\xemu_decomp` | Runtime evidence (reference emulator) |

## Phantom Crash project

One-time toolkit setup, then onboarding with this game's paths:

```powershell
$tools = 'C:\Projects\Xbox\xbox-re-tools\scripts'
pwsh -NoProfile -File $tools\setup-ghidra-tools.ps1
pwsh -NoProfile -File $tools\build-ghidra-xbe.ps1 -Install

$game = @{
    Executable = '.\artifacts\europe\files\default.xbe'
    ProjectDirectory = '.\research-output\ghidra'
    ProjectName = 'phantomcrash-europe'
}
& $tools\onboard-xbe.ps1 @game -SdkVersion 5849          # report only
& $tools\onboard-xbe.ps1 @game -SdkVersion 5849 -Apply   # name, type, save
& $tools\onboard-xbe.ps1 @game -SdkVersion 4627 -Apply   # fill gaps, retype
```

Two XDK builds are available; the game linked 4721, which was never
released. The project was named from 5849 first and then from 4627, which
only names functions 5849 left unnamed. Prototypes come from 4627, the last
run. For a new project, use 4627 first (see the toolkit README).

The project, created by `analyze-xbe.ps1`, is
`research-output\ghidra\phantomcrash-europe.gpr`. The function summary is in
`phantomcrash-europe-summary\{summary.json,functions.csv}`, and the Function
ID and prototype reports of the last run are in `phantomcrash-europe-xdk-fid`
and `phantomcrash-europe-xdk-signatures`. Results are in
[the European baseline](EUROPE_BASELINE.md#xdk-function-id). The XBE has no
`.XTLID` section, so the loader's empty XTLID table loses nothing.

Decompile chosen functions (append `+` to include direct callees), bounded
by a timeout:

```powershell
analyzeHeadless.bat research-output\ghidra phantomcrash-europe -process default.xbe `
  -noanalysis -readOnly -scriptPath C:\Projects\Xbox\xbox-re-tools\ghidra_scripts `
  -postScript DecompileFunctions.java <output-dir> 2c460+ a2268
```

The script creates a function where none exists, which covers task callbacks
reached only through data pointers; with `-readOnly` those functions are not
saved.

## Naming provenance

SDK names from XbSymbolDatabase and XDK Function ID are library
identifications, not recovered game-source symbols. Function ID names come
from SDK builds (4627, 5849) other than the one the game linked (4721); the
plate comment naming the build and the bookmark mark them. Game-owned functions remain unnamed until
their behaviour is understood; decompiler output is a model of machine code,
not the original source.
