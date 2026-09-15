# MSXGr-WinUSB

Remplacement libre de `MSXGr.dll` pour le lecteur **ASCII/Sunrise MSX Game
Reader** sous Windows 10/11. La bibliothèque communique directement avec le
pilote Microsoft WinUSB, sans libusb, Python ni Zadig après l'installation
initiale du pilote.

Le nom du fichier reste obligatoirement `MSXGr.dll`, car les émulateurs
recherchent cette ABI historique.

## À quoi sert la version x64 ?

La DLL x64 est destinée à **blueMSX+ compilé en 64 bits** et aux autres
programmes x64 compatibles avec l'API MSXGr. Elle ne peut pas être chargée par
le MSXPLAYer historique ni par le blueMSX historique lorsqu'ils sont en
32 bits. Pour ceux-ci, conservez la DLL officielle x86 si elle fonctionne, ou
compilez la variante x86 de ce projet.

## Installation

Consultez [INSTALLATION.md](INSTALLATION.md). En résumé : installez une fois le
pilote WinUSB pour `VID 1125 / PID AC01`, puis placez `MSXGr.dll` **dans le même
dossier que l'exécutable de l'émulateur**. Il ne faut pas copier la DLL dans
`System32`.

## Cartouches validées sur matériel réel

| Cartouche | Taille | Mapper | Résultat |
|---|---:|---|---|
| The Goonies | 32 Kio | Konami | OK |
| Gradius / Nemesis | 128 Kio | Konami | OK, fluide |
| Nemesis 2 | — | Konami SCC | OK, SCC fluide |
| Space Manbow | — | Konami SCC | OK, SCC fluide avec blueMSX+ modifié |
| R-Type | — | R-Type | OK |
| Rastan Saga | 256 Kio | ASCII8 | OK |
| Break In (1987) | 64 Kio | Mirrored | OK |
| Ghost (2017) | 32 Kio | Linear | OK |

Cette liste constitue le périmètre annoncé. Les cartouches modernes à flash,
EEPROM, FPGA ou protections particulières ne sont pas déclarées compatibles.

La DLL transporte les accès mémoire et I/O. Le choix du mapper appartient à
l'émulateur : il faut sélectionner le mapper réel de la cartouche. Par exemple,
The Goonies doit être déclaré **Konami**, malgré sa taille de 32 Kio.

## Fonctions exportées

La bibliothèque conserve l'ABI `__cdecl` historique :

- `MSXGR_Init`, `MSXGR_Uninit`, `MSXGR_Err2Str`, `MSXGR_GetVersion` ;
- `MSXGR_SetDebugMode`, `MSXGR_IsSlotEnable`, `MSXGR_GetSlotStatus` ;
- `MSXGR_ReadMemory`, `MSXGR_WriteMemory`, `MSXGR_ReadIO`, `MSXGR_WriteIO`.

Elle utilise SetupAPI et WinUSB, gère les unités choisies par les DIP 1–3 et
inclut un cache MegaROM de pages de 8 Kio. Les écritures restent toujours
transmises au matériel.

## Compiler

Téléchargez [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases), puis
indiquez le dossier extrait au script. Celui-ci produit les variantes x64 et
x86 :

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 `
  -ToolchainPath C:\Outils\llvm-mingw
```

Si les compilateurs LLVM-MinGW sont déjà dans `PATH`, `-ToolchainPath` peut être
omis.

Résultats :

- `build/x64/MSXGr.dll` ;
- `build/x86/MSXGr.dll`.

Le test matériel fourni est réservé à une cartouche Gradius/Nemesis originale
de 128 Kio. Fermez l'émulateur avant de l'exécuter :

```powershell
.\test-gradius.ps1
```

Il vérifie le dump de 128 Kio avec le CRC32 de référence `4dfcc009`. Une DLL
fraîchement recompilée n'est pas considérée comme validée tant que ce test et un
démarrage dans l'émulateur n'ont pas réussi.

## Version x64 validée

SHA-256 de `MSXGr.dll` x64 :

```text
DD23AFDE25ACD004EF895A6F216C23EF6F67E5B8ACA549C5B5BCEE636444204F
```

Les ROM commerciales, émulateurs portables, journaux, dumps et prototypes non
validés ne font pas partie de ce dépôt.

## Licence

`MSXGr-WinUSB` est distribué sous [licence MIT](LICENSE). blueMSX+ est un projet
distinct sous GPLv2 ; ses modifications ne sont pas incluses dans ce dépôt.
