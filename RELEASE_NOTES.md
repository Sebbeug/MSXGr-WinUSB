# MSXGr-WinUSB v1.0.0.7

Première publication de la DLL x64 WinUSB pour l'ASCII/Sunrise MSX Game
Reader sous Windows 10/11.

## Contenu

- ABI historique `MSXGr.dll` ;
- accès direct via SetupAPI/WinUSB ;
- détection du numéro de lecteur choisi avec les DIP 1–3 ;
- cache MegaROM par pages de 8 Kio ;
- écritures mémoire et I/O toujours transmises au Game Reader ;
- aucune dépendance à Python, libusb ou Zadig après l'installation de WinUSB.

Cartouches validées : The Goonies, Gradius/Nemesis, Nemesis 2, Space Manbow,
R-Type, Rastan Saga, Break In et Ghost. Les détails de taille et de mapper sont
présents dans le README.

## DLL x64 validée

```text
Nom     : MSXGr.dll
Taille  : 61952 octets
SHA-256 : DD23AFDE25ACD004EF895A6F216C23EF6F67E5B8ACA549C5B5BCEE636444204F
```

Cette DLL doit être placée à côté de l'exécutable x64. Elle ne peut pas être
chargée par un programme 32 bits.
