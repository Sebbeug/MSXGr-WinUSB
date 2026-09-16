# MSXGr-WinUSB v1.0.0.7

First x64 WinUSB DLL release for the ASCII/Sunrise MSX Game Reader on
Windows 10/11. This release supports `VID 1125 / PID AC01`.

## Features

- Original `MSXGr.dll` ABI;
- direct access through SetupAPI and WinUSB;
- detection of the reader's logical ID selected by DIP switches 1–3;
- MegaROM cache with 8 KiB pages;
- memory and I/O writes always forwarded to the Game Reader;
- no Python, libusb or Zadig dependency after WinUSB setup.

Tested cartridges: The Goonies, Gradius/Nemesis, Nemesis 2, Space Manbow,
R-Type, Rastan Saga, Break In, Ghost, Super Mario World (Noramos, 2021) and a
custom 2 MiB ASCII16 version of Aleste 2. See the README for sizes and mappers.
SCC audio requires the modified blueMSX+ Game Reader integration.

## Validated x64 DLL

```text
Name    : MSXGr.dll
Size    : 61952 bytes
SHA-256 : DD23AFDE25ACD004EF895A6F216C23EF6F67E5B8ACA549C5B5BCEE636444204F
```

Place this DLL next to the x64 application executable. A 32-bit application
cannot load it.
