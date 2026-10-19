from pathlib import Path
import json
import pytest
from phantomcrash_research.slai import install as installer


def test_invalid_iso_never_creates_content(tmp_path):
    disc = tmp_path / 'not-slai.iso'
    disc.write_bytes(b'not a disc')
    with pytest.raises(ValueError, match='ISO'):
        installer.install(disc, tmp_path, tmp_path / 'audio.exe')
    assert not (tmp_path / 'artifacts/slai').exists()


def fake_conversion(tmp_path, monkeypatch, fail=False):
    monkeypatch.setattr(installer, 'validate_disc', lambda _: None)
    assets = tmp_path / 'artifacts/europe/assets/tables'
    assets.mkdir(parents=True)
    (assets / 'parts.json').write_text('{}')
    converter = tmp_path / 'audio.exe'
    converter.write_bytes(b'fixture')
    def convert(disc, output):
        if fail:
            (output / 'partial').write_text('incomplete')
            raise ValueError('conversion failed')
        for name in ['tables/parts.json', 'tables/garage.json', 'robots/catalogue.json',
                     'slai/native_parts.json', 'slai/assets/manifest.json']:
            file = output / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('{}')
    monkeypatch.setattr(installer, 'convert_assets', convert)
    monkeypatch.setattr(installer, 'extract_stats', lambda _: {})
    for name in ['register_parts', 'register_stages', 'register_cosmetics',
                 'register_sound', 'register_particles', 'convert_music']:
        monkeypatch.setattr(installer, name, lambda *args, **kwargs: {})
    return converter


def test_failed_install_leaves_no_partial_pack(tmp_path, monkeypatch):
    converter = fake_conversion(tmp_path, monkeypatch, fail=True)
    with pytest.raises(ValueError, match='conversion failed'):
        installer.install(tmp_path / 'disc.iso', tmp_path, converter)
    assert list((tmp_path / 'artifacts/slai').iterdir()) == []


def test_success_has_receipt_and_existing_install_is_preserved(tmp_path, monkeypatch):
    converter = fake_conversion(tmp_path, monkeypatch)
    installer.install(tmp_path / 'disc.iso', tmp_path, converter)
    destination = tmp_path / 'artifacts/slai/assets'
    receipt = json.loads((destination / 'installed.json').read_text())
    assert receipt == {'version': 1, 'disc': 'SLES_529.40'}
    (destination / 'user.txt').write_text('keep')
    with pytest.raises(FileExistsError):
        installer.install(tmp_path / 'disc.iso', tmp_path, converter)
    assert (destination / 'user.txt').read_text() == 'keep'
