"""Command-line entry point: ``python -m phantomcrash_research <command>``."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from . import stats_csv, xbe
from .convert import convert, update_robots, update_town
from .extract import extract
from .mechassault import arm_swap as mechassault_arm
from .mechassault import export as mechassault_export
from .slai import arm_weapon as slai_arm_weapon
from .slai.music import convert_music
from .slai.wheels import import_wheels
from .slai.assets import convert_assets
from .slai.native_parts import register_parts
from .slai.native_stages import register_stages
from .slai.native_cosmetics import register_cosmetics
from .xdvdfs import XdvdfsImage

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DISC = REPOSITORY_ROOT / "Phantom Crash (Europe).xiso.iso"
DEFAULT_OUTPUT = REPOSITORY_ROOT / "artifacts" / "europe"
DEFAULT_ASSETS = DEFAULT_OUTPUT / "assets"
DEFAULT_MODS = REPOSITORY_ROOT / "mods"


def _command_list(arguments: argparse.Namespace) -> int:
    with XdvdfsImage(arguments.disc) as image:
        for entry in image.files():
            print(f"{entry.size:>12,}  {entry.path}")
    return 0


def _command_xbe(arguments: argparse.Namespace) -> int:
    if arguments.file:
        data = Path(arguments.file).read_bytes()
    else:
        with XdvdfsImage(arguments.disc) as image:
            data = image.read(image.find("default.xbe"))
    print(json.dumps(xbe.parse(data).to_dict(), indent=2))
    return 0


def _command_extract(arguments: argparse.Namespace) -> int:
    report = None if arguments.quiet else print
    result = extract(Path(arguments.disc), Path(arguments.output), report)
    header = result["xbe"]
    print(
        f"Extracted {result['disc']['file_count']} files "
        f"({result['disc']['total_bytes']:,} bytes) to {arguments.output}\n"
        f"XBE {header['title_code']} '{header['title_name']}' XDK {header['xdk_build']} "
        f"sha256 {header['sha256']}"
    )
    return 0


def _command_convert(arguments: argparse.Namespace) -> int:
    report = None if arguments.quiet else print
    manifest = convert(Path(arguments.files), Path(arguments.output), arguments.replace, report)
    textures = sum(len(pack["textures"]) for pack in manifest["texture_packs"])
    effects = sum(len(bank["effects"]) for bank in manifest["sound_banks"])
    tables = manifest["tables"]
    print(
        f"Converted {textures} textures, {len(manifest['models'])} models, {effects} effects, "
        f"{tables['sprites']} sprites and "
        f"{tables['resources']} resources to {arguments.output}"
    )
    return 0


def _command_convert_robots(arguments: argparse.Namespace) -> int:
    report = None if arguments.quiet else print
    robots = update_robots(Path(arguments.files), Path(arguments.output), report)
    print(
        f"Converted {len(robots['models'])} robot models, {robots['textures']} paints and "
        f"{robots['motion_banks']} motion banks to {arguments.output}"
    )
    return 0


def _command_convert_mechassault_vfx(arguments: argparse.Namespace) -> int:
    summary = mechassault_export.convert(Path(arguments.disc), Path(arguments.output))
    print(
        f"Converted {summary['effects']} weapon effects, {summary['meshes']} meshes and "
        f"{summary['textures']} textures to {arguments.output}"
    )
    for label in ("skipped", "missing"):
        if summary[label]:
            print(f"{label}: {len(summary[label])}, e.g. {summary[label][:3]}")
    return 0


def _command_swap_mechassault_arm(arguments: argparse.Namespace) -> int:
    for model, source in ((arguments.right, arguments.right_arm), (arguments.left, arguments.left_arm)):
        if not model:
            continue
        directory, name = source.rsplit("\\", 1)
        summary = mechassault_arm.swap_arm(
            Path(arguments.disc),
            arguments.archive,
            directory,
            name,
            Path(arguments.assets) / model,
            Path(arguments.output),
            model,
        )
        print(f"Wrote {arguments.output}/{model}: {len(summary['parts'])} parts, scale {summary['scale']}")
    return 0


def _command_add_slai_arm_weapon(arguments: argparse.Namespace) -> int:
    summary = slai_arm_weapon.add_arm_weapon(
        Path(arguments.disc), Path(arguments.assets), Path(arguments.data), Path(arguments.output),
        Path(arguments.spec),
    )
    print(
        f"Added arm weapon {summary['part']} '{summary['name']}' (arm type {summary['arm_type']}, "
        f"{summary['triangles']} triangles) to {arguments.output}"
    )
    return 0


def _command_export_stats(arguments: argparse.Namespace) -> int:
    counts = stats_csv.export(Path(arguments.assets), Path(arguments.output))
    print(f"Wrote {len(counts)} tables ({sum(counts.values())} rows) to {arguments.output}")
    return 0


def _command_convert_town(arguments: argparse.Namespace) -> int:
    town = update_town(Path(arguments.files), Path(arguments.output))
    print(
        f"Converted {len(town['contests'])} contests, {len(town['classes'])} classes and "
        f"{len(town['calendar'])} calendar days to {arguments.output}"
    )
    return 0


def _command_slai_wheels(arguments: argparse.Namespace) -> int:
    result = import_wheels(Path(arguments.disc), Path(arguments.assets), Path(arguments.data),
                           Path(arguments.output), Path(arguments.spec))
    print(f"Imported {result['wheels']} wheeled legs; deliveries on {result['shop_days']} days")
    return 0


def _command_slai_assets(arguments: argparse.Namespace) -> int:
    result = convert_assets(Path(arguments.disc), Path(arguments.output), sound=not arguments.no_sound)
    print(json.dumps(result))
    return 0


def _command_register_slai_assets(arguments: argparse.Namespace) -> int:
    from .slai.native_sound import register_sound
    from .slai.native_particles import register_particles

    disc, output = Path(arguments.disc), Path(arguments.output)
    result = register_parts(disc, Path(arguments.assets), Path(arguments.data), output)
    result.update(register_stages(disc, Path(arguments.data), output))
    result.update(register_cosmetics(disc, Path(arguments.assets), Path(arguments.data), output))
    result.update(register_sound(disc, Path(arguments.data), output))
    result.update(register_particles(disc, output))
    print(json.dumps(result))
    return 0


def _command_slai_music(arguments: argparse.Namespace) -> int:
    result = convert_music(Path(arguments.disc), Path(arguments.assets), Path(arguments.data),
                           Path(arguments.output), Path(arguments.converter))
    print(f"Imported {result['songs']} songs ({result['bytes'] / 1048576:.1f} MiB), sold in Sonic Amp")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="phantomcrash", description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    listing = commands.add_parser("list", help="List every file on the disc")
    listing.add_argument("--disc", default=str(DEFAULT_DISC))
    listing.set_defaults(handler=_command_list)

    header = commands.add_parser("xbe", help="Print the XBE header as JSON")
    header.add_argument("--disc", default=str(DEFAULT_DISC))
    header.add_argument("--file", help="Read an extracted XBE instead of the disc")
    header.set_defaults(handler=_command_xbe)

    extraction = commands.add_parser("extract", help="Extract the disc into artifacts")
    extraction.add_argument("--disc", default=str(DEFAULT_DISC))
    extraction.add_argument("--output", default=str(DEFAULT_OUTPUT))
    extraction.add_argument("--quiet", action="store_true", help="Only print the summary")
    extraction.set_defaults(handler=_command_extract)

    conversion = commands.add_parser(
        "convert", help="Convert extracted textures, effects and tables to open formats"
    )
    conversion.add_argument("--files", default=str(DEFAULT_OUTPUT / "files"))
    conversion.add_argument("--output", default=str(DEFAULT_ASSETS))
    conversion.add_argument(
        "--replace", action="store_true", help="Delete and rebuild an earlier conversion"
    )
    conversion.add_argument("--quiet", action="store_true", help="Only print the summary")
    conversion.set_defaults(handler=_command_convert)

    robot_conversion = commands.add_parser(
        "convert-robots", help="Redo only the robot part of an existing conversion"
    )
    robot_conversion.add_argument("--files", default=str(DEFAULT_OUTPUT / "files"))
    robot_conversion.add_argument("--output", default=str(DEFAULT_ASSETS))
    robot_conversion.add_argument("--quiet", action="store_true", help="Only print the summary")
    robot_conversion.set_defaults(handler=_command_convert_robots)

    town_conversion = commands.add_parser(
        "convert-town", help="Redo only the town's tables (tables/town.json)"
    )
    town_conversion.add_argument("--files", default=str(DEFAULT_OUTPUT / "files"))
    town_conversion.add_argument("--output", default=str(DEFAULT_ASSETS))
    town_conversion.set_defaults(handler=_command_convert_town)

    mechassault = commands.add_parser(
        "convert-mechassault-vfx",
        help="Convert a MechAssault disc's weapon effects into a mod for the port",
    )
    mechassault.add_argument("disc", help="MechAssault .iso or .xiso")
    mechassault.add_argument("--output", default=str(DEFAULT_MODS))
    mechassault.set_defaults(handler=_command_convert_mechassault_vfx)

    arm = commands.add_parser(
        "swap-mechassault-arm",
        help="Give Phantom Crash arm models a MechAssault mech's gun arms (a mod)",
    )
    arm.add_argument("disc", help="MechAssault .iso or .xiso")
    arm.add_argument("--archive", default="Mission06a.mgf")
    madcat = r"\Models\Mechs\Heavy\MadCat\Model_Low_Eny"
    arm.add_argument("--right-arm", default=madcat + r"\mad_rgimparm\mad_rgimparm")
    arm.add_argument("--left-arm", default=madcat + r"\mad_lgimparm\mad_lgimparm")
    arm.add_argument("--right", default="models/binlink/105.gltf", help="right arm model to replace")
    arm.add_argument("--left", default="models/binlink/92.gltf", help="left arm model to replace")
    arm.add_argument("--assets", default=str(DEFAULT_ASSETS))
    arm.add_argument("--output", default=str(DEFAULT_MODS))
    arm.set_defaults(handler=_command_swap_mechassault_arm)

    slai = commands.add_parser(
        "add-slai-arm-weapon",
        help="Add an S.L.A.I. (PS2) arm weapon to the port as a new part (a mod)",
    )
    slai.add_argument("disc", help="S.L.A.I. .iso")
    slai.add_argument("--spec", default=str(slai_arm_weapon.SPEC), help="the weapon's definition")
    slai.add_argument("--assets", default=str(DEFAULT_ASSETS))
    slai.add_argument("--data", default=str(REPOSITORY_ROOT / "data"))
    slai.add_argument("--output", default=str(DEFAULT_MODS))
    slai.set_defaults(handler=_command_add_slai_arm_weapon)

    music = commands.add_parser("convert-slai-music", help="Import named S.L.A.I. songs as Ogg, sold in Sonic Amp")
    music.add_argument("disc")
    music.add_argument("--assets", default=str(DEFAULT_ASSETS))
    music.add_argument("--data", default=str(REPOSITORY_ROOT / "data"))
    music.add_argument("--output", default=str(DEFAULT_MODS))
    music.add_argument("--converter", default=str(REPOSITORY_ROOT / "build/port/port/Release/phantomcrash_slai_audio.exe"))
    music.set_defaults(handler=_command_slai_music)

    wheels = commands.add_parser("convert-slai-wheels", help="Import S.L.A.I. wheels and rotating shop deliveries")
    wheels.add_argument("disc")
    wheels.add_argument("--assets", default=str(DEFAULT_ASSETS))
    wheels.add_argument("--data", default=str(REPOSITORY_ROOT / "data"))
    wheels.add_argument("--output", default=str(DEFAULT_MODS))
    wheels.add_argument("--spec", default=str(Path(__file__).parent / "slai/wheels.json"))
    wheels.set_defaults(handler=_command_slai_wheels)

    slai_assets = commands.add_parser("convert-slai-assets", help="Export S.L.A.I. models, paints, rigs, motions, stages and sound assets")
    slai_assets.add_argument("disc")
    slai_assets.add_argument("--output", default=str(DEFAULT_MODS))
    slai_assets.add_argument("--no-sound", action="store_true", help="Skip sound bank conversion")
    slai_assets.set_defaults(handler=_command_slai_assets)
    native = commands.add_parser("register-slai-assets", help="Make converted S.L.A.I. parts and stages selectable")
    native.add_argument("disc")
    native.add_argument("--assets", default=str(DEFAULT_ASSETS))
    native.add_argument("--data", default=str(REPOSITORY_ROOT / "data"))
    native.add_argument("--output", default=str(DEFAULT_MODS))
    native.set_defaults(handler=_command_register_slai_assets)

    stats_export = commands.add_parser(
        "export-stats", help="Write every part, item and opponent's stats as CSV files"
    )
    stats_export.add_argument("--assets", default=str(DEFAULT_ASSETS))
    stats_export.add_argument("--output", default=str(DEFAULT_OUTPUT / "stats"))
    stats_export.set_defaults(handler=_command_export_stats)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    return arguments.handler(arguments)


if __name__ == "__main__":
    sys.exit(main())
