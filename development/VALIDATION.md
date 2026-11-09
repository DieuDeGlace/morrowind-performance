# Development validation

The last completed run passed 33 test groups under Lua 5.1 using Lupa and the real OpenMW 0.51 Combat interface and Morrowind combat handler. A final run is performed after packaging changes. The original MWSE main/perk files execute in a second engine double to supply comparison values.

The comparison covers 30 separate fist-health cases (five skill tiers, three armor totals, Always hit on/off) and 64 weapon-hit comparisons across short blade, long blade, blunt, spear, axe, bow, crossbow, and thrown. Further cases cover timers, ESP dependencies, original ESP hashes, skill gains, combat cleanup, native misses, armor order, and player fatigue restoration. These checks do not constitute an in-game or complete 1:1 parity certification.

Both original ESPs are checked by SHA-256 in `tests/run_tests.py`. `MWSE/` is preserved unchanged as a regression oracle and is not shipped in the upload ZIP.

Run with Python 3.12 and Lupa:

```text
python -m pip install lupa
python development/tests/run_tests.py
```

For the upstream handler checks, download the following two files from the official `openmw-0.51.0` source tag into an external reference directory, using the flattened filenames below, and add `--openmw-reference DIRECTORY` to the test command:

- `files/data/scripts/omw/combat/interface.lua` -> `files_data_scripts_omw_combat_interface.lua`
- `files/data-mw/scripts/omw/combat/local.lua` -> `files_data-mw_scripts_omw_combat_local.lua`

Reference-file SHA-256 values from the validation run:

- `files_data_scripts_omw_combat_interface.lua`: `d834f3a4849c48ab7b81c0def690f5d1e42fce7f8400d32a98e9062e6bb7ac0a`
- `files_data-mw_scripts_omw_combat_local.lua`: `b47e2cde36a6d514cbdc8147937ba83c0c272b75e18d370c20e4d1454d30e8a0`

Build the clean upload package with `python development/package.py`. It uses a fixed 14-file allowlist, verifies both original ESP hashes, checks ZIP contents and CRCs, and includes no development files. Temporary downloaded source caches and the local test interpreter are removed after final validation.

Final post-relocation validation passed: 33 groups with the upstream OpenMW handlers and 32 standalone groups. The clean archive contains 14 runtime files, both ESP hashes match, and temporary caches were deleted after the successful runs.
