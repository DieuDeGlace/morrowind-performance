# Next Generation Combat for OpenMW

Native parity preview 1 of Aracelliy's Next Generation Combat 1.2.2, for
DieuDeGlace's Windows/Android Physics Preview 11 builds.

See [installation and supported behavior](mod/README-OpenMW.md) and
[validation and remaining differences](development/VALIDATION.md).
The original author, ESPs and MWSE reference implementation are credited and
preserved. This branch is the standalone Lua mod source; engine code lives in
the repository's `codex/ngc-parity` and `codex/ngc-parity-android` branches.

- `mod/`: the 15 runtime files.
- `development/MWSE/`: unchanged original implementation used as an oracle.
- `development/tests/`: numerical and behavior comparisons.
- `development/integration/`: reproducible packaged-engine gameplay scenarios.
- `development/tools/`: package verification utilities.

The native hooks are optional engine controls; installing the engine alone does
not enable this combat mod. The full engine builds retain the physics defaults
and Preview 10 shadow fix.
