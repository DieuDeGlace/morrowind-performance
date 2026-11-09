"""Build the clean, allowlisted OpenMW upload ZIP using only the standard library."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import hashlib

ESP_HASHES = {
    "Next Generation Combat.esp": "1bcf14e93a88362fb7407de1b9893a9a908aff2a729c75d8b5c63cbaeab6a32d",
    "Next Generation Combat - Hand To Hand Fixes.esp": "55406d5239babe07ed4a8821d09e61bceb45b8b897c2f8ae17fcf2e5ab8748ba",
}
ALLOWED = (
    "Next Generation Combat.esp",
    "Next Generation Combat - Hand To Hand Fixes.esp",
    "Next Generation Combat.omwscripts",
    "README-OpenMW.md",
    "icons/ngc/active_block.tga",
    "l10n/NGC/en.yaml",
) + tuple("scripts/ngc/openmw/" + name + ".lua" for name in (
    "actor", "config", "defaults", "global", "load", "player", "rules", "settings"
))


def main():
    root = Path(__file__).resolve().parent.parent
    mod = root / "mod"
    actual = {p.relative_to(mod).as_posix() for p in mod.rglob("*") if p.is_file()}
    if actual != set(ALLOWED):
        raise RuntimeError(f"Runtime file list differs: {actual.symmetric_difference(ALLOWED)}")
    for name, expected in ESP_HASHES.items():
        assert hashlib.sha256((mod / name).read_bytes()).hexdigest() == expected, name
    output = root / "Next Generation Combat - OpenMW 0.51.0.zip"
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
        for name in ALLOWED:
            archive.write(mod / name, name)
    with ZipFile(output) as archive:
        assert set(archive.namelist()) == set(ALLOWED)
        assert archive.testzip() is None
        for name in ALLOWED:
            assert archive.read(name) == (mod / name).read_bytes(), name
    print(f"Verified upload package: {output}")
    print(f"{len(ALLOWED)} runtime files, {output.stat().st_size} bytes; no development files or caches.")


if __name__ == "__main__":
    main()
