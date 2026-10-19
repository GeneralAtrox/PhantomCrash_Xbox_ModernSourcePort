"""Install the optional, separately enabled S.L.A.I. content pack from a PAL ISO."""
from pathlib import Path
import argparse
import json
import shutil
import tempfile

from .archive import SlaiDisc
from .assets import convert_assets, save
from .stats import extract_stats
from .music import convert_music
from .native_parts import register_parts
from .native_stages import register_stages
from .native_cosmetics import register_cosmetics
from .native_sound import register_sound
from .native_particles import register_particles

INSTALL_VERSION = 1


def validate_disc(path: Path) -> None:
    with path.open('rb') as stream:
        stream.seek(16 * 2048)
        descriptor = stream.read(2048)
    if len(descriptor) != 2048 or descriptor[1:6] != b'CD001':
        raise ValueError('Select a S.L.A.I. PlayStation 2 ISO image.')
    with SlaiDisc(path) as disc:
        files = disc.disc_files()
        if not {'SLES_529.40', 'PC2.STM', 'PC2PRG.STM'} <= files.keys():
            raise ValueError('This importer requires the European S.L.A.I. disc (SLES-52940).')
    # Reject a different executable revision before creating or replacing assets.
    extract_stats(path)


def install(disc: Path, root: Path, converter: Path, report=print) -> None:
    disc, root = disc.resolve(), root.resolve()
    validate_disc(disc)
    assets, data = root / 'artifacts/europe/assets', root / 'data'
    if not (assets / 'tables/parts.json').is_file() or not converter.is_file():
        raise ValueError('Install Phantom Crash game data first; the audio converter is also required.')
    container = root / 'artifacts/slai'
    destination = container / 'assets'
    if destination.exists():
        raise FileExistsError('S.L.A.I. is already installed. The existing content has been kept.')
    container.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.install-', dir=container) as temporary:
        output = Path(temporary) / 'assets'
        output.mkdir()
        report('Converting models, textures, animation, stages and sound...', flush=True)
        convert_assets(disc, output)
        save(output / 'slai/source_stats.json', extract_stats(disc))
        report('Registering parts, stages, paints, stickers, audio and effects...', flush=True)
        register_parts(disc, assets, data, output)
        register_stages(disc, data, output)
        register_cosmetics(disc, assets, data, output)
        register_sound(disc, data, output)
        register_particles(disc, output)
        report('Converting music...', flush=True)
        convert_music(disc, assets, data, output, converter, report=lambda line: report(line, flush=True))
        for relative in ['tables/parts.json', 'tables/garage.json', 'robots/catalogue.json',
                         'slai/native_parts.json', 'slai/assets/manifest.json']:
            if not (output / relative).is_file():
                raise ValueError('Incomplete S.L.A.I. conversion: ' + relative)
        save(output / 'installed.json', {'version': INSTALL_VERSION, 'disc': 'SLES_529.40'})
        output.rename(destination)
    report('S.L.A.I. installed. The ISO is no longer needed. Restart the game after enabling it.', flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--converter', type=Path, required=True)
    args = parser.parse_args()
    try:
        install(args.disc, args.root, args.converter)
        return 0
    except Exception as error:
        print('S.L.A.I. installation failed: ' + str(error), flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
