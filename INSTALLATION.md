# Quick installation on Windows 10/11

## 1. Install WinUSB once

1. Connect the MSX Game Reader.
2. Run [Zadig](https://zadig.akeo.ie/) and enable **Options > List All Devices**.
3. Select the device matching **`VID 1125 / PID AC01`**.
4. Select **WinUSB**, then click **Install Driver** or **Replace Driver**.

Check the VID/PID carefully before replacing a driver. Zadig is no longer
needed after this initial setup. The WinUSB device interface must expose
GUID `{D6CAADCF-5D01-4B7F-B69D-31335078587D}` for this DLL to find the reader;
an installation using a different GUID will not be detected.
v1.0.0.8 also accepts PID AC02, but this variant has not been hardware-tested.

## 2. Install the DLL

Close the emulator and back up any existing `MSXGr.dll`. Copy the new DLL
**next to the emulator's `.exe` file**.

Example for a portable x64 [blueMSX+](https://github.com/Hesoten/blueMSX-plus):

```text
C:\Emulators\blueMSX+\blueMSX+.exe
C:\Emulators\blueMSX+\MSXGr.dll
```

Do not place the DLL in `C:\Windows\System32`. Match the application's
architecture:

- x64 application → x64 `MSXGr.dll`;
- x86 application → official x86 DLL or an x86 build of this project.

## 3. Use the cartridge

For v1.0.0.8, set the cache profile before launching the emulator:

```powershell
$env:MSXGR_MAPPER = 'konami' # Gradius/Nemesis
& 'C:\Emulators\blueMSX+\blueMSX+.exe'
```

Profiles: `linear`, `konami`, `konamiscc`, `ascii8`, `ascii16`, or `none`
(default). Use `konamiscc` for standard SCC ROMs and `ascii16` for the tested
custom Aleste 2 cartridge. Profiles assume immutable ROM; use `none` for
unknown mappers, R-Type, RAM/flash access, SCC+ and diagnostics. Without a
cache, emulator performance can be much slower. The emulator's mapper choice
does not set this variable; restart with the correct profile when changing
cartridge type. v1.0.0.7 does not use this setting.

Select **Game Reader** as the cartridge type, and choose
the cartridge's actual mapper when prompted. For SCC audio, use blueMSX+ with
the modified Game Reader integration; replacing the DLL alone does not add
SCC sound synthesis. The x64 DLL cannot run inside the original 32-bit
MSXPLAYer or blueMSX.
