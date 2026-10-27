> Note 2026-10-27: the ESP-IDF v6.1 checkout (installed by EIM) is referred to below as `<IDF folder>`. Its path must not contain spaces; the `-p` folder in the install command below is the historical location from the original installation.

> This file was copied from the kanchan speaker project folder (its original ESP-IDF setup document) because bleaudiocall shares the same toolchain and tooling folder. This project's `tools/Enter-EspIdf.ps1` and the VS Code terminal profile run on exactly this setup.

# ESP32-LyraT v4.3: learning the VS Code setup

Date: 25 October 2026

## Goal

Build our own firmware with ESP-IDF. ESP-IDF is the development framework; the ready-made ESP-ADF audio application is not used for now. The board's chip target is `esp32`, not `esp32s3`.

## Step 1: Check the existing setup

In PowerShell, `Get-Command code,git,python,idf.py,cmake,ninja -ErrorAction SilentlyContinue` was used to check PATH. The VS Code extension folders and user settings were checked as well.

Result: VS Code, Git and Python were installed. The ESP-IDF extension and the standard SDK locations were not found. The workspace had no project. The COM port query found no serial device.

Lesson: the editor and the compiler are separate things. Having VS Code installed does not mean a firmware build environment is installed. A tool missing from PATH is also not absolute proof that no custom installation exists.

## Step 2: Official VS Code extension

Command:

```powershell
code --install-extension espressif.esp-idf-extension
```

Result: Espressif ESP-IDF extension v2.3.0 installed successfully.

For C code navigation, `code --install-extension ms-vscode.cpptools` was run as well. Microsoft C/C++ extension v1.34.4 installed successfully. IntelliSense gets the real compiler flags from the generated `build/compile_commands.json` after the first successful configure/build.

Lesson: the extension gives VS Code build, configuration, flash and serial-monitor controls. The actual compiler and SDK are installed in the next step.

## Step 3: Installation Manager

Command:

```powershell
winget install --id Espressif.EIM-CLI --exact --source winget --accept-source-agreements --accept-package-agreements --silent
```

Purpose: install the official Espressif Installation Manager (EIM). It manages the SDK, the target compiler, the Python environment and the build tools.

Result: EIM CLI v0.20.1 installed successfully. Installer hash verification passed.

Note: the `eim` alias was not immediately available in the current terminal. The actual executable was found here:

```text
%LOCALAPPDATA%\Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe
```

The sandbox denied access when launching the executable; help and install were run through approved elevated execution. This was not an SDK error.

## Step 4: ESP-IDF and the ESP32 tools

