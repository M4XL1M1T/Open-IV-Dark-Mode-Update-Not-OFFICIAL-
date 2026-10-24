# Open IV Dark-Mode Update (Not OFFICIAL)

![Downloads](https://img.shields.io/github/downloads/M4XL1M1T/Open-IV-Dark-Mode-Update-Not-OFFICIAL-/total)

Unofficial dark mode for **OpenIV 4.1** (32-bit, Windows 10/11). Not affiliated with New Technology Studio or the OpenIV team. Use at your own risk.

OpenIV has no complete dark mode. This project is a small helper DLL that is loaded when OpenIV starts and converts the window colours to a dark palette. `OpenIV.exe` itself is not modified.
## Preview
<img width="220" height="258" alt="image" src="https://github.com/user-attachments/assets/a9059b61-acd2-4649-96da-f968186d27d1" />

<img width="356" height="256" alt="image" src="https://github.com/user-attachments/assets/882722fe-7c56-42e3-a84e-807dee5428cc" />

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


## Antivirus false positives

Windows Defender (and possibly other scanners) may flag `dinput8.dll`, for example as `Trojan:Win32/Wacatac.B!ml`. This is a false positive. The `!ml` suffix means a machine-learning verdict from the cloud, and an unsigned DLL that is loaded in place of a system DLL and patches import tables looks similar to malware to such models. The DLL does not open network connections, does not read or write files (apart from loading the real system `dinput8.dll`) and does not install keyboard hooks. You do not have to take my word for it:

- Read the source in `src/darkmode.c` (about 700 lines).
- Build the DLL yourself (see below) and use your own copy.
- Compare the SHA-256 checksum of the downloaded file with the one in the release notes, and scan it on [VirusTotal](https://www.virustotal.com/).
- Report the false positive to Microsoft: [Security Intelligence file submission](https://www.microsoft.com/wdsi/filesubmission) (choose "software developer"). The more people report it, the sooner it is fixed.
- If you do not trust it, simply do not use it. Deleting `dinput8.dll` removes everything.

## Building

You need a 32-bit compiler, for example [llvm-mingw](https://github.com/mstorsjo/llvm-mingw):

```
i686-w64-mingw32-windres src/darkmode.rc -O coff -o darkmode.res.o
i686-w64-mingw32-gcc -shared -O2 -o dinput8.dll src/darkmode.c darkmode.res.o -Wl,--kill-at -static -luser32 -lgdi32
```

## Known limitations

- The main window is tested. Settings, dialogs and message boxes are not, so some light areas may remain there.
- The colour of the group headers in the file list is tuned for Windows 11 (build 26100) and may look different on other Windows versions.
- It uses undocumented `uxtheme` functions (ordinals 135 and 136) that Windows may change.

## Contributing

Everyone may develop this further, fork it and improve it (MIT license, see `LICENSE`). Improvements, bug reports with a screenshot and pull requests are welcome. Especially helpful would be testing the dialogs and settings windows, and feedback on other Windows versions.
