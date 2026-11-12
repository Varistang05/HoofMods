# HoofMods

A little mod manager for Horses of Hoofprint Bay. Drag a mod in, flip a switch to turn it on or off, and that's it. No more digging through hidden folders.

Version 1.1. Questions or bugs? Find me on Patreon: https://patreon.com/Varistang

## Getting started

1. Unzip it anywhere, your Desktop is fine. Keep the `loader` folder next to `HoofMods.exe`.
2. Open `HoofMods.exe`. It finds the game on its own.
3. Press **Install loader** once. That's the little helper that starts code mods and keeps turned off tack actually turned off.

You don't need HoofMods open while you play.

## Using it

* **Add a mod:** drag its zip onto the window. Tack, clothes, language files, code mods and BepInEx mods all go where they need to go.
* **Turn it off:** flip the switch. Turned off tack and clothes are stored, not deleted.
* **Get rid of it:** press Remove. It goes to the Recycle Bin, so you can get it back.
* **Game acting weird?** Press **Start without mods** at the top. The game starts once with no code mods, so you can see if a mod is the problem. The next start is normal again.

BepInEx mods need BepInEx to run, and HoofMods tells you if you don't have it. Press **Get BepInEx** in HoofMods to download the right one (the IL2CPP one, not Mono) and drag that zip onto HoofMods too. It sets it up for you, and the first game start after that takes a minute or two.

If a horse is wearing tack you're turning off, HoofMods tells you first and backs up your saves. The game just takes the item off, and when you turn it back on it's right where it was. HoofMods never changes your saves.

Using StickHorse? Just drag its zip in too. If you already put `version.dll` in the game folder, HoofMods will offer to turn that copy off so it doesn't run twice.

It works with a controller too, and you can change the colours under **Appearance**.

## Good to know

* Some antivirus programs don't like the `hid.dll` it puts in the game folder. That's the loader and it's safe. Add the game folder as an exception if yours removes it.
* 7z and rar files need to be unzipped first.
* I haven't tried it on a Steam Deck yet. If you do, tell me how it went!
* Something not working? Send me `loader_log.txt` from `%LOCALAPPDATA%\HoofMods` on Patreon.
* To uninstall, delete `hid.dll` and the `Mods` folder from the game folder, then delete HoofMods.

Thanks for downloading! :3
