# Open IV Dark-Mode Update (Not OFFICIAL)

Inoffizieller Darkmode für **OpenIV 4.1** (32-Bit, Windows 10/11). Nicht mit New Technology Studio oder dem OpenIV-Team verbunden. Benutzung auf eigene Gefahr.

OpenIV hat keinen vollständigen Darkmode. Dieses Projekt ist eine kleine Hilfs-DLL, die beim Start in OpenIV geladen wird und die Farben des Fensters dunkel umrechnet. Die `OpenIV.exe` wird nicht verändert.

## Installation

1. `bin/dinput8.dll` in den OpenIV-Ordner kopieren (dorthin, wo `OpenIV.exe` liegt).
2. OpenIV starten.

Optional, für den dunklen Teil der Oberfläche und den Texteditor, in `%LOCALAPPDATA%\New Technology Studio\OpenIV\Application.xml`:

```xml
<AppColorThemeIndex>1</AppColorThemeIndex>
<TextEditorColorsJson>Monokai</TextEditorColorsJson>
```

Vorher OpenIV schließen und die Datei sichern.

## Deinstallation

`dinput8.dll` aus dem OpenIV-Ordner löschen oder umbenennen.

## So funktioniert es

OpenIV lädt `dinput8.dll` aus seinem eigenen Ordner, noch bevor es die System-Version verwendet. Die DLL leitet `DirectInput8Create` unverändert an die echte `dinput8.dll` weiter und biegt beim Laden die Farb- und Zeichenfunktionen von `OpenIV.exe` und `comctl32.dll` um (Import-Tabellen-Patch). Helle Farben werden dabei abgedunkelt, dunkler Text wird aufgehellt. Zusätzlich wird Windows' eigener Darkmode für Listen, Bäume und Scrollbalken aktiviert.

Hinweis: Ein Virenscanner kann eine unsignierte `dinput8.dll` als verdächtig melden, weil diese Technik (DLL-Proxy) auch von Schadsoftware benutzt wird. Der komplette Quelltext liegt in `src/darkmode.c`, die DLL lässt sich damit selbst bauen.

## Selbst bauen

32-Bit-Compiler nötig, zum Beispiel [llvm-mingw](https://github.com/mstorsjo/llvm-mingw):

```
i686-w64-mingw32-gcc -shared -O2 -o dinput8.dll src/darkmode.c -Wl,--kill-at -static -luser32 -lgdi32
```

## Bekannte Einschränkungen

- Das Hauptfenster ist getestet. Einstellungen, Dialoge und Meldungsfenster nicht, dort kann noch Helles übrig sein.
- Die Farbe der Gruppen-Überschriften in der Dateiliste ist auf Windows 11 (Build 26100) abgestimmt. Auf anderen Windows-Versionen kann sie anders aussehen.
- Es werden undokumentierte `uxtheme`-Funktionen (Ordinal 135 und 136) benutzt, die Windows ändern kann.