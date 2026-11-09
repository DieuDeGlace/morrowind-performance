# Next Generation Combat OpenMW

Native parity work is kept in Documents/morrowind-mods/next-generation-combat-openmw.

- `mod/`: runtime Lua scripts, original unchanged ESPs and icon, installation notes.
- `development/MWSE/`: unchanged original implementation used as the comparison oracle.
- `development/tests/`: behavior harness using each engine's actual Combat scripts.
- `development/results/`: local build provenance, logs and validation reports.
- `development/integration/`: reproducible isolated gameplay scenarios.
- `development/runtime/`: local earlier profiles; not shipped in the mod ZIP.

The matching native engines are the `codex/ngc-parity` and
`codex/ngc-parity-android` branches in morrowind-performance; the Android wrapper
uses `codex/ngc-parity-app`. Native hooks are opt-in and do not enable NGC by
themselves. The published shadow fix remains part of both engine branches.

See mod/README-OpenMW.md for supported behavior, adaptations, credits and setup.
See development/VALIDATION.md and development/results for validation scope.
