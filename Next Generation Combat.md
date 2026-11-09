# Next Generation Combat OpenMW

All files for this port are kept here, separate from the physics mod.

- `mod/` - clean runtime files, including both original ESPs.
- `Next Generation Combat - OpenMW 0.51.0.zip` - upload this package; development files are excluded.
- `development/` - regression tests, original MWSE source used as a comparison, and validation notes.

The port now restores separate physical fist health damage, post-armor weapon bonuses, the ESP's Adrenaline Rush ability, three-second bow draw, original skill selection, and legacy multistrike behavior. It is a close Lua port with documented engine limits; it has not been tested inside a running OpenMW game.

See `mod/README-OpenMW.md` for installation and remaining differences, and `development/VALIDATION.md` for test evidence. This folder is under Documents/Morrowind Mods/next generation combat openmw. Codex's old Downloads project binding must be changed by opening this folder as the project.
