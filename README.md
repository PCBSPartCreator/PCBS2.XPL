<div align="center">

<img src="assets/PCBS2_XPL_Logo.png" alt="PCBS2.XPL Logo" width="160" />

# PC Building Simulator 2 - XML Part Loader

[![Version](https://img.shields.io/badge/version-1.0.2-blue.svg)](../../releases)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg)]()
[![Game](https://img.shields.io/badge/game-PC%20Building%20Simulator%202-orange.svg)](https://www.pcbuildingsim.com/)
[![C++](https://img.shields.io/badge/C%2B%2B-14-00599C.svg?logo=cplusplus)]()

A `version.dll` proxy that injects custom parts into PC Building Simulator 2 at startup.  
Designed as a companion to [PCBS2 Part Creator](https://www.nexusmods.com/pcbuildingsimulator2/mods/102), which produces compatible XML files through a visual editor.  
PCBS2.XPL reads XML part definitions from the `mods/` folder and feeds them through the game's own `ImportFromHTML` pipeline, so they appear alongside the built-in catalog as if they were vanilla content.  
It also ships **SaveFix**, which keeps your saves loadable after a part mod is removed by cleanly pruning the leftover parts on load.

##

<a href="#-features">✨ Features</a> • 
<a href="#-requirements">🔧 Requirements</a> • 
<a href="#-installation">📦 Installation</a> • 
<a href="#-building-from-source">🛠️ Building from Source</a> • 
<a href="#-mod-xml-format">📄 Mod XML Format</a> • 
<a href="#-how-it-works">🔬 How It Works</a> • 
<a href="#-part-types">🌟 Part Types</a> • 
<a href="#-usage-notes">📖 Usage Notes</a> • 
<a href="#-troubleshooting">🐛 Troubleshooting</a> • 
<a href="#-credits">©️ Credits</a>

</div>


## ✨ Features

- ✅ **Drop-in Installation**: Single `version.dll` proxy, no BepInEx or MelonLoader required
- ✅ **All Part Categories**: Supports every part type the game registers internally - RAM, CPU, GPU, Storage, PSU, Motherboard & [much more](#-part-types)
- ✅ **Native Behavior**: Mod XMLs pass through the game's own `ImportFromHTML` method, identical to how vanilla parts are loaded - no manual reconstruction
- ✅ **Folder Structure**: Mods are scanned recursively, so they can be organized into subfolders by category (`mods/GPU/`, `mods/CPU/`, ...) or kept flat - both work
- ✅ **Multiple Parts per File**: A single XML can carry many parts of the same type, matching the game's own part-pack layout
- ✅ **Vanilla Override**: Mods that reuse a vanilla `ID` override that specific part - useful for balance tweaks
- ✅ **SaveFix**: Removing a part mod no longer breaks your saves - on load, parts left behind by an uninstalled mod are pruned cleanly (including water-cooling loops and tubing), so the save opens normally and the freed slots stay buildable
- ✅ **Configurable**: A self-creating `config/PCBS2.XPL.cfg` lets you toggle SaveFix on or off (`SaveFix=true` by default)
- ✅ **Reliable Startup**: Bootstrap is deferred onto a real game thread via an `il2cpp_runtime_invoke` hook, avoiding the IL2CPP/GC race that could crash the first launch after a reboot
- ✅ **Lightweight**: Size-optimized release build (~a third smaller than before) that still ships as a single drop-in `version.dll`
- ✅ **Companion Tool**: XML files can be generated visually with [PCBS2 Part Creator](https://www.nexusmods.com/pcbuildingsimulator2/mods/102)
- ✅ **Safe Injection**: SEH-guarded database calls prevent a malformed mod from crashing the game
- ✅ **Detailed Logging**: Per-mod status written to `PCBS2.XPL.log` next to the executable
- ✅ **Optional Chain-Load**: Loads [Jelly's Socket Creator](https://github.com/ZeOs360/JellysSocketCreator) `version.dll` automatically when present, allowing both mods to coexist on the same `version.dll` slot


## 🔧 Requirements

- **Game**: [PC Building Simulator 2](https://store.epicgames.com/p/pc-building-simulator-2)
- **OS**: Windows 10 / 11 (x64)
- **Runtime**: [Visual C++ Redistributable 2015-2026](https://visualstudio.microsoft.com/de/downloads/) (typically already installed)


## 📦 Installation

### Step 1: Install PCBS2.XPL

1. Download the latest version from [NexusMods](https://www.nexusmods.com/pcbuildingsimulator2/mods/135)
2. Copy it into your PC Building Simulator 2 directory, next to `PCBS2.exe`.  
   Default Epic Games path: `...\Epic Games\PCBuildingSimulator2\`
3. Launch the game once. **PCBS2.XPL** will create a `mods/` folder in the same directory if it doesn't exist, along with a `config/PCBS2.XPL.cfg` you can use to toggle SaveFix.

> **Note**: If you already have [Jelly's Socket Creator](https://github.com/ZeOs360/JellysSocketCreator) installed (also as `version.dll`), rename it to `JellysSockets.dll` first, then place **PCBS2.XPL** as the new `version.dll`. **PCBS2.XPL** will chain-load JellysSockets automatically - see [Note 6](#note-6-running-alongside-jellys-socket-creator).


### Step 2: Add Mod Files

1. Place `.xml` part files into the `mods/` folder. Subfolders are allowed and scanned recursively.
2. The easiest way to produce valid files is through [PCBS2 Part Creator](https://www.nexusmods.com/pcbuildingsimulator2/mods/102).
3. Launch the game.


### Verification

Check `PCBS2.XPL.log` next to the game executable for entries like:

```text
[+] IL2CPP API loaded
[+] runtime_invoke hooked, bootstrap deferred to game thread
[+] Loaded JellysSockets.dll
[+] TextAsset .ctor(CreateOptions, string) resolved
[+] Hooked PartsDatabase.Load
[+] Hooked PartsDatabase.ImportFromHTML
[+] SaveFix ready
[+] Hooked SaveLoadSystem.LoadPC
[+] Hooked SaveLoadSystem.LoadGame
[+] Hooked PartInstance.FixForVersion
[+] Hooked VirtualComputer.CheckBiosError
[+] Hooked VirtualComputer.UpdateTabletApps
[+] Hooked OS.OnStartup
[+] Hooked WalkingState.SetSelected
[+] Hooked ComputerSave.GetBaseRAMInstance
[+] Hooked Slot.HandleRadiator
[+] Hooked ComputerSave.OnAfterDeserialize
[+] Hooked CalendarDay.UpdateEvents
[+] Hooked Job.OnDeserialization
[+] Found mod file: AirCooledGPU.xml
[+] Found mod file: CPU.xml
[+] Found mod file: Motherboard.xml
[+] Found mod file: PSU.xml
[+] Found mod file: RAM.xml
[+] Found mod file: Storage.xml
[+] Total mod files: 6
[+] 6 mods queued
[+] Ready
[+] Injected dynamic header for: AirCooledGPU.xml
[+] Imported: AirCooledGPU.xml
[+] Injected dynamic header for: CPU.xml
[+] Imported: CPU.xml
[+] Injected dynamic header for: Motherboard.xml
[+] Imported: Motherboard.xml
[+] Injected dynamic header for: PSU.xml
[+] Imported: PSU.xml
[+] Injected dynamic header for: RAM.xml
[+] Imported: RAM.xml
[+] Injected dynamic header for: Storage.xml
[+] Imported: Storage.xml
[=] Mod load complete: 6 imported, 0 failed
[=] Loaded Parts: 3050
```

## 🛠️ Building from Source

### Requirements

- **[Visual Studio](https://visualstudio.microsoft.com/de/downloads/)** or newer with the *Desktop development with C++* workload
- **[Windows SDK 10](https://learn.microsoft.com/de-de/windows/apps/windows-sdk/downloads)**
- **C++ 14 or newer**


### Steps

1. Clone the repository - MinHook is already vendored under `third_party/minhook` so no additional dependencies are needed.
2. Open the solution in Visual Studio.
4. Set configuration to **Release / x64**.
5. Build. The output is `version.dll`.

The project targets x64 only - PC Building Simulator 2 is a 64-bit IL2CPP Unity build, a 32-bit proxy would not load.


### Project Layout

| File           | Purpose                                                  |
|----------------|----------------------------------------------------------|
| `dllmain.cpp`  | `version.dll` export forwards, DllMain, init thread, deferred bootstrap via `il2cpp_runtime_invoke` |
| `il2cpp.*`     | IL2CPP API loader and class/method lookup helpers        |
| `hooks.*`      | MinHook setup for `PartsDatabase.Load` / `ImportFromHTML` dispatch and the SaveFix hooks (`SaveLoadSystem.LoadPC` / `LoadGame`, `PartInstance.FixForVersion`) |
| `config.*`     | Recursive `mods/` scan, example-file generation, and `config/PCBS2.XPL.cfg` parsing (SaveFix toggle) |
| `logger.*`     | Thread-safe logger writing to `PCBS2.XPL.log`            |


## 📄 Mod XML Format

PCBS2.XPL reads the exact same XML layout the game uses for its own parts. The file is parsed by the game's built-in `HTMLTableReader`, so no separate format spec or schema exists - if it works for a vanilla part, it works for a mod.

### File Format
 
A mod XML has two structural pieces:
 
1. **First line**: An asset name (any text - used for logging and error messages).
2. **Data row(s)**: One `<tr>` per part. Each `<td>` carries the value, and a `div="..."` attribute identifying which property it corresponds to.
```xml
GPU_Example
<table>
  <tr>
    <td div=""></td>
    <td div="Part Type">GPU</td>
    <td div="ID">GPU_Example_001</td>
    <td div="In Game">Yes</td>
    <td div="Manufacturer">ExampleCorp</td>
    <td div="Part Name">Example GPU</td>
    <td div="Price">1500</td>
  </tr>
</table>
```
 
PCBS2.XPL builds the property header automatically at runtime from the `div="..."` attributes of each row, so mod authors only need to write the data rows.


### Format Rules
 
- **Property Name**: Set via the `div="..."` attribute on each data cell. Case-sensitive
- **Property Value**: Goes between `<td>` and `</td>` on the data row
- **Required Fields**: `Part Type`, `ID`, and `In Game = Yes` must be present, otherwise the row is silently skipped by the game's reader
- **Multiple Parts**: A single XML may contain multiple data rows - useful for shipping a whole pack as one file. All parts in one XML must share the same `Part Type`
- **Overrides**: A row whose `ID` matches an existing vanilla or modded part overwrites that part rather than adding a new one


## 🔬 How It Works
 
PCBS2.XPL is a proxy DLL that loads into the game process at startup, then uses [MinHook](https://github.com/TsudaKageyu/minhook) to intercept a few methods of the game and hand it the mod XMLs - and, for SaveFix, to clean up save files before the game reads them.
 
1. **Proxy Load**: PCBS2.XPL is named `version.dll` and placed next to `PCBS2.exe`. Windows loads DLLs from the program's own folder before the system folder, so the game loads PCBS2.XPL instead of the real one. All exports are forwarded to `C:\Windows\System32\version.dll`.
2. **Wait for the Game**: A background thread waits for `GameAssembly.dll` to load, then resolves the IL2CPP runtime API used to look up game classes by name.
3. **Defer Bootstrap to a Game Thread**: Doing IL2CPP work from the native init thread is unsafe - that thread is invisible to the runtime's garbage collector, which was the cause of the old cold-start crash. PCBS2.XPL therefore installs a single detour on `il2cpp_runtime_invoke` and stops there. The first time the game invokes a managed method - on a real, GC-tracked thread - the detour runs the bootstrap below exactly once (CAS-gated, with retries) and then steps aside.
4. **Install the Hooks**: MinHook places detours on `PartsDatabase.Load` (part injection) and on `SaveLoadSystem.LoadPC`, `SaveLoadSystem.LoadGame`, and `PartInstance.FixForVersion` (SaveFix). Method pointers to `PartsDatabase.ImportFromHTML` and the `UnityEngine.TextAsset(CreateOptions, string)` constructor are resolved at the same time.
5. **Scan Mods**: The `mods/` folder is walked recursively, every `.xml` file found is queued by path.
6. **Injection**: When the game calls `PartsDatabase.Load()`, the detour first runs the original method (loading every vanilla part). It then iterates the queued mod files: each XML is read from disk, a property header is built from the `div="..."` attributes of the first data row and prepended to the table, the result is wrapped in a `TextAsset`, and passed to `ImportFromHTML` - the same method the game uses internally for its own part assets.
The game's own `HTMLTableReader` parses the table, the game's own `PartDesc.Create` factory builds the right subclass for each row, and the game's own virtual `ImportProp` dispatch sets every property. PCBS2.XPL never constructs a `PartDesc`, never sets a property, never touches the database directly - it just hands the game a `TextAsset` and lets the vanilla loading code do the rest.
7. **SaveFix**: Once the full part list is known, the `SaveLoadSystem` detours inspect each save as it loads and remove any part whose `ID` is no longer present in the database - the leftover of an uninstalled mod - before the game can choke on it. Emptied slots are nulled and any water-cooling loops or tubing that referenced the removed part are pruned, so the rest of the build stays intact. The whole pass is SEH-guarded: if anything unexpected happens, the save is left exactly as it was. SaveFix can be disabled in `config/PCBS2.XPL.cfg`.
### Technical Flow
 
```
   ┌─────────────────────────────────────────────────────────────┐
   │ Setup Phase  (native init thread)                           │
   ├─────────────────────────────────────────────────────────────┤
   │                                                             │
   │     Game starts                                             │
   │         │                                                   │
   │         ▼                                                   │
   │     Windows loads version.dll (PCBS2.XPL)                   │
   │         │                                                   │
   │         ▼                                                   │
   │     Wait for GameAssembly.dll                               │
   │         │                                                   │
   │         ▼                                                   │
   │     Resolve IL2CPP runtime                                  │
   │         │                                                   │
   │         ▼                                                   │
   │     Hook il2cpp_runtime_invoke, defer the rest              │
   │                                                             │
   └─────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
   ┌─────────────────────────────────────────────────────────────┐
   │ Bootstrap Phase  (first managed call, GC-tracked thread)    │
   ├─────────────────────────────────────────────────────────────┤
   │                                                             │
   │     runtime_invoke detour fires once (CAS-gated)            │
   │         │                                                   │
   │         ├─────────────────┐                                 │
   │         ▼                 ▼                                 │
   │     Scan mods/         Install MinHook detours:             │
   │     recursively          • PartsDatabase.Load               │
   │     into queue           • SaveLoadSystem.LoadPC / LoadGame │
   │                          • PartInstance.FixForVersion       │
   │                                                             │
   └─────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
   ┌─────────────────────────────────────────────────────────────┐
   │ Injection Phase  (on PartsDatabase.Load)                    │
   ├─────────────────────────────────────────────────────────────┤
   │                                                             │
   │     Run original Load() - vanilla parts populate m_parts    │
   │         │                                                   │
   │         ▼                                                   │
   │     For each queued mod XML:                                │
   │       • Read file into memory                               │
   │       • Inject property header from div="..." attributes    │
   │       • Wrap content in a TextAsset                         │
   │       • Call PartsDatabase.ImportFromHTML(asset)            │
   │         │                                                   │
   │         ▼                                                   │
   │     Vanilla pipeline parses rows, builds PartDescs, adds    │
   │     them to m_parts - mods now indistinguishable from       │
   │     built-in content                                        │
   │                                                             │
   └─────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
   ┌─────────────────────────────────────────────────────────────┐
   │ SaveFix Phase  (on every save load)                         │
   ├─────────────────────────────────────────────────────────────┤
   │                                                             │
   │     Game calls SaveLoadSystem.LoadPC / LoadGame             │
   │         │                                                   │
   │         ▼                                                   │
   │     Scan the save for parts whose ID is no longer in        │
   │     the loaded database (leftovers of a removed mod)        │
   │         │                                                   │
   │         ▼                                                   │
   │     Prune them: null emptied slots, drop water-cooling      │
   │     loops / pipes that referenced them                      │
   │         │                                                   │
   │         ▼                                                   │
   │     Hand the cleaned save to the original method            │
   │     (SEH-guarded - on error the save is left untouched)     │
   │                                                             │
   └─────────────────────────────────────────────────────────────┘
```

## 🌟 Part Types
 
The `Part Type` field of each mod row must match a string the game's `PartDesc.Create` factory recognizes. PCBS2 Part Creator exports the correct strings automatically, so manual editing is rarely needed.
 
### Supported Part Type values
 
| Category        | Accepted `Part Type` values                                                                    |
|-----------------|------------------------------------------------------------------------------------------------|
| Core components | `GPU`, `WaterCooledGPU`, `CPU`, `RAM`, `Motherboard`, `PSU`, `Case`                            |
| Storage         | `HDD`, `SSD`, `M2`                                                                             |
| Cooling         | `Air Cooler`, `Liquid Cooler`, `Case Fan`, `Radiator`                                          |
| Water cooling   | `CPUBlock`, `GPUBlock`, `MotherboardBlock`, `MemoryBlock`, `Pump`, `PumpReservoir`, `Coolant`  |
| Connectivity    | `Cable`, `CableConnectors`, `Pipes`, `PipeConnectors`                                          |
| Peripherals     | `Monitor`, `Keyboard`, `Mouse`, `Mousepad`, `Headset`, `Microphone`                            |
| Accessories     | `Decoration`, `RamHeatsink`, `PowerSplitter`, `PowerAdapter`                                   |
| Software & misc | `Program`, `USB Drive`, `Tool`, `Tool Upgrade`                                                 |
 
> **Note**: Strings are case-sensitive and the spaces in multi-word values (`Air Cooler`, `Case Fan`, `Tool Upgrade`, `USB Drive`) are part of the name - omit or replace them and the game will reject the row.

## 📖 Usage Notes
 
This section is for mod authors and players setting up a `mods/` folder for the first time. If you only want to use existing mods, follow the [Installation](#-installation) steps - the notes below cover common questions that come up during authoring and distribution.
 
 
### Note 1: Adding a Single Part
 
Place `mods/MyCustomGPU.xml` in the mods folder. The filename doesn't matter - the game identifies parts by their `ID` field.  
Launch the game and the part appears in the shop and inventory.
 
 
### Note 2: One or Multiple Parts Per File
 
A mod XML can contain a single part or many. PCBS2.XPL builds the property header from the first data row's `div="..."` attributes, then every `<tr>` in the file becomes one part.  
The catch: all parts in one XML must be of the same `Part Type`, since they share the same generated header. A pack of fifteen GPUs in one XML is fine; mixing one GPU and one CPU in the same file is not - use two files instead.
 
 
### Note 3: Organizing the mods/ Folder
 
PCBS2.XPL scans `mods/` recursively, so subfolders are allowed and never required. Both layouts work:
 
```
mods/
  MyGPU.xml
  MyCPU.xml
```
 
```
mods/
  GPU/
    MyGPU.xml
  CPU/
    MyCPU.xml
```
 
Use whatever makes sense for your collection. The game only sees the parts inside the files, not their folder structure.
 
 
### Note 4: Shipping a Parts Pack
 
Drop all `.xml` files (and any subfolders) into `mods/` - no manifest, no installer, no load order. The game's pipeline handles every part type uniformly.  
To distribute the pack, zip the XMLs (with their folder structure if any) and let users extract them into their own `mods/` folder.
 
 
### Note 5: Overriding Vanilla Parts
 
If a mod row uses an `ID` that already exists in the parts database (vanilla or from another mod), the existing part is overwritten with the new values.  
This is useful for balance mods that want to tweak a specific vanilla part without redefining it from scratch. To **add** a new part instead of overriding, simply pick an `ID` that doesn't collide with anything in vanilla - PCBS2 Part Creator's `_Custom_######` suffix is one common convention.
 
 
### Note 6: Running Alongside Jelly's Socket Creator
 
[Jelly's Socket Creator](https://github.com/ZeOs360/JellysSocketCreator) also ships as `version.dll`, so both mods compete for the same slot.  
Keep PCBS2.XPL as `version.dll`, rename the Jelly's Socket Creator DLL to `JellysSockets.dll` in the same folder.  
PCBS2.XPL chain-loads it on startup - both run side by side.
 
 
### Note 7: Updating or Removing Mods
 
Modded parts are loaded fresh on every launch, so updating a mod is just replacing its XML file. Removing a mod is deleting the XML.  
Save files reference parts by `ID`. With **SaveFix** enabled (the default), removing a mod no longer breaks the save - on load, any part whose `ID` is no longer present is pruned out, emptied slots are cleared, and water-cooling loops or pipes that used the part are cleaned up, so the save still opens and the freed slots can be rebuilt. Keeping `ID`s stable across updates is still good practice, since a changed `ID` is treated as a removed part.  
SaveFix can be turned off in `config/PCBS2.XPL.cfg` (`SaveFix=false`); with it disabled, a save that references a missing modded part will fail to load that part, as before. See [Note 9](#note-9-savefix).
 
 
### Note 8: Duplicate IDs Across Files
 
If two XML files declare the same `ID`, the file loaded last wins (the earlier one is effectively overwritten).  
File load order follows the recursive directory walk, which is not strictly alphabetical. For predictable behavior, ensure every mod uses a unique `ID` unless you specifically intend to override another mod.
 
 
### Note 9: SaveFix
 
SaveFix keeps your saves loadable when a part mod is removed or its `ID` changes. Without it, the game throws as soon as it tries to load a part `ID` that no longer exists in the database, and the save won't open.
 
On every save load, SaveFix:
 
- Removes parts whose `ID` is no longer present - across the active PC plus parts in storage, on the bench, and being carried
- Clears the slots those parts occupied, so they read as empty and can be rebuilt
- Prunes any custom water-cooling loops and the tubing connected to a removed part, leaving the rest of the loop intact
 
It only ever runs once the full part database has loaded, so it can never remove a part by mistake, and the entire pass is SEH-guarded - if anything unexpected happens, the save is left untouched. Every cleanup is recorded in `PCBS2.XPL.log`. To disable it, set `SaveFix=false` in `config/PCBS2.XPL.cfg` (created automatically on first launch).
 
> **Save still won't load?** If a save doesn't open and SaveFix can't rescue it, please send me your `Player.log` on Discord so I can look into a fix. You'll find it at `%USERPROFILE%\AppData\LocalLow\Epic Games Publishing\PCBS2\Player.log`.


## 🐛 Troubleshooting
 
Most issues can be diagnosed from `PCBS2.XPL.log` in the game directory. Check it first - the prefixes (`[+]` success, `[~]` info, `[!]` warning, `[-]` error, `[=]` summary) indicate the severity of each event. For issues with the parts themselves, the game's own `Player.log` (under `%USERPROFILE%\AppData\LocalLow\Epic Games Publishing\PCBS2\`) often contains additional clues from the vanilla loading pipeline. If neither log helps, the categories below cover the common failure modes.
 
 
### Game Doesn't Start
 
**Symptoms**: Game window flashes and closes, or doesn't launch at all. No `PCBS2.XPL.log` is created.
 
**Solutions**:
 
- Verify `version.dll` is directly next to `PCBS2.exe`, not in a subfolder
- Confirm the build matches your platform - only an x64 build exists, 32-bit Windows is not supported
- Install the [Visual C++ Redistributable 2015-2026](https://visualstudio.microsoft.com/de/downloads/)
- Temporarily rename `version.dll` to confirm it's the cause. If the game still doesn't start, the issue is unrelated to PCBS2.XPL
- Antivirus software sometimes quarantines unknown DLLs in game directories - check the quarantine folder
### No Log File Is Created
 
**Symptoms**: Game launches normally, but `PCBS2.XPL.log` doesn't appear.
 
**Solutions**:
 
- The proxy isn't loading. Verify the filename is exactly `version.dll`, not `version (1).dll` or similar
- The game folder must be writable. Epic Games libraries under `Program Files` may require running the game once as administrator, or moving the library to a user-writable location
- A leftover `JellysSockets.dll` from a previous mod can also occupy the slot - see [Note 6](#note-6-running-alongside-jellys-socket-creator)
### Mods Don't Appear In-Game
 
**Symptoms**: The log shows `[=] Mod load complete: N imported, 0 failed`, but the modded parts aren't visible in the shop or inventory.
 
**Solutions**:
 
1. **Check Player.log**. The game's `HTMLTableReader` logs a `Couldn't find parts in <name>` warning when an XML's structure doesn't match what it expects. Open `%USERPROFILE%\AppData\LocalLow\Epic Games Publishing\PCBS2\Player.log` and search for "Couldn't find parts" - if present, the XML format is off
3. **Verify `In Game = Yes`**. Rows where `In Game` is empty, `No`, or missing are silently dropped by the game's loader, even if everything else is correct
4. **Confirm the `Part Type` is supported**. If the type is one of the [unsupported values](#-currently-unsupported-part-type-values), the part will never appear - the loader has no class to instantiate
5. **Watch for ImportFromHTML crashes**. `[-] ImportFromHTML crashed for: <file>` in `PCBS2.XPL.log` indicates the XML caused an exception inside the game's loader. Compare structurally against a known-working PCBS2 Part Creator export
### Cold-Start Crash
 
**Symptoms**: The first launch after a reboot crashes; subsequent launches work fine. (Fixed in 1.0.2.)
 
**Solutions**:
 
- This was caused by PCBS2.XPL doing IL2CPP work on its native init thread, which the runtime's garbage collector doesn't track. As of 1.0.2 the bootstrap is deferred onto a real, GC-tracked game thread via an `il2cpp_runtime_invoke` hook, which resolves it - make sure you're on 1.0.2 or newer
- If it still occurs on a current build, the timing window may be unusual on your system. Open an [issue](../../issues) with the log and approximate hardware specs
### "Class Not Found" Errors
 
**Symptoms**: Log lines like `[-] PartsDatabase class not found` or `[-] UnityEngine.TextAsset class not found`.
 
**Solutions**:
 
- A game update changed the IL2CPP class layout. Check the [Issues](../../issues) page for a tracking ticket, or wait for an updated PCBS2.XPL release
- Confirm you're running the official Epic Games release - modified or pirated builds may have stripped or renamed classes
### Hook Installation Failed
 
**Symptoms**: `[-] CreateHook on PartsDatabase.Load failed (MH_STATUS=...)` in the log.
 
**Solutions**:
 
- Another mod may have already hooked the same method. Disable other DLL-based mods one at a time to identify the conflict
- For any other status code, open an [issue](../../issues) with the full log attached
### Game Crashes Mid-Injection
 
**Symptoms**: Game crashes shortly after launch. The log shows several `[+] Imported:` lines, then ends abruptly.
 
**Solutions**:
 
- The last `[+] Imported:` line before the crash points to the *previous* successful mod - the failing one is the next in queue. To identify it, move all XMLs out of `mods/`, then add them back in halves until the crash returns
- Open the suspected XML and verify it has all required fields - comparing against a vanilla part exported through PCBS2 Part Creator is the fastest cross-check
- A missing or invalid `Asset Path` is a frequent culprit: the part loads, but crashes the game when it later tries to instantiate the (nonexistent) prefab
- If the crash persists with a single isolated XML, the file is the cause - report it to the mod's author

## ©️ Credits

PCBS2.XPL bundles the following third-party libraries:
 
- **[MinHook](https://github.com/TsudaKageyu/minhook)** by Tsuda Kageyu - minimal x86/x64 API hooking library, used for the `PartsDatabase.Load` detour [(BSD-2-Clause)](https://opensource.org/license/BSD-2-Clause)

---
<div align="center">
<strong>Made with ❤️ by anonymus637</strong>
</div>
