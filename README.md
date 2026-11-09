# Next Generation Combat for OpenMW

ESP/Lua preview 2 of Aracelliy's Next Generation Combat 1.2.2.

This standalone mod uses standard OpenMW Lua API 129 or newer and three ESPs.
No custom combat engine hooks are required. Physics Preview 10 retains the
previous shadow fix and is a supported engine baseline.

See [installation and behavior](mod/README-OpenMW.md) and
[validation and adaptations](development/VALIDATION.md).

- mod/: the runtime package, including both unchanged original ESPs.
- development/MWSE/: unchanged original scripts used as the numerical oracle.
- development/build_companion.py: deterministic companion ESP source.
- development/tests/: original-formula comparisons and stock-API behavior tests.
- development/integration/: real Preview 10 executable gameplay scenarios.
- development/tools/: package verification utilities.

This branch contains the standalone mod source. The engine's physics-test and
physics-test-android branches restore the Preview 10 engine trees; the Android
wrapper rerelease changes package version/validation metadata only.
