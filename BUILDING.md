# Building HoofMods

You only need this if you want to build HoofMods yourself. If you just want to use it, grab the zip from Nexus.

1. Install [Zig](https://ziglang.org/download/) 0.14.1 and make sure typing `zig version` in a command prompt works.
2. Double-click `build.bat`.
3. That's it! `HoofMods.exe` and `loader\hid.dll` end up in the `build` folder.

## What's in here

- `app` is HoofMods itself, the window where you manage your mods.
- `loader` is the little `hid.dll` that goes in the game folder and starts code mods with the game.

It's all plain C with no extra libraries. The Nexus link and the update check settings are in `app\release.h`.
