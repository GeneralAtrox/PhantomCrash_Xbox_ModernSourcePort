"""Build a self-contained ISO importer; players do not need Python installed."""
from pathlib import Path
import importlib.metadata
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/port/slai-installer'
OUTPUT = ROOT / 'build/port/port/Release'


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    resources = []
    for path in sorted((ROOT / 'src/phantomcrash_research/slai').glob('*.json')):
        resources += ['--add-data', str(path) + ':phantomcrash_research/slai']
    subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--onefile',
                    '--name', 'phantomcrash_slai_install', '--paths', str(ROOT / 'src'),
                    '--collect-data', 'phantomcrash_research',
                    '--copy-metadata', 'numpy', '--copy-metadata', 'pillow',
                    '--distpath', str(OUTPUT), '--workpath', str(BUILD / 'work'),
                    '--specpath', str(BUILD), *resources, str(ROOT / 'tools/slai_install.py')],
                   cwd=ROOT, check=True, timeout=600)
    notices = BUILD / 'licenses'
    notices.mkdir(exist_ok=True)
    python_license = Path(sys.base_prefix) / 'LICENSE.txt'
    shutil.copy2(python_license, notices / 'python.txt')
    for name in ['numpy', 'pillow', 'pyinstaller']:
        distribution = importlib.metadata.distribution(name)
        for item in distribution.files or []:
            if any(part.lower().startswith(('license', 'copying', 'authors')) for part in item.parts):
                source = Path(distribution.locate_file(item))
                if source.is_file():
                    target = notices / name / Path(*item.parts)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target)
    print('Built importer and collected bundled runtime notices.')


if __name__ == '__main__':
    main()
