> Yeh file kanchan speaker project (`D:\Vscode\esp\docs\01-esp-idf-setup-hinglish.md`) se copy ki gayi hai, kyunki bleaudiocall wahi toolchain aur tooling folder share karta hai. Yahan ke `tools/Enter-EspIdf.ps1` aur VS Code terminal profile isi setup par chalte hain.

# ESP32-LyraT v4.3: VS Code setup seekho

Date: 25 October 2026

## Goal

Apna firmware ESP-IDF se banana. ESP-IDF development framework hai; ESP-ADF ka ready audio application abhi use nahi karenge. Board ka chip target `esp32` hai, `esp32s3` nahi.

## Step 1: Existing setup check

PowerShell mein `Get-Command code,git,python,idf.py,cmake,ninja -ErrorAction SilentlyContinue` se PATH check kiya. VS Code extension folders aur user settings bhi check kiye.

Result: VS Code, Git, Python installed the. ESP-IDF extension aur standard SDK locations nahi mile. Workspace mein project nahi tha. COM port query mein koi serial device nahi mila.

Seekho: editor aur compiler alag cheezein hain. VS Code installed hone ka matlab firmware build environment installed hona nahi hai. PATH mein tool na milna custom installation absent hone ka absolute proof bhi nahi hai.

## Step 2: Official VS Code extension

Command:

```powershell
code --install-extension espressif.esp-idf-extension
```

Result: Espressif ESP-IDF extension v2.3.0 installed successfully.

C code navigation ke liye `code --install-extension ms-vscode.cpptools` bhi chalaya. Microsoft C/C++ extension v1.34.4 installed successfully. IntelliSense ko actual compiler flags generated `build/compile_commands.json` se milenge, pehle successful configure/build ke baad.

Seekho: extension VS Code ko build, configuration, flash aur serial-monitor controls deta hai. Actual compiler aur SDK next step mein install hote hain.

## Step 3: Installation Manager

Command:

```powershell
winget install --id Espressif.EIM-CLI --exact --source winget --accept-source-agreements --accept-package-agreements --silent
```

Purpose: official Espressif Installation Manager (EIM) install karna. Ye SDK, target compiler, Python environment aur build tools manage karta hai.

Result: EIM CLI v0.20.1 installed successfully. Installer hash verification passed.

Note: current terminal mein `eim` alias turant available nahi tha. Actual executable yahan mila:

```text
C:\Users\rohit\AppData\Local\Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe
```

Sandbox ne executable launch par access denied diya; approved elevated execution se help aur install chalaya. Ye SDK error nahi tha.

## Step 4: ESP-IDF aur ESP32 tools

Actual command (PowerShell `&` quoted executable path ko run karta hai):

```powershell
& 'C:\Users\rohit\AppData\Local\Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe' install -i v6.1 -t esp32 -p 'D:\Vscode\esp\tooling\espressif' --idf-features ide --skip-components-download true --do-not-track true --log-file 'D:\Vscode\esp\docs\eim-install.log' --config-file-save-path 'D:\Vscode\esp\docs\eim-install-config.toml'
```

- `-i v6.1`: reproducible SDK version; official stable ESP32 documentation currently resolves to v6.1.
- `-t esp32`: LyraT ka processor target. Board name aur processor target alag hain.
- `-p`: SDK checkout ka base folder. Actual install mein shared tools default `C:\Espressif\tools` aur downloaded archives `C:\Espressif\dist` mein aaye. Project ka apna source `main/` mein hai.
- `--idf-features ide`: editor support dependencies.
- `--skip-components-download true`: registry metadata ka bulk preload skip; built-in components wala first project phir bhi build hona chahiye.
- `--do-not-track true`: installer usage telemetry disabled.
- Log aur config files setup ko inspect/reproduce karne ke liye hain.

Observed logging difference: `--log-file` dene ke baad bhi EIM ka active log `%LOCALAPPDATA%\eim\logs\eim.log` mein mila. Setup ke end par uski copy `docs/` mein save karenge.

Prerequisite result: Git found, Python 3.14 sanity checks passed (pip, venv, SSL, ctypes and standard library). Installer selected GitHub for SDK/tools and PyPI for Python packages. One unused mirror returned 403; installer continued with a working source.

## Step 5: Minimal project files

- Root `CMakeLists.txt`: ESP-IDF build system load karta hai aur project name set karta hai.
- `main/CMakeLists.txt`: `main.c` ko application component mein register karta hai.
- `main/main.c`: `app_main()` hamara entry point hai. `ESP_LOGI` serial console par message bhejta hai; `vTaskDelay` task ko 1 second ke liye wait karata hai.
- `sdkconfig.defaults`: initial chip target `esp32`.
- `.vscode/extensions.json`: project kholne wale editor ko ESP-IDF extension recommend karta hai.
- `.vscode/settings.json`: extension activate karta hai aur SDK/build folders ko search se exclude karta hai.
- `.gitignore`: downloaded tools aur generated build output ko source-control commit se exclude karta hai.

Pehla program sirf logging karta hai. Codec, amplifier, GPIO pin configuration audio lessons mein separately add karenge. Compile hone se hardware par program chalne ka proof nahi milta; uske liye flash + serial log verify karna hota hai.

