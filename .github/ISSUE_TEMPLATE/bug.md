---
name: 🐜 Bug report
about: Something that's supposed to work does not work or the program crashes.
---

Hi, and thanks for reporting errors! You can help us locate the bug even faster by providing detailed information in the sections below:

### The issue

Describe here what you've tried to do, what you expected to see and what actually happens. If necessary, describe step by step how to reproduce the problem.

### System and game

- **Platform and device**: for example "Android 9 (64-bit) on SomeTV Box", "Debian 12 on a Raspberry Pi 4 with 2GB RAM". For graphical glitches or slow games, please mention the CPU/GPU too.
- **System and core**: which system the game belongs to (for example PlayStation, Nintendo 64) and, if you know it, which core runs it.
- **ROM format**: for example `.chd`, `.zip`, `.7z`.

### PoniaEmu version

Open the Help screen and look at the About section. Also, have you tried the latest release? It might already have a fix for the problem!

### Log

PoniaEmu writes a log file about the last launch. It is named `lastrun.log` and lives in the `poniafe` configuration folder:

- Linux: `~/.config/poniafe/lastrun.log`
- Android: in the `poniafe` folder of your storage (the "no games found" screen also shows the exact path)

If possible, please attach the file to this bug report.
