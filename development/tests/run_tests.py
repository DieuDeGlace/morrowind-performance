"""Run Lua 5.1 combat regressions with Lupa 2.6.

Use --engine-source DIR for the actual supported OpenMW Combat scripts.
The legacy --openmw-reference option accepts flattened reference files.
"""
import argparse
import hashlib
import struct
from pathlib import Path

from lupa.lua51 import LuaRuntime


ESP_HASHES = {
    "Next Generation Combat - OpenMW.esp": "6ae4b30de084fba6ed73815cd495be16503baf7c1b02c11d00d49914d53f2540",
    "Next Generation Combat.esp": "1bcf14e93a88362fb7407de1b9893a9a908aff2a729c75d8b5c63cbaeab6a32d",
    "Next Generation Combat - Hand To Hand Fixes.esp": "55406d5239babe07ed4a8821d09e61bceb45b8b897c2f8ae17fcf2e5ab8748ba",
}


def read_esp_spells(root):
    spells = {}
    for name, expected_hash in ESP_HASHES.items():
        data = (root / name).read_bytes()
        assert hashlib.sha256(data).hexdigest() == expected_hash, f"Runtime ESP changed: {name}"
        offset = 0
        while offset < len(data):
            tag, size, _, _ = struct.unpack_from("<4sIII", data, offset)
            body = data[offset + 16:offset + 16 + size]
            offset += 16 + size
            if tag != b"SPEL":
                continue
            fields = {}
            effects = []
            cursor = 0
            while cursor < len(body):
                key, length = struct.unpack_from("<4sI", body, cursor)
                value = body[cursor + 8:cursor + 8 + length]
                cursor += 8 + length
                if key == b"ENAM":
                    effect, _, _, _, _, duration, minimum, maximum = struct.unpack("<HbbIIIII", value)
                    effects.append({"id": {77: "fortifyfatigue", 117: "fortifyattack"}.get(effect, str(effect)),
                                    "duration": duration, "magnitudeMin": minimum, "magnitudeMax": maximum})
                else:
                    fields[key] = value
            spell_id = fields[b"NAME"].rstrip(b"\0").decode("ascii")
            spells[spell_id] = {"id": spell_id, "effects": effects,
                                "type": struct.unpack_from("<I", fields[b"SPDT"])[0]}
    return spells


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--openmw-reference", type=Path)
    parser.add_argument("--engine-source", type=Path,
                        help="Exercise the real Combat scripts from this OpenMW checkout")
    parser.add_argument("--quick", action="store_true", help="Skip the large formula matrices")
    args = parser.parse_args()
    development = Path(__file__).resolve().parent.parent
    root = development.parent / "mod"
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.globals().quickTests = args.quick
    lua.globals().projectRoot = root.as_posix()
    lua.globals().developmentRoot = development.as_posix()
    lua.globals().espSpellRecords = lua.table_from(read_esp_spells(root), recursive=True)
    lua.globals().espContentNames = lua.table_from({name: True for name in ESP_HASHES})
    lua.globals().referenceRoot = args.openmw_reference.resolve().as_posix() if args.openmw_reference else None
    lua.globals().engineSourceRoot = args.engine_source.resolve().as_posix() if args.engine_source else None
    if args.engine_source:
        import re
        cmake = (args.engine_source / "CMakeLists.txt").read_text(encoding="utf-8")
        revision = int(re.search(r"set\(OPENMW_LUA_API_REVISION (\d+)\)", cmake)[1])
        lua.globals().testAPIRevision = revision
        lua.globals().modernCombat = revision >= 166
        print(f"Testing real engine Combat scripts, API {revision}", flush=True)
    compile_lua = lua.eval("function(source, name) local fn, err = loadstring(source, name); assert(fn, err) end")
    for path in sorted((root / "scripts/ngc/openmw").glob("*.lua")):
        compile_lua(path.read_text(encoding="utf-8"), str(path))
    lua.execute((development / "tests/test_openmw.lua").read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
