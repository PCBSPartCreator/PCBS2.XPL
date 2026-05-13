# PCBS2.XPL

A `version.dll` proxy that injects custom parts into PC Building Simulator 2 at startup.  
PCBS2.XPL reads XML part definitions from a `mods/` folder and registers them with the game's `PartsDatabase` so they appear alongside the built-in catalog. Designed as a companion to [PCBS2 Part Creator](https://pcbs-partcreator.com), which produces compatible XML files through a visual editor.


## 📋 Table of Contents

- [Features](#-features)
- [Requirements](#-requirements)
- [Installation](#-installation)
- [Mod XML Format](#-mod-xml-format)
- [How It Works](#-how-it-works)
- [Building from Source](#-building-from-source)
- [Usage Examples](#-usage-examples)
- [Troubleshooting](#-troubleshooting)


## ✨ Features

- **Drop-in Installation**: Single `version.dll` proxy, no BepInEx or MelonLoader required
- **All Part Categories**: Supports every part type the game uses — GPU, CPU, RAM, Motherboard, PSU, Case, Storage, water-cooling components, peripherals, cables, decorations (32 categories total)
- **Native Behavior**: Reuses the game's own `ImportProp` and `AddNewPart` methods, so injected parts behave identically to vanilla ones
- **Companion Tool**: XML files can be generated visually with [PCBS2 Part Creator](https://pcbs-partcreator.com)
- **Safe Injection**: SEH-guarded database calls prevent a malformed mod from crashing the game
- **Detailed Logging**: Per-mod status written to `PCBS2.XPL.log` next to the executable
- **Optional Chain-Load**: Loads `JellysSockets.dll` automatically when present, allowing both mods to coexist on the same `version.dll` slot


## 🔧 Requirements

- **Game**: PC Building Simulator 2
- **OS**: Windows 10 / 11 (x64)
- **Runtime**: Visual C++ Redistributable 2015–2022 (typically already installed)


## 📦 Installation

### Step 1: Install PCBS2.XPL

1. Download the latest `version.dll` from the [Releases](../../releases) page.
2. Copy it into your PC Building Simulator 2 directory, next to `PCBuildingSimulator2.exe`.  
   Default Steam path: `...\steamapps\common\PC Building Simulator 2\`
3. Launch the game once. PCBS2.XPL will create a `mods/` folder in the same directory if it doesn't exist.

> **Note**: If you already have JellysSockets installed (also as `version.dll`), rename it to `JellysSockets.dll` first, then place PCBS2.XPL as the new `version.dll`. PCBS2.XPL will chain-load JellysSockets automatically — see [Scenario 4](#scenario-4-co-existing-with-jellyssockets).


### Step 2: Add Mod Files

1. Place any number of `.xml` part files into the `mods/` folder.
2. The easiest way to produce valid files is through [PCBS2 Part Creator](https://pcbs-partcreator.com).
3. Launch the game.


### Verification

Check `PCBS2.XPL.log` next to the game executable for entries like:

```text
[+] IL2CPP API loaded
[+] Hooked: PartDescGPU
[+] Hooked: PartDescCPU
...
[+] Loaded: MyCustomCard_RTX9090 (GPU)
[+] Total mods: 1
[+] Ready
[+] Injected: MyCustomCard_RTX9090 (42 props)
```


## 📄 Mod XML Format

PCBS2.XPL reads the same XML layout the game uses for its own parts. Every file must define at minimum a `Part Type` and an `ID`.

### File Format

```xml
<td div="Part Type">GPU</td>
<td div="ID">MyCustomCard_RTX9090</td>
<td div="Name">My Custom Card RTX 9090</td>
<td div="Manufacturer">CustomBrand</td>
<td div="Price">1499</td>
<!-- additional properties as needed -->
```


### Format Rules

- **Property Name**: Goes inside the `div="..."` attribute, case-sensitive
- **Property Value**: Goes between `<td>` and `</td>`
- **Whitespace**: Leading and trailing spaces in values are automatically trimmed
- **HTML Entities**: `&amp;` `&quot;` `&lt;` `&gt;` `&apos;` are decoded automatically
- **Required Fields**: `Part Type` and `ID` must both be present, otherwise the file is skipped
- **Duplicates**: If the same `ID` appears in multiple files, the first occurrence wins


### Supported Part Types

| Category        | Accepted `Part Type` values                                            |
|-----------------|------------------------------------------------------------------------|
| Core components | `GPU`, `CPU`, `RAM`, `Motherboard`, `PSU`, `Case`                      |
| Storage         | `Storage`, `HDD`, `SSD`, `M2`, `M.2` (all map to Storage internally)   |
| Cooling         | `Cooler`, `AirCooler`, `LiquidCooler` (the latter two map to Cooler)   |
| Water cooling   | `CPUBlock`, `GPUBlock`, `MotherboardBlock`, `MemoryBlock`, `Pump`, `Reservoir`, `PumpReservoir`, `Coolant` |
| Peripherals     | `Monitor`, `Keyboard`, `Mouse`, `MousePad`, `Headset`, `Microphone`    |
| Connectivity    | `Cable`, `CableConnector`, `Pipe`, `PipeConnector`                     |
| Accessories     | `Tool`, `Decoration`, `LEDStrip`, `RamHeatsink`, `PowerSplitter`, `PowerAdapter` |


## 🔬 How It Works

PCBS2.XPL is a proxy DLL that hijacks the Windows loader to get code execution inside the game process, then uses MinHook to intercept the game's own part-loading code.

1. **Proxy Load**: The game loads `version.dll` from its install directory before falling back to the system one. PCBS2.XPL forwards every export to `C:\Windows\System32\version.dll` so the game sees a functional API.
2. **IL2CPP Bootstrap**: A background thread waits for `GameAssembly.dll`, then resolves the IL2CPP API (`il2cpp_domain_get`, `il2cpp_class_from_name`, etc.) from it.
3. **Hook Installation**: MinHook installs detours on `ImportProp` for every `PartDesc*` class (`PartDescGPU`, `PartDescCPU`, ...). Several PartDesc classes share a compiled ImportProp body — those cases are detected and reuse the same trampoline.
4. **Mod Loading**: XML files in `mods/` are parsed into in-memory `ModPart` structures and queued.
5. **Injection**: When the game first calls any `ImportProp` during its own part loading, the matching detour fires, constructs the corresponding `PartDesc*` object, runs `ImportProp` for each property of every queued mod of that type, and registers the result via `PartsDatabase.AddNewPart`.


### Technical Flow

```text
Game starts
    ↓
Windows loads version.dll (PCBS2.XPL)
    ↓
MainThread waits for GameAssembly.dll
    ↓
IL2CPP API resolved, MinHook installed
    ↓
mods/*.xml parsed into ModPart queue
    ↓
Game begins its own part loading (calls ImportProp)
    ↓
Detour fires, constructs PartDesc*, replays ImportProp per property
    ↓
PartsDatabase.AddNewPart registers the mod
    ↓
Game continues, mod appears in catalog
```


## 🛠 Building from Source

### Requirements

- **Visual Studio 2022** with the *Desktop development with C++* workload
- **Windows SDK 10**
- **C++17**
- **[MinHook](https://github.com/TsudaKageyu/minhook)** — easiest via NuGet package `minhook.x64`


### Steps

1. Clone the repository.
2. Open the solution in Visual Studio 2022.
3. Install MinHook via *Project → Manage NuGet Packages → Browse → `minhook.x64`*.
4. Set configuration to **Release / x64**.
5. Build. The output is `version.dll`.

The project targets x64 only — PC Building Simulator 2 is a 64-bit IL2CPP Unity build, a 32-bit proxy would not load.


### Project Layout

| File           | Purpose                                                  |
|----------------|----------------------------------------------------------|
| `dllmain.cpp`  | `version.dll` export forwards, DllMain, init thread      |
| `il2cpp.*`     | IL2CPP API loader and class/method lookup helpers        |
| `hooks.*`      | MinHook setup for `ImportProp` on every `PartDesc*` class|
| `config.*`     | XML mod file parsing and part-type normalization         |
| `logger.*`     | Thread-safe logger writing to `PCBS2.XPL.log`            |


## 📖 Usage Examples

### Scenario 1: Add a Custom GPU

You want to add a fictional RTX 9090 GPU to the game catalog.

**Solution**: Create `mods/RTX9090.xml` using PCBS2 Part Creator (or manually), set `Part Type` to `GPU` and `ID` to something unique like `MyCustomCard_RTX9090`. Drop it into `mods/` and launch the game.


### Scenario 2: Add Multiple Parts at Once

You're shipping a parts pack with five GPUs, three CPUs, and a custom case.

**Solution**: Place all nine `.xml` files into `mods/`. PCBS2.XPL loads every `.xml` file it finds — no manifest or installer needed. Each file's `Part Type` determines which hook handles it.


### Scenario 3: Storage Variants

You have an XML that was authored as an SSD but the game internally only has one Storage category.

**Solution**: Use `Part Type = SSD`, `HDD`, `M2`, or `M.2` in the XML — all four are normalized to the `Storage` category automatically. No need to rewrite existing files when porting between authors.


### Scenario 4: Co-existing With JellysSockets

You already use JellysSockets and don't want to lose it. Both JellysSockets and PCBS2.XPL ship as `version.dll`, so they can't both occupy that slot directly.

**Solution**: Keep PCBS2.XPL as `version.dll` in the game directory, and rename the JellysSockets DLL to `JellysSockets.dll` in the same folder. On startup PCBS2.XPL detects `JellysSockets.dll` next to itself and chain-loads it, so both mods run side by side.


## 🐛 Troubleshooting

### Game Doesn't Start

**Symptoms**: Game window flashes and closes, or doesn't launch at all.

**Solutions**:

- Verify `version.dll` is in the game directory, not in a subfolder
- Make sure you downloaded the x64 build (the only build that exists)
- Check that the Visual C++ Redistributable 2015–2022 is installed
- Temporarily remove `version.dll` to confirm it's the cause


### No Log File Created

**Symptoms**: `PCBS2.XPL.log` doesn't appear after launching the game.

**Solutions**:

- PCBS2.XPL isn't loading. Verify `version.dll` is next to `PCBuildingSimulator2.exe`
- Some antivirus tools quarantine unknown DLLs in game directories — check quarantine
- Make sure the game folder is writable (avoid Program Files if possible, or run once as admin)


### Mods Not Appearing In-Game

**Symptoms**: Game loads, log shows `[+] Ready`, but mods don't show up in the catalog.

**Solutions**:

1. **Check log for skip reasons**: Look for lines like `[!] Skipped (missing Part Type or ID)` or `[-] Duplicate mod ID`
2. **Verify Part Type spelling**: Case-sensitive. `gpu` won't work, `GPU` will
3. **Check the Injected count**: Look for `[+] Injected: <id> (<n> props)`. If the count is 0, no properties were accepted — the XML structure is probably malformed
4. **Look for AddNewPart errors**: `[-] AddNewPart crashed for: <id>` means the part itself broke the game's database call. Compare against a working vanilla part XML


### "Class not found" Errors

**Symptoms**: Log shows `[-] Class not found: PartDescGPU` or similar.

**Solutions**:

- This usually means a game update changed the IL2CPP class layout. Wait for an updated PCBS2.XPL release, or check the [Issues](../../issues) page for a tracking ticket
- Verify you're running the official Steam version — some pirated builds strip or rename classes


### Hook Installation Failed

**Symptoms**: Log shows `[-] CreateHook failed for: <ClassName> (MH_STATUS=...)`.

**Solutions**:

- Another mod may have already hooked the same method. Try removing other mods one at a time
- If the status code is `MH_ERROR_ALREADY_CREATED`, it's the shared-ImportProp case and the log should also contain `[~] <X> shares ImportProp with <Y>` — that's fine, not a real error
- For other status codes, open an [issue](../../issues) with the log attached


### Game Crashes During Loading

**Symptoms**: Game crashes a few seconds after launch, log shows partial mod injections.

**Solutions**:

- Remove all mods from `mods/`. If the game launches cleanly, one of the mods is the culprit
- Bisect by adding mods back in halves until you find the problematic file
- The crashing mod's log line will be the last `[+] Injected:` entry before the crash — the mod after that one is the cause


---


**Made with ❤️ by anonymus637**