@echo off
rem Builds HoofMods. Needs Zig 0.14.1 (https://ziglang.org/download/) with zig on your PATH.
cd /d "%~dp0"
if not exist build\loader mkdir build\loader

echo Building the loader (hid.dll)...
zig cc -target x86_64-windows-gnu -O2 -Wall -shared -o build\loader\hid.dll loader\proxy.c loader\loader.c loader\loader.rc || goto fail

echo Building HoofMods.exe...
zig cc -target x86_64-windows-gnu -O2 -Wall -municode -Wl,--subsystem,windows -o build\HoofMods.exe app\main.c app\mods.c app\install.c app\zip.c app\preview.c app\cfg.c app\pad.c app\update.c app\app.rc -luser32 -lgdi32 -lshell32 -lole32 -ladvapi32 -lcomdlg32 -lwindowscodecs -luuid -ldwmapi -lwinhttp || goto fail

del /q build\*.pdb build\*.lib build\loader\*.pdb build\loader\*.lib 2>nul
echo.
echo Done! HoofMods.exe and loader\hid.dll are in the build folder.
exit /b 0

:fail
echo.
echo Something went wrong, check the messages above.
exit /b 1
