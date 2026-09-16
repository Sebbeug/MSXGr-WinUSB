# MSXGr-WinUSB v1.0.0.8 — prerelease

Cache and USB reliability fixes for the MSX Game Reader on Windows 10/11.
The original eleven `__cdecl` exports remain compatible.

## Changes and validation

- Mapper-specific cache keys fix stale banks after writes to register aliases.
- SCC regions and reads crossing their boundaries bypass the cache; prefetch
  excludes those regions. Other mapper profiles do not apply SCC exclusions.
- I/O writes and unrecognized memory writes invalidate the cache.
- Transfer failures close the connection and invalidate cached state. The next
  call attempts to reopen it; a failed operation is never automatically replayed.
- Reader status is refreshed on access after 250 ms; `Init` also probes an
  existing connection. Reinitialize after cartridge changes: a swap between
  polls can go unnoticed if the status stays identical.
- PID AC02 is accepted alongside AC01; pipe policy errors are checked,
  device access is exclusive, and error context is stored per thread.
- `MSXGR_ReadMemoryDirect` provides uncached physical reads without prefetch;
  `MSXGR_SetCacheProfile` selects the cache profile and invalidates cached data.

**The cache is disabled by default.** Before starting the emulator, set
`MSXGR_MAPPER` to `linear`, `konami`, `konamiscc`, `ascii8` or `ascii16` for a
known ROM mapper. Use `none` for unknown cartridges, RAM/flash, SCC+ and
diagnostics; uncached emulation may be slow. The emulator's mapper selection
does not configure this setting. See INSTALLATION.md for an example.

Both x86 and x64 builds pass the simulated USB regression tests. A physical
AC01 reader also passed initialization, status and a direct read containing
`ALESTE20`, without cartridge writes. Full in-game validation of this version
remains pending; the latest successful Gradius retest was reported without
identifying the DLL version. AC02 is tested in simulation only. One reader per
process is supported. These fixes do not establish compatibility with Pampas.
SCC sound still requires the modified blueMSX+ Game Reader integration.

## x64 prerelease DLL

```text
Name    : MSXGr.dll
Size    : 68608 bytes
SHA-256 : 246C041FB708906280872C7F854600E7FC4BBB1A2F2EF386C95FFACB20776CE5
```

Place this DLL next to the x64 application executable. A 32-bit application
cannot load it. The hardware-validated
[v1.0.0.7 release](https://github.com/Sebbeug/MSXGr-WinUSB/releases/tag/v1.0.0.7)
remains available.