## Official references

- https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html
- https://docs.espressif.com/projects/idf-im-ui/en/latest/
- https://docs.espressif.com/projects/idf-im-ui/en/latest/cli_commands.html

## Current verification status

Extensions aur ESP-IDF v6.1 installation complete. EIM status `finished` verified. Apna minimal ESP32 firmware successfully built. Final verification command returned exit code 0. Board firmware has not been changed.

## Terminal aur build commands (installation complete hone ke baad)

Project folder ki PowerShell terminal mein:

```powershell
. .\tools\Enter-EspIdf.ps1
idf.py --version
idf.py build
```

Pehli command mein initial dot aur uske baad space zaroori hai: ye script ke environment changes current terminal mein rakhta hai. Har nayi terminal session ko environment activate karna padta hai.

Is workspace mein new terminal ka default profile `ESP-IDF v6.1 (LyraT)` set kar diya hai; wo activation automatically karta hai. Existing terminal ko close karke new terminal kholo. Normal PowerShell terminal mein upar wali dot-source command manually use kar sakte ho.

Actual activation EIM ki generated script `. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1` se hoti hai. Ismein `IDF_PATH`, compiler PATH aur `IDF_PYTHON_ENV_PATH` set hote hain. Python venv actual location `C:\Espressif\tools\python\v6.1\venv` hai. EIM layout mein `IDF_TOOLS_PATH=C:\Espressif\tools` hai; manually guessed layout se activate nahi karna.

First build par Windows Unicode warning aayi. Wrapper mein `PYTHONUTF8=1` set kiya; permanent Windows language/region settings change nahi kiye.

EIM default selection ko explicit kiya:

```powershell
& 'C:\Users\rohit\AppData\Local\Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe' select v6.1
```

Installer ne desktop par activated PowerShell shortcut bhi banaya. Generated install configuration `docs/eim-install-config.toml` mein save hui.

`idf.py build` source ko ESP32 machine code mein compile/link karta hai. Isse bootloader, partition table aur application binaries generate hoti hain. Ye command board par upload nahi karti.

VS Code shortcut `Ctrl+Shift+B` ke liye `.vscode/tasks.json` mein default build task bhi add kiya hai. Ye `tools/Build.ps1` chalata hai, jo environment activate karke `idf.py build` run karta hai. Task mein PowerShell execution policy sirf us process ke liye bypass hai; system ki permanent execution policy change nahi ki gayi.

VS Code mein `Ctrl+Shift+P` -> `ESP-IDF: Doctor Command` se extension diagnostics dekh sakte ho. Agar setup selection chahiye: `ESP-IDF: Select Current ESP-IDF Version` -> installed v6.1 checkout select karo. Workspace mein `idf.currentSetup` isi checkout par set kiya gaya hai.

Board connected aur COM port verified hone ke baad hi `idf.py -p COMx flash monitor` use karenge; `COMx` actual detected port hoga. Flash board ka existing application replace karega.

## Step 6: Actual build verification

VS Code build task wala same script run kiya:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Build.ps1
```

First full build ne application aur bootloader compile/link kiye. Unicode warning ki wajah se outer PowerShell command ne nonzero status report kiya, lekin binary generate hui aur IDF ne `Project build complete` print kiya. Warning fix ke baad same build script ka final verification run exit code **0** ke saath pass hua. Logs `first-build.log` aur `build-verification.log` mein saved hain.

Verified output:

- `build/lyrat_learning.bin`: application firmware, 0x22b80 = 142208 bytes.
- `build/bootloader/bootloader.bin`: 0x6640 = 26176 bytes.
- `build/partition_table/partition-table.bin`: generated partition layout.
- `build/compile_commands.json`: editor ke liye real compiler commands.
- Build target: ESP32; compiler: GNU 15.2.0; SDK: v6.1.

First configure mein SDK ke kuch Kconfig deprecation/default-value notes aaye; build pass hua. Project abhi Git repository nahi hai, isliye automatic Git project-version detection unavailable ka message bhi aaya. Ye compilation failure nahi tha.

Current flash size is the SDK default **2MB**; PSRAM is not enabled. Ye software build verification hai, complete LyraT hardware configuration abhi nahi. Physical module/flash capacity verify karke flash size, PSRAM, partition design aur audio pins ko next hardware lessons mein explicitly configure karenge.

## Ab tum VS Code mein kya karo

1. Agar extension controls nahi dikh rahe: Ctrl+Shift+P -> `Developer: Reload Window`.
2. `Terminal > New Terminal` kholo. Profile `ESP-IDF v6.1 (LyraT)` environment automatically activate karega.
3. `idf.py --version` se active SDK version dekho.
4. Ctrl+Shift+B se firmware build karo. Expected: `Project build complete`.
5. `main/main.c` kholo: hamara code `app_main()` se start hota hai. Serial log counter aur 1-second task delay dekho.
6. Board connect karke COM port verify karna aur first flash/monitor next hardware step hai. Abhi board par flash nahi kiya gaya.

VS Code extension ka GUI Doctor command is session mein execute nahi kiya gaya; configuration files, EIM registration aur actual task build verified hain.
