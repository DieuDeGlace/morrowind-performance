"""Build the deterministic, OpenMW-only NGC accuracy ability ESP."""
from pathlib import Path
import argparse
import struct

def subrecord(name, data):
    return name.encode("ascii") + struct.pack("<I", len(data)) + data

def record(name, data):
    return name.encode("ascii") + struct.pack("<III", len(data), 0, 0) + data

def companion_bytes():
    header = struct.pack("<fI32s256sI", 1.3, 0, b"NGC OpenMW port",
        b"OpenMW accuracy helper for Aracelliy's Next Generation Combat. Original ESPs are unchanged.", 1)
    spell = subrecord("NAME", b"ngc_omw_accuracy\0")
    spell += subrecord("FNAM", b"NGC accuracy\0")
    spell += subrecord("SPDT", struct.pack("<III", 1, 0, 0))  # Ability, zero cost.
    spell += subrecord("ENAM", struct.pack("<HbbIIIII", 117, -1, -1, 0, 0, 0, 10000, 10000))
    return record("TES3", subrecord("HEDR", header)) + record("SPEL", spell)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    path = Path(__file__).resolve().parent.parent / "mod/Next Generation Combat - OpenMW.esp"
    data = companion_bytes()
    if args.check:
        assert path.read_bytes() == data, "Companion ESP differs from its deterministic source"
    else:
        path.write_bytes(data)
    print(f"Verified companion ESP: {len(data)} bytes")

if __name__ == "__main__":
    main()
