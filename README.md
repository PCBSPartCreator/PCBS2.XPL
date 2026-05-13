<div align="center">

<img src="assets/PCBS2_XPL_Logo.png" alt="PCBS2.XPL Logo" width="160" />

# PC Building Simulator 2 - XML Part Loader

[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](../../releases)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg)]()
[![Game](https://img.shields.io/badge/game-PC%20Building%20Simulator%202-orange.svg)](https://www.pcbuildingsim.com/)
[![C++](https://img.shields.io/badge/C%2B%2B-14-00599C.svg?logo=cplusplus)]()

A `version.dll` proxy that injects custom parts into PC Building Simulator 2 at startup.  
Designed as a companion to [PCBS2 Part Creator](https://www.nexusmods.com/pcbuildingsimulator2/mods/102), which produces compatible XML files through a visual editor.  
PCBS2.XPL reads XML part definitions from the `mods/` folder and registers them with the game's `PartsDatabase` so they appear alongside the built-in catalog.

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
- ✅ **All Part Categories**: Supports every part type the game uses - RAM, CPU, GPU, Storage, PSU, Motherboard & [much more](#-part-types)
- ✅ **Native Behavior**: Reuses the game's own `ImportProp` and `AddNewPart` methods, so injected parts behave identically to vanilla ones
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
3. Launch the game once. **PCBS2.XPL** will create a `mods/` folder in the same directory if it doesn't exist.

> **Note**If you already have [Jelly's Socket Creator](https://github.com/ZeOs360/JellysSocketCreator) installed (also as `version.dll`), rename it to `JellysSockets.dll` first, then place **PCBS2.XPL** as the new `version.dll`. **PCBS2.XPL** will chain-load JellysSockets automatically - see [Note 6](#note-6-running-alongside-jellys-socket-creator).


### Step 2: Add Mod Files

1. Place `.xml` part files into the `mods/` folder.
2. The easiest way to produce valid files is through [PCBS2 Part Creator](https://www.nexusmods.com/pcbuildingsimulator2/mods/102).
3. Launch the game.


### Verification

Check `PCBS2.XPL.log` next to the game executable for entries like:

```text
[+] IL2CPP API loaded
[+] Hooked: PartDescGPU
[+] Hooked: PartDescCPU
...
[+] Loaded: MyCustomCard_RTX6090 (GPU)
[+] Total mods: 1
[+] Ready
[+] Injected: MyCustomCard_RTX6090 (61 props)
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
| `dllmain.cpp`  | `version.dll` export forwards, DllMain, init thread      |
| `il2cpp.*`     | IL2CPP API loader and class/method lookup helpers        |
| `hooks.*`      | MinHook setup for `ImportProp` on every `PartDesc*` class|
| `config.*`     | XML mod file parsing and part-type normalization         |
| `logger.*`     | Thread-safe logger writing to `PCBS2.XPL.log`            |


## 📄 Mod XML Format

PCBS2.XPL reads the same XML layout the game uses for its own parts. Every file must define at minimum a `Part Type` and an `ID`.

### File Format

```xml
AirCooledGPU_AMD AMD FirePro S9170 32GB
<table>
    <tr>
        <td div=""></td>
        <td div="Part Type">GPU</td>
        <td div="Class">Modded</td>
        <td div="Manufacturer">AMD</td>
        <td div="ID">AirCooledGPU_Custom_659391</td>
        <td div="Part Name">AMD FirePro S9170</td>
        <td div="Price">2000</td>
        <td div="Level">8</td>
        <!-- additional properties as needed -->
    </tr>
</table>
```


### Format Rules

- **Property Name**: Goes inside the `div="..."` attribute, case-sensitive
- **Property Value**: Goes between `<td>` and `</td>`
- **Whitespace**: Leading and trailing spaces in values are automatically trimmed
- **HTML Entities**: `&amp;` `&quot;` `&lt;` `&gt;` `&apos;` are decoded automatically
- **Required Fields**: `Part Type` and `ID` must both be present, otherwise the file is skipped
- **Duplicates**: If the same `ID` appears in multiple files, the first occurrence wins


## 🔬 How It Works
 
PCBS2.XPL is a proxy DLL that loads into the game process at startup, then uses [MinHook](https://github.com/TsudaKageyu/minhook) to intercept the game's own part-loading code and inject mod XMLs through it.
 
1. **Proxy Load**: PCBS2.XPL is named `version.dll` and placed next to `PCBS2.exe`. Windows loads DLLs from the program's own folder before the system folder, so the game loads PCBS2.XPL instead of the real one. All exports are forwarded to `C:\Windows\System32\version.dll`.
2. **Wait for the Game**: A background thread waits for `GameAssembly.dll` to load, then resolves the IL2CPP runtime API used to look up game classes by name.
3. **Install Hooks**: MinHook places a detour on the `ImportProp` method of every `PartDesc*` class. Several PartDesc classes share the same compiled `ImportProp` body - those reuse the existing trampoline.
4. **Read Mods**: XML files in `mods/` are parsed and queued in memory.
5. **Injection**: The first time the game calls `ImportProp` on a part of a given type, the detour fires. PCBS2.XPL constructs a new `PartDesc*` for each queued mod of that type, replays every property through the original `ImportProp`, and registers the finished part via `PartsDatabase.AddNewPart`.

### Technical Flow
 
```
   ┌─────────────────────────────────────────────────────────────┐
   │  Setup Phase                                                │
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
   │         ├─────────────────────────┐                         │
   │         ▼                         ▼                         │
   │     Parse mods/*.xml         Install MinHook                │
   │     into queue               detours on ImportProp          │
   │                                                             │
   └─────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  Injection Phase                                            │
   ├─────────────────────────────────────────────────────────────┤
   │                                                             │
   │     Game calls ImportProp on a vanilla part                 │
   │         │                                                   │
   │         ▼                                                   │
   │     Detour fires (once per part type)                       │
   │         │                                                   │
   │         ▼                                                   │
   │     For each queued mod of this type:                       │
   │       • new PartDesc*()                                     │
   │       • replay properties via original ImportProp           │
   │       • PartsDatabase.AddNewPart(id, part)                  │
   │         │                                                   │
   │         ▼                                                   │
   │     Original ImportProp continues                           │
   │                                                             │
   └─────────────────────────────────────────────────────────────┘
```

## 🌟 Part Types

### Supported Part Type values
| Category        | Accepted `Part Type` values                                                       |
|-----------------|-----------------------------------------------------------------------------------|
| Core components | `GPU`, `WaterCooledGPU`, `CPU`, `RAM`, `Motherboard`, `PSU`, `Case`               |
| Storage         | `Storage`, `HDD`, `SSD`, `M2`, `M.2`                                              |
| Cooling         | `Cooler`, `AirCooler`, `Air Cooler`, `LiquidCooler`, `Liquid Cooler`              |
| Water cooling   | `CPUBlock`, `GPUBlock`, `MotherboardBlock`, `MemoryBlock`, `Pump`, `Reservoir`, `PumpReservoir`, `Coolant` |
| Peripherals     | `Monitor`, `Keyboard`, `Mouse`, `MousePad`, `Mousepad`, `Headset`, `Microphone`   |
| Connectivity    | `Cable`, `CableConnector`, `CableConnectors`, `Pipe`, `Pipes`, `PipeConnector`, `PipeConnectors` |
| Accessories     | `Tool`, `Decoration`, `LEDStrip`, `RamHeatsink`, `PowerSplitter`, `PowerAdapter`  |

### ⚠️ Currently Unsupported Part Type values
The following part types are recognized by PCBS2 Part Creator but cannot be injected through PCBS2.XPL, because the game does not load them via its `ImportProp` mechanism:
 
- **Case Fan**, **Radiator**, **Pipes** - these exist in-game only as Unity components, not as standalone XML-loadable part definitions.
- **Program**, **USB Drive**, **Tool Upgrade** - handled internally as specialized `Tool` variants or as Unity prefabs; they have no dedicated `PartDesc` class with an `ImportProp` method.

If a mod XML uses one of these types, the file will be parsed but no matching hook will fire, and the part will silently not appear in-game.  
The log will show `[+] Loaded: <id> (<type>)` but no corresponding `[+] Injected: <id>` line.  
For these part types, use PCBS2 Part Creator's direct patch mode (which writes to the game's `.assets` files) instead of the XML mod loader.

## 📖 Usage Notes
 
This section is for mod authors and players setting up a `mods/` folder for the first time. If you only want to use existing mods, follow the [Installation](#-installation) steps - the notes below cover common questions that come up during authoring and distribution.
 
 
### Note 1: Adding a Single Part
 
Place `mods/MyCustomGPU.xml` in the mods folder. The filename doesn't matter - PCBS2.XPL identifies parts by their `ID` field.  
Launch the game and the part appears in the shop and inventory.
 
 
### Note 2: One Part Per File
 
PCBS2.XPL expects exactly one part per XML file. The parser walks every `<td div="...">value</td>` tag in the file and merges all properties into a single part  
If a file contains multiple `<tr>` blocks, the later `ID` and `Part Type` values overwrite the earlier ones, and only the last part is loaded.  
For multiple parts, use multiple files: one `.xml` per part.
 
 
### Note 3: Shipping a Parts Pack
 
Drop all `.xml` files into `mods/` - no manifest, no installer, no load order. Each file's `Part Type` decides which hook handles it.  
To distribute the pack, zip the XMLs and let users extract them into their own `mods/` folder.
 
 
### Note 4: Storage Variants
 
The game has a single `Storage` category internally, but PCBS2 Part Creator exports SSDs, HDDs, and M.2 drives with distinct `Part Type` values.  
All four (`SSD`, `HDD`, `M2`, `M.2`) are normalized to `Storage` on load, so XMLs from any source work without rewriting.
 
 
### Note 5: Water-Cooled GPUs
 
PCBS2 has no separate `PartDescWaterCooledGPU` class - water-cooled GPUs use the regular `GPU` class with extra properties.  
`Part Type = WaterCooledGPU` is normalized to `GPU` automatically, so PCBS2 Part Creator exports work directly.
 
 
### Note 6: Running Alongside Jelly's Socket Creator
 
[Jelly's Socket Creator](https://github.com/ZeOs360/JellysSocketCreator) also ships as `version.dll`, so both mods compete for the same slot.  
Keep PCBS2.XPL as `version.dll`, rename the Jelly's Socket Creator DLL to `JellysSockets.dll` in the same folder.  
PCBS2.XPL chain-loads it on startup - both run side by side.
 
 
### Note 7: Updating or Removing Mods
 
Modded parts are loaded fresh on every launch, so updating a mod is just replacing its XML file. Removing a mod is deleting the XML.  
Save files reference parts by `ID`, so if a save was made with a modded part and the mod is later removed, the save will fail to load that specific part - keep `ID`s stable across updates.
 
 
### Note 8: Duplicate IDs Across Files
 
If two XML files declare the same `ID`, only the first one loaded wins.  
The duplicate is logged as `[-] Duplicate mod ID: <id> (<filename>) - skipped` and ignored.  
File load order is determined by Windows' `FindFirstFile`, which is not strictly alphabetical - for predictable behavior, ensure every mod uses a unique `ID`.


## 🐛 Troubleshooting
 
Most issues can be diagnosed from `PCBS2.XPL.log` in the game directory. Check it first - the prefixes (`[+]` success, `[~]` info, `[!]` warning, `[-]` error) indicate the severity of each event. If the log doesn't help, the categories below cover the common failure modes.
 
 
### Game Doesn't Start
 
**Symptoms**: Game window flashes and closes, or doesn't launch at all. No `PCBS2.XPL.log` is created.
 
**Solutions**:
 
- Verify `version.dll` is directly next to `PCBS2.exe`, not in a subfolder
- Confirm the build matches your platform - only an x64 build exists, 32-bit Windows is not supported
- Install the [Visual C++ Redistributable 2015-2026](https://visualstudio.microsoft.com/de/downloads/)
- Temporarily rename `version.dll` to confirm it's the cause. If the game still doesn't start, the issue is unrelated to PCBS2.XPL
- Antivirus software sometimes quarantines unknown DLLs in game directories — check the quarantine folder

### No Log File Is Created
 
**Symptoms**: Game launches normally, but `PCBS2.XPL.log` doesn't appear.
 
**Solutions**:
 
- The proxy isn't loading. Verify the filename is exactly `version.dll`, not `version (1).dll` or similar
- The game folder must be writable. Epic Games libraries under `Program Files` may require running the game once as administrator, or moving the library to a user-writable location
- A leftover `JellysSockets.dll` from a previous mod can also occupy the slot - see [Note 6](#note-6-running-alongside-jellys-socket-creator)

### Mods Don't Appear In-Game
 
**Symptoms**: The log shows `[+] Ready` and `[+] Total mods: N`, but the modded parts aren't visible in the shop or inventory.
 
**Solutions**:
 
1. **Check for `[+] Injected:` lines**. If a mod appears as `[+] Loaded:` but never `[+] Injected:`, its part type has no matching hook - typically because the type isn't supported (see [Currently Unsupported Part Type values](#-currently-unsupported-part-type-values))
2. **Verify `Part Type` spelling**. Values are case-sensitive: `gpu` is not recognized, `GPU` is. PCBS2 Part Creator's spaced variants like `Air Cooler` are accepted, see [Supported Part Type values](#supported-part-type-values)
3. **Inspect the property count**. `[+] Injected: <id> (0 props)` means every property was rejected by the game - the XML is structurally valid but contains no recognized field names. Compare against a known-working export
4. **Look for crashes during injection**. `[-] AddNewPart crashed for: <id>` indicates the part itself broke the game's database call. Most often caused by missing required fields like `Asset Path` or `Concat name`

### Cold-Start Crash
 
**Symptoms**: The first launch after a reboot crashes; subsequent launches work fine. The log may contain `Collecting from unknown thread`.
 
**Solutions**:
 
- This is a race between MinHook's thread-suspension logic and Unity's thread pool initialization. PCBS2.XPL polls for `PartDesc*` to delay hook installation until IL2CPP metadata is populated, which fixes it in nearly all cases
- If it still occurs, the timing window may be tighter on your system. Open an [issue](../../issues) with the log and approximate hardware specs

### "Class Not Found" Errors
 
**Symptoms**: Log lines like `[-] Class not found: PartDesc*`.
 
**Solutions**:
 
- A game update changed the IL2CPP class layout. Check the [Issues](../../issues) page for a tracking ticket, or wait for an updated PCBS2.XPL release
- Confirm you're running the official Epic Games release - modified or pirated builds may have stripped or renamed classes

### Hook Installation Failed
 
**Symptoms**: `[-] CreateHook failed for: <ClassName> (MH_STATUS=...)` in the log.
 
**Solutions**:
 
- Another mod may have already hooked the same method. Disable other DLL-based mods one at a time to identify the conflict
- A `[~] <X> shares ImportProp with <Y>` line for the same class is not an error — several PartDesc classes share the same compiled `ImportProp` body, and PCBS2.XPL reuses the existing detour transparently
- For any other status code, open an [issue](../../issues) with the full log attached

### Game Crashes Mid-Injection
 
**Symptoms**: Game crashes shortly after launch. The log shows several `[+] Injected:` lines, then ends abruptly.
 
**Solutions**:
 
- The last `[+] Injected:` line before the crash points to the *previous* successful mod - the failing one is the next in queue. To identify it, move all XMLs out of `mods/`, then add them back in halves until the crash returns
- Open the suspected XML and verify it has all required fields for its part type - comparing against a vanilla part exported through PCBS2 Part Creator is the fastest cross-check
- If the crash persists with a single isolated XML, the file is the cause - report it to the mod's author

## ©️ Credits

PCBS2.XPL bundles the following third-party libraries:
 
- **[MinHook](https://github.com/TsudaKageyu/minhook)** by Tsuda Kageyu - minimal x86/x64 API hooking library, used for the `ImportProp` detours [(BSD-2-Clause)](https://opensource.org/license/BSD-2-Clause)  

---
<div align="center">
<strong>Made with ❤️ by anonymus637</strong>
</div>