# Open IV Dark-Mode Update (Not OFFICIAL)

![Downloads](https://img.shields.io/github/downloads/M4XL1M1T/Open-IV-Dark-Mode-Update-Not-OFFICIAL-/total)

Unofficial dark mode for **OpenIV 4.1** (32-bit, Windows 10/11). Not affiliated with New Technology Studio or the OpenIV team. Use at your own risk.

OpenIV has no complete dark mode. This project is a small helper DLL that is loaded when OpenIV starts and converts the window colours to a dark palette. `OpenIV.exe` itself is not modified.

## Installation

1. Copy `bin/dinput8.dll` (or the one from the [release](../../releases)) into the OpenIV folder, next to `OpenIV.exe`.
2. Start OpenIV.

Optional, for the dark title bar and the text editor, set these values in `%LOCALAPPDATA%\New Technology Studio\OpenIV\Application.xml`:

```xml
<AppColorThemeIndex>1</AppColorThemeIndex>
<TextEditorColorsJson>Monokai</TextEditorColorsJson>
```

Close OpenIV and back up the file first.

## Uninstall

Delete or rename `dinput8.dll` in the OpenIV folder.

## How it works

OpenIV loads `dinput8.dll` from its own folder before it looks at the system copy. The DLL forwards `DirectInput8Create` unchanged to the real `dinput8.dll` and, on load, redirects the colour and drawing functions of `OpenIV.exe` and `comctl32.dll` (import table patching). Light colours are darkened and dark text is lightened. In addition, the Windows dark mode for lists, trees and scroll bars is switched on.

Note: antivirus software may flag an unsigned `dinput8.dll`, because this technique (DLL proxying) is also used by malware. The complete source is in `src/darkmode.c`, so you can build the DLL yourself.

## Building

You need a 32-bit compiler, for example [llvm-mingw](https://github.com/mstorsjo/llvm-mingw):

```
i686-w64-mingw32-gcc -shared -O2 -o dinput8.dll src/darkmode.c -Wl,--kill-at -static -luser32 -lgdi32
```

## Known limitations

- The main window is tested. Settings, dialogs and message boxes are not, so some light areas may remain there.
- The colour of the group headers in the file list is tuned for Windows 11 (build 26100) and may look different on other Windows versions.
- It uses undocumented `uxtheme` functions (ordinals 135 and 136) that Windows may change.

## Contributing

Everyone may develop this further, fork it and improve it (MIT license, see `LICENSE`). Improvements, bug reports with a screenshot and pull requests are welcome. Especially helpful would be testing the dialogs and settings windows, and feedback on other Windows versions.
