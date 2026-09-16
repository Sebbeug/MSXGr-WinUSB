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

Restart the emulator, select **Game Reader** as the cartridge type, and choose
the cartridge's actual mapper when prompted. For SCC audio, use blueMSX+ with
the modified Game Reader integration; replacing the DLL alone does not add
SCC sound synthesis. The x64 DLL cannot run inside the original 32-bit
MSXPLAYer or blueMSX.
