# Installation rapide sous Windows 10/11

## 1. Installer WinUSB une seule fois

1. Branchez le MSX Game Reader.
2. Lancez [Zadig](https://zadig.akeo.ie/), puis activez **Options > List All
   Devices**.
3. Sélectionnez uniquement le périphérique correspondant à
   `VID 1125 / PID AC01`.
4. Choisissez **WinUSB**, puis cliquez sur **Install Driver** ou
   **Replace Driver**.

Vérifiez soigneusement le VID/PID avant de remplacer un pilote. Zadig n'est
plus nécessaire après cette première association à WinUSB.

## 2. Installer la DLL

Fermez l'émulateur et sauvegardez son éventuelle ancienne `MSXGr.dll`. Copiez
ensuite la nouvelle DLL **à côté du fichier `.exe` de l'émulateur**.

Exemple pour un [blueMSX+](https://github.com/Hesoten/blueMSX-plus) x64
portable :

```text
C:\Emulateurs\blueMSX+\blueMSX+.exe
C:\Emulateurs\blueMSX+\MSXGr.dll
```

Ne placez pas la DLL dans `C:\Windows\System32` et ne mélangez pas les
architectures :

- programme x64 → `MSXGr.dll` x64 ;
- programme x86 → DLL officielle x86 ou compilation x86 de ce projet.

## 3. Utiliser la cartouche

Relancez l'émulateur, insérez **Game Reader** comme type de cartouche, puis
choisissez le mapper réel lorsque l'émulateur le demande. Une DLL x64 ne peut
pas fonctionner dans le MSXPLAYer ou le blueMSX historique en 32 bits.