Actual command (PowerShell's `&` runs a quoted executable path):

```powershell
& "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe" install -i v6.1 -t esp32 -p '<IDF folder parent>\espressif' --idf-features ide --skip-components-download true --do-not-track true --log-file '<project folder>\docs\eim-install.log' --config-file-save-path '<project folder>\docs\eim-install-config.toml'
```

- `-i v6.1`: reproducible SDK version; the official stable ESP32 documentation currently resolves to v6.1.
- `-t esp32`: the LyraT's processor target. Board name and processor target are different things.
- `-p`: base folder for the SDK checkout (`<IDF folder>` = `<that base>\espressif\v6.1\esp-idf`; no spaces in the path). In the actual install the shared tools went to the default `C:\Espressif\tools` and the downloaded archives to `C:\Espressif\dist`. The project's own source lives in `main/`.
- `--idf-features ide`: editor-support dependencies.
- `--skip-components-download true`: skips the bulk preload of registry metadata; a first project with built-in components should still build.
- `--do-not-track true`: installer usage telemetry disabled.
- The log and config files exist so the setup can be inspected and reproduced.

Observed logging difference: even with `--log-file`, EIM's active log was found at `%LOCALAPPDATA%\eim\logs\eim.log`. A copy is saved to `docs/` at the end of setup.

Prerequisite result: Git found, Python 3.14 sanity checks passed (pip, venv, SSL, ctypes and standard library). The installer selected GitHub for SDK/tools and PyPI for Python packages. One unused mirror returned 403; the installer continued with a working source.

## Step 5: Minimal project files

- Root `CMakeLists.txt`: loads the ESP-IDF build system and sets the project name.
- `main/CMakeLists.txt`: registers `main.c` in the application component.
- `main/main.c`: `app_main()` is our entry point. `ESP_LOGI` sends a message to the serial console; `vTaskDelay` makes the task wait for 1 second.
- `sdkconfig.defaults`: initial chip target `esp32`.
- `.vscode/extensions.json`: recommends the ESP-IDF extension to any editor that opens the project.
- `.vscode/settings.json`: activates the extension and excludes the SDK/build folders from search.
- `.gitignore`: keeps downloaded tools and generated build output out of source control.

The first program only logs. Codec, amplifier and GPIO pin configuration are added separately in the audio lessons. Compiling does not prove the program runs on hardware; that needs flash + serial log verification.

## Official references

- https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html
- https://docs.espressif.com/projects/idf-im-ui/en/latest/
- https://docs.espressif.com/projects/idf-im-ui/en/latest/cli_commands.html

## Current verification status

Extensions and the ESP-IDF v6.1 installation are complete. EIM status `finished` verified. Our minimal ESP32 firmware built successfully. The final verification command returned exit code 0. Board firmware has not been changed.

## Terminal and build commands (after installation is complete)

In the project folder's PowerShell terminal:

```powershell
. .\tools\Enter-EspIdf.ps1
idf.py --version
idf.py build
```

The leading dot and the space after it in the first command are required: that keeps the script's environment changes in the current terminal. Every new terminal session must activate the environment.

In this workspace the default profile for new terminals is set to `ESP-IDF v6.1 (LyraT)`; it performs the activation automatically. Close the existing terminal and open a new one. In a normal PowerShell terminal the dot-source command above can be used manually.

The actual activation happens through EIM's generated script `. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1`. It sets `IDF_PATH`, the compiler PATH and `IDF_PYTHON_ENV_PATH`. The actual Python venv location is `C:\Espressif\tools\python\v6.1\venv`. In the EIM layout `IDF_TOOLS_PATH=C:\Espressif\tools`; do not activate from a manually guessed layout.

The first build produced a Windows Unicode warning. The wrapper sets `PYTHONUTF8=1`; the permanent Windows language/region settings were not changed.

The EIM default selection was made explicit:

```powershell
& "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe" select v6.1
```

The installer also created an activated PowerShell shortcut on the desktop. The generated install configuration was saved to `docs/eim-install-config.toml`.

`idf.py build` compiles and links the source into ESP32 machine code. It generates the bootloader, partition table and application binaries. This command does not upload to the board.

A default build task was also added to `.vscode/tasks.json` for the VS Code shortcut `Ctrl+Shift+B`. It runs `tools/Build.ps1`, which activates the environment and runs `idf.py build`. The PowerShell execution policy is bypassed only for that process; the system's permanent execution policy was not changed.

In VS Code, `Ctrl+Shift+P` -> `ESP-IDF: Doctor Command` shows the extension diagnostics. If a setup selection is needed: `ESP-IDF: Select Current ESP-IDF Version` -> select the installed v6.1 checkout. The workspace's `idf.currentSetup` is set to that checkout.

`idf.py -p COMx flash monitor` is used only after the board is connected and the COM port is verified; `COMx` is the actually detected port. Flashing replaces the board's existing application.

## Step 6: Actual build verification

The same script as the VS Code build task was run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Build.ps1
```

The first full build compiled and linked the application and the bootloader. Because of the Unicode warning the outer PowerShell command reported a nonzero status, but the binary was generated and IDF printed `Project build complete`. After the warning fix, the final verification run of the same build script passed with exit code **0**. Logs are saved in `first-build.log` and `build-verification.log`.

Verified output:

- `build/lyrat_learning.bin`: application firmware, 0x22b80 = 142208 bytes.
- `build/bootloader/bootloader.bin`: 0x6640 = 26176 bytes.
- `build/partition_table/partition-table.bin`: generated partition layout.
- `build/compile_commands.json`: real compiler commands for the editor.
- Build target: ESP32; compiler: GNU 15.2.0; SDK: v6.1.

The first configure printed some SDK Kconfig deprecation/default-value notes; the build passed. The project was not yet a Git repository, so a message about automatic Git project-version detection being unavailable appeared as well. That was not a compilation failure.

The current flash size is the SDK default **2MB**; PSRAM is not enabled. This is software build verification, not the complete LyraT hardware configuration. After verifying the physical module/flash capacity, the flash size, PSRAM, partition design and audio pins are configured explicitly in the next hardware lessons.

## What to do now in VS Code

1. If the extension controls are not visible: Ctrl+Shift+P -> `Developer: Reload Window`.
2. Open `Terminal > New Terminal`. The `ESP-IDF v6.1 (LyraT)` profile activates the environment automatically.
3. `idf.py --version` shows the active SDK version.
4. Build the firmware with Ctrl+Shift+B. Expected: `Project build complete`.
5. Open `main/main.c`: our code starts at `app_main()`. Look at the serial log counter and the 1-second task delay.
6. Connecting the board, verifying the COM port and the first flash/monitor are the next hardware steps. Nothing has been flashed to the board yet.

The VS Code extension's GUI Doctor command was not executed in this session; the configuration files, EIM registration and the actual task build are verified.
