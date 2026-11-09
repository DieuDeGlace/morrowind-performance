# Next Generation Combat OpenMW

Current work is kept in Documents/morrowind-mods/next-generation-combat-openmw.

The current runtime uses ESPs plus standard OpenMW Lua. It does not read or set
custom combat controls or subscribe to custom native combat events. Original
MWSE sources and ESPs are preserved; the added companion ESP contains only the
OpenMW accuracy ability.

Both engine branches restore the exact shadow-fixed Physics Preview 10 trees.
Android rerelease metadata uses a higher package version to permit updating
over Preview 11 while retaining the same application ID and signing key.

See README.md, mod/README-OpenMW.md and development/VALIDATION.md.
Earlier native-preview reports are historical evidence, not current validation.
