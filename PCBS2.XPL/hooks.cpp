#include "hooks.h"
#include "logger.h"
#include "il2cpp.h"
#include <MinHook.h>
#include <mutex>
#include <set>
#include <atomic>
#include <string>

typedef void (*PartsDatabase_Load_t)(void* self);
typedef void (*PartsDatabase_ImportFromHTML_t)(void* self, void* asset);
typedef void (*TextAsset_Ctor_t)(void* self, int options, Il2CppString* text);
typedef Il2CppString* (*TextAsset_GetText_t)(void* self);

static std::vector<ModFile>* g_pendingMods = nullptr;
static PartsDatabase_Load_t g_originalLoad = nullptr;
static PartsDatabase_ImportFromHTML_t g_importFromHTML = nullptr;   // trampoline (original)
static Il2CppClass* g_textAssetClass = nullptr;
static TextAsset_Ctor_t g_textAssetCtor = nullptr;
static TextAsset_GetText_t g_textAssetGetText = nullptr;
static bool g_modsLoaded = false;
static bool g_saveFixEnabled = true;   // set from config before Hooks_Install

// --- GC thread guard --------------------------------------------------------
// Managed allocation on an unregistered thread can crash the Boehm GC
// ("Collecting from unknown thread"), so attach if needed and detach only what
// we attached (never the main thread).
struct GcThreadGuard {
    void* attached = nullptr;
    GcThreadGuard() {
        if (il2cpp_thread_current && il2cpp_thread_attach && il2cpp_domain_get) {
            if (!il2cpp_thread_current()) {
                Il2CppDomain* d = il2cpp_domain_get();
                if (d) attached = il2cpp_thread_attach(d);
            }
        }
    }
    ~GcThreadGuard() {
        if (attached && il2cpp_thread_detach) il2cpp_thread_detach(attached);
    }
};

// --- small utilities ---------------------------------------------------------
static std::string Utf16ToUtf8(Il2CppString* s) {
    if (!s || s->length <= 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s->chars, s->length, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s->chars, s->length, &out[0], n, nullptr, nullptr);
    return out;
}

// Value of div="..." inside a tag, or empty.
static std::string DivAttr(const std::string& tag) {
    size_t dp = tag.find("div=\"");
    if (dp == std::string::npos) return {};
    size_t vs = dp + 5;
    size_t ve = tag.find('"', vs);
    return (ve == std::string::npos) ? std::string{} : tag.substr(vs, ve - vs);
}

// Calls fn(tagAttrs, contentBegin) for each <td ...> in row. fn returns false to stop.
template <class F>
static void ForEachTd(const std::string& row, F fn) {
    size_t p = 0;
    while ((p = row.find("<td", p)) != std::string::npos) {
        size_t tagEnd = row.find(">", p);
        if (tagEnd == std::string::npos) return;
        if (!fn(row.substr(p, tagEnd - p), tagEnd + 1)) return;
        p = tagEnd + 1;
    }
}

// --- Part ID collection (in-memory whitelist; no disk dump) -----------------
static std::mutex   g_idMutex;
static int          g_totalIdCount = 0;
static bool         g_idsComplete = false;
static std::set<std::string> g_validIds;

const std::set<std::string>& Hooks_GetValidIds() { return g_validIds; }
bool Hooks_PartIdsReady() { return g_idsComplete; }

// Sanity floor: never prune against a suspiciously small whitelist. If stock
// part collection partially fails (e.g. a game update changes the HTML format),
// g_validIds would be "complete" but short, and SaveFix would shred real builds.
// Stock is ~2777.
static const size_t kMinWhitelist = 1000;
static bool SaveFixWhitelistOK() {
    return Hooks_PartIdsReady() && g_validIds.size() >= kMinWhitelist;
}

// Deserialize-phase prunes fire in bursts of dozens of callbacks; tally instead
// of logging each, flush one summary at the next load entry. Atomics because
// OnAfterDeserialize may run on a worker thread; the flush is best-effort
// logging, so a lost increment in the read/reset window is harmless.
static std::atomic<int> g_deserParts{ 0 };  // parts removed during deserialize
static std::atomic<int> g_deserPCs{ 0 };    // PCs that had >=1 removal
static std::atomic<int> g_deserJobs{ 0 };   // jobs that had >=1 removal
static void FlushDeserTally() {
    int parts = g_deserParts.exchange(0);
    int pcs = g_deserPCs.exchange(0);
    int jobs = g_deserJobs.exchange(0);
    if (parts > 0)
        Logger::Log("[=] SaveFix: removed " + std::to_string(parts) +
            " orphaned part(s) during deserialize (" + std::to_string(pcs) +
            " PCs, " + std::to_string(jobs) + " jobs)");
}

// Column names treated as the part ID; extend if the HTML uses another.
static const char* const kIdColumns[] = {
    "ID", "Id", "id", "PartID", "PartId", "partId", "partID", "Code"
};

static bool IsIdColumn(const std::string& col) {
    for (const char* c : kIdColumns) if (col == c) return true;
    return false;
}

// Reads the ID column out of each <td div="Col">value</td> data row.
static void CollectPartIds(const std::string& html, const std::string& source) {
    std::lock_guard<std::mutex> lock(g_idMutex);
    if (g_idsComplete) return;

    int count = 0;
    int rows = 0;
    std::string firstDataRow;

    size_t pos = 0;
    while ((pos = html.find("<tr>", pos)) != std::string::npos) {
        size_t rowEnd = html.find("</tr>", pos);
        if (rowEnd == std::string::npos) break;
        std::string row = html.substr(pos + 4, rowEnd - (pos + 4));
        pos = rowEnd + 5;

        // Only data rows carry div="ColName" on their cells; skip header rows.
        if (row.find("div=\"") == std::string::npos) continue;
        rows++;
        if (firstDataRow.empty()) firstDataRow = row.substr(0, 300);

        std::string id;
        ForEachTd(row, [&](const std::string& tag, size_t contentBegin) {
            if (!IsIdColumn(DivAttr(tag))) return true;
            size_t valEnd = row.find("</td>", contentBegin);
            if (valEnd != std::string::npos)
                id = row.substr(contentBegin, valEnd - contentBegin);
            return false;
            });

        if (!id.empty()) { g_validIds.insert(id); count++; }
    }

    g_totalIdCount += count;
    if (count == 0 && rows > 0) {
        // No column matched; log the first row so kIdColumns can be fixed.
        Logger::Log("[!] No ID column matched in '" + source +
            "' - adjust kIdColumns. First data row: " + firstDataRow);
    }
}

// SEH-isolated (no C++ objects here, so no unwinding conflict).
static Il2CppString* SafeGetText(void* asset) {
    __try { return g_textAssetGetText(asset); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static std::string ReadTextAsset(void* asset) {
    if (!g_textAssetGetText || !asset) return {};
    return Utf16ToUtf8(SafeGetText(asset));
}

// --- injection --------------------------------------------------------------
static std::string ReadFile(const std::string& path) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) { CloseHandle(h); return {}; }
    std::string out((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    ::ReadFile(h, &out[0], (DWORD)sz.QuadPart, &got, nullptr);
    CloseHandle(h);
    out.resize(got);
    return out;
}

static void* CreateTextAsset(const std::string& content) {
    if (!g_textAssetClass || !g_textAssetCtor) return nullptr;

    Il2CppObject* obj = il2cpp_object_new(g_textAssetClass);
    if (!obj) return nullptr;
    il2cpp_runtime_object_init(obj);

    Il2CppString* text = il2cpp_string_new(content.c_str());
    if (!text) return nullptr;

    g_textAssetCtor(obj, 1, text);
    return obj;
}

static bool SafeImportFromHTML(void* db, void* asset) {
    __try {
        g_importFromHTML(db, asset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Header rows have empty div="" cells; a non-empty div value in the first <tr>
// means it's already a data row, i.e. the file has no header.
static bool HasHeaderRow(const std::string& content) {
    size_t firstTr = content.find("<tr>");
    if (firstTr == std::string::npos) return true;
    size_t secondTr = content.find("<tr>", firstTr + 4);
    size_t endOfFirstTr = (secondTr == std::string::npos)
        ? content.find("</tr>", firstTr) : secondTr;
    if (endOfFirstTr == std::string::npos) return true;

    std::string firstRow = content.substr(firstTr, endOfFirstTr - firstTr);

    size_t p = 0;
    while ((p = firstRow.find("div=\"", p)) != std::string::npos) {
        p += 5;
        if (p < firstRow.size() && firstRow[p] != '"') return false;
        p++;
    }
    return true;
}

static std::string BuildHeaderFromDataRow(const std::string& firstRow) {
    std::string header = "<tr>";
    ForEachTd(firstRow, [&](const std::string& tag, size_t) {
        header += "<td>" + DivAttr(tag) + "</td>";
        return true;
        });
    header += "</tr>";
    return header;
}

static std::string EnsureHeader(const std::string& content, const std::string& fileName) {
    if (HasHeaderRow(content)) return content;

    size_t firstTr = content.find("<tr>");
    if (firstTr == std::string::npos) return content;
    size_t endOfFirstTr = content.find("</tr>", firstTr);
    if (endOfFirstTr == std::string::npos) return content;

    std::string firstRow = content.substr(firstTr, endOfFirstTr + 5 - firstTr);
    std::string header = BuildHeaderFromDataRow(firstRow);

    size_t tablePos = content.find("<table>");
    if (tablePos == std::string::npos) return content;

    std::string result = content;
    result.insert(tablePos + 7, header);
    Logger::Log("[+] Injected dynamic header for: " + fileName);
    return result;
}

static void LoadAllMods(void* dbInstance) {
    if (g_modsLoaded) return;
    if (!g_pendingMods || g_pendingMods->empty()) return;
    if (!g_importFromHTML) return;

    int loaded = 0;
    int failed = 0;
    for (const auto& mod : *g_pendingMods) {
        std::string content = ReadFile(mod.fullPath);
        if (content.empty()) {
            Logger::Log("[-] Empty or unreadable: " + mod.fileName);
            failed++;
            continue;
        }

        content = EnsureHeader(content, mod.fileName);

        void* asset = CreateTextAsset(content);
        if (!asset) {
            Logger::Log("[-] TextAsset creation failed: " + mod.fileName);
            failed++;
            continue;
        }

        if (SafeImportFromHTML(dbInstance, asset)) {
            loaded++;
            Logger::Log("[+] Imported: " + mod.fileName);
            // Trampoline bypasses the collect hook, so collect injected IDs here.
            CollectPartIds(content, "mod: " + mod.fileName);
        }
        else {
            Logger::Log("[-] ImportFromHTML crashed for: " + mod.fileName);
            failed++;
        }
    }

    g_modsLoaded = true;

    Logger::Log("[=] Mod load complete: " + std::to_string(loaded) +
        " imported, " + std::to_string(failed) + " failed");
}

// --- hooks ------------------------------------------------------------------
static void Hook_PDB_Load(void* self) {
    GcThreadGuard guard;   // may run on an unregistered worker thread
    g_originalLoad(self);
    LoadAllMods(self);

    std::lock_guard<std::mutex> lock(g_idMutex);
    if (!g_idsComplete) {
        Logger::Log("[=] Loaded Parts: " + std::to_string(g_totalIdCount));
        if (g_validIds.size() < kMinWhitelist)
            Logger::Log("[!] SaveFix DISABLED: whitelist too small (" +
                std::to_string(g_validIds.size()) + " < " + std::to_string(kMinWhitelist) +
                ") - part loading likely failed; not pruning to avoid deleting valid parts");
        g_idsComplete = true;
    }
}

// Fires for stock parts (game-driven ImportFromHTML). Our injection uses the
// trampoline directly, so it doesn't re-enter here.
static void Hook_ImportFromHTML(void* self, void* asset) {
    GcThreadGuard guard;
    if (asset) {
        std::string html = ReadTextAsset(asset);
        if (!html.empty()) CollectPartIds(html, "game (stock)");
    }
    g_importFromHTML(self, asset);
}

// --- SaveFix: prune orphaned parts from a ComputerSave at load --------------
// Orphan = PartInstance.m_partId not in g_validIds. Hooked at LoadPC before the
// game resolves IDs (it would NRE). In-memory only; the save file is untouched.
typedef void (*SaveLoadSystem_LoadPC_t)(void* self, void* save, void* header,
    void* compSave, void* slot, bool powered, void* mi);
static SaveLoadSystem_LoadPC_t g_loadPC_orig = nullptr;
typedef void (*SaveLoadSystem_LoadGame_t)(void* self, void* name, void* header,
    void* saveGame, void* mi);
static SaveLoadSystem_LoadGame_t g_loadGame_orig = nullptr;
typedef bool (*PartInstance_FixForVersion_t)(void* self, int version, bool csOnly, void* mi);
static PartInstance_FixForVersion_t g_fixForVersion_orig = nullptr;
typedef bool (*VC_CheckBiosError_t)(void* self, void* mi);
static VC_CheckBiosError_t g_checkBiosError_orig = nullptr;
typedef void (*CS_OnAfterDeserialize_t)(void* self, void* mi);
static CS_OnAfterDeserialize_t g_onAfterDeser_orig = nullptr;

// IL2CPP object-array layout (x64): klass, monitor, bounds, max_length, [data].
struct Il2CppArrayHeader { void* klass; void* monitor; void* bounds; uintptr_t max_length; };
static uintptr_t ArrLen(void* a) { return a ? ((Il2CppArrayHeader*)a)->max_length : 0; }
static void** ArrData(void* a) { return (void**)((uint8_t*)a + sizeof(Il2CppArrayHeader)); }

static Il2CppClass* g_partInstanceClass = nullptr;
static Il2CppClass* g_waterPipeSaveClass = nullptr;

struct CSOff {
    int caseID, motherboardID, cpuID, cpuCoolerID, psuID;            // scalar PartInstance
    int psuSplitterIDs, storageSlots, caseFanSlots, radiatorSlots;   // PartInstance[]
    int reservoirSlots, pciSlots, ramSlots, usbSlots, m2Slots, powerAdapterIDs;
    int m_loops, pipesInstalled;
};
static CSOff g_cs{};
static int  g_off_partId = -1;   // PartInstance.m_partId
static int  g_off_partFans = -1;   // PartInstance.m_caseFanSlots
static int  g_off_loopComps = -1;   // WaterLoopSave.m_components
static int  g_off_pipeFrom = -1;   // WaterPipeSave.m_from.m_component (first field of ConnectorId)
static int  g_off_pipeTo = -1;   // WaterPipeSave.m_to.m_component
static bool g_fixReady = false;
static int  g_cs_status = -1;          // ComputerSave.m_status (ComputerCareerStatus)
static int  g_ccs_preloadedWb = -1;    // ComputerCareerStatus.preloadedWaterblocks (PartInstance[])
static int  g_ccs_brokenParts = -1;    // ComputerCareerStatus.brokenParts (PartInstance[])
static int  g_job_startComputer = -1;  // Job.m_startComputer (ComputerSave, serialized @0xD0)
static int  g_job_finishComputer = -1; // Job.m_finishComputer (ComputerSave, NonSerialized @0xD8)
// SaveGame container offsets
static int  g_sg_inStorage = -1;   // List<ComputerSave> computersInStorage
static int  g_sg_onBenches = -1;   // List<ComputerSave> computersOnBenches
static int  g_sg_carrying = -1;   // ComputerSave carryingComputer
static int  g_sg_pkg = -1;   // List<PartInstance> carryingPackageContents
static int  g_sg_careerState = -1; // SaveGame.careerState (CareerStatus.State)

static int FieldOff(Il2CppClass* k, const char* name) {
    if (!k) return -1;
    Il2CppFieldInfo* f = il2cpp_class_get_field_from_name(k, name);
    return f ? (int)il2cpp_field_get_offset(f) : -1;
}
static inline void* GetRef(void* obj, int off) {
    return (obj && off >= 0) ? *(void**)((uint8_t*)obj + off) : nullptr;
}
static inline void SetRef(void* obj, int off, void* v) {
    if (obj && off >= 0) *(void**)((uint8_t*)obj + off) = v;
}

// Direct view into a managed List<T>'s backing array (_items/_size).
struct ManagedList {
    void* list = nullptr;
    void** items = nullptr;
    int size = 0;
    int sizeOff = -1;
    bool valid() const { return items != nullptr; }
};
static ManagedList OpenList(void* list) {
    ManagedList v;
    if (!list) return v;
    Il2CppClass* lk = *(Il2CppClass**)list;
    int io = FieldOff(lk, "_items"), so = FieldOff(lk, "_size");
    if (io < 0 || so < 0) return v;
    void* items = GetRef(list, io);
    int sz = *(int*)((uint8_t*)list + so);
    if (!items || sz <= 0) return v;
    v.list = list;
    v.items = ArrData(items);
    v.size = sz;
    v.sizeOff = so;
    return v;
}
// Replaces the list content with `keep`, nulls the tail, fixes _size.
static void ShrinkList(const ManagedList& v, const std::vector<void*>& keep) {
    for (size_t i = 0; i < keep.size(); i++) v.items[i] = keep[i];
    for (int i = (int)keep.size(); i < v.size; i++) v.items[i] = nullptr;
    *(int*)((uint8_t*)v.list + v.sizeOff) = (int)keep.size();
}

static std::string PartId(void* pi) {
    return Utf16ToUtf8((Il2CppString*)GetRef(pi, g_off_partId));
}
static bool IsOrphan(void* pi) {
    if (!pi) return false;
    std::string id = PartId(pi);
    if (id.empty()) return false;                       // empty slot / no id
    return g_validIds.find(id) == g_validIds.end();
}

static void CollectFromArray(void* arr, std::set<void*>& orphans, std::set<void*>& seen) {
    uintptr_t n = ArrLen(arr);
    void** d = ArrData(arr);
    for (uintptr_t i = 0; i < n; i++) {
        void* pi = d[i];
        if (!pi || seen.count(pi)) continue;
        seen.insert(pi);
        if (IsOrphan(pi)) orphans.insert(pi);
        CollectFromArray(GetRef(pi, g_off_partFans), orphans, seen);   // nested case fans
    }
}
static void CollectScalar(void* cs, int off, std::set<void*>& orphans, std::set<void*>& seen) {
    void* pi = GetRef(cs, off);
    if (!pi || seen.count(pi)) return;
    seen.insert(pi);
    if (IsOrphan(pi)) orphans.insert(pi);
    CollectFromArray(GetRef(pi, g_off_partFans), orphans, seen);
}
static void NullOrphansInArray(void* arr, const std::set<void*>& orphans) {
    uintptr_t n = ArrLen(arr);
    void** d = ArrData(arr);
    for (uintptr_t i = 0; i < n; i++) {
        void* pi = d[i];
        if (!pi) continue;
        if (orphans.count(pi)) { d[i] = nullptr; continue; }
        NullOrphansInArray(GetRef(pi, g_off_partFans), orphans);
    }
}
static void* NewArrayFrom(Il2CppClass* elemClass, const std::vector<void*>& items) {
    void* na = il2cpp_array_new(elemClass, items.size());
    if (!na) return nullptr;
    void** d = ArrData(na);
    for (size_t i = 0; i < items.size(); i++) d[i] = items[i];
    return na;
}

// Drop orphaned components per loop; drop the loop if it empties. Surviving open
// loops are fine - the game tolerates them.
static void PruneLoops(void* cs, const std::set<void*>& orphans) {
    if (g_off_loopComps < 0) return;
    ManagedList loops = OpenList(GetRef(cs, g_cs.m_loops));
    if (!loops.valid()) return;

    std::vector<void*> keptLoops;
    for (int i = 0; i < loops.size; i++) {
        void* loop = loops.items[i];
        if (!loop) continue;
        void* comps = GetRef(loop, g_off_loopComps);
        uintptr_t cn = ArrLen(comps);
        void** cd = ArrData(comps);
        std::vector<void*> survivors;
        for (uintptr_t j = 0; j < cn; j++)
            if (cd[j] && !orphans.count(cd[j])) survivors.push_back(cd[j]);
        if (survivors.size() != cn) {
            void* nc = NewArrayFrom(g_partInstanceClass, survivors);
            if (nc) SetRef(loop, g_off_loopComps, nc);
        }
        if (!survivors.empty()) keptLoops.push_back(loop);
    }
    ShrinkList(loops, keptLoops);
}

// Drop any pipe whose from/to endpoint is an orphaned part (shrink array).
static void PrunePipes(void* cs, const std::set<void*>& orphans) {
    void* pipes = GetRef(cs, g_cs.pipesInstalled);
    uintptr_t pn = ArrLen(pipes);
    if (pn == 0) return;
    void** pd = ArrData(pipes);
    std::vector<void*> keep;
    for (uintptr_t i = 0; i < pn; i++) {
        void* pipe = pd[i];
        if (!pipe) continue;
        void* fc = GetRef(pipe, g_off_pipeFrom);
        void* tc = GetRef(pipe, g_off_pipeTo);
        if (orphans.count(fc) || orphans.count(tc)) continue;
        keep.push_back(pipe);
    }
    if (keep.size() != pn) {
        void* np = NewArrayFrom(g_waterPipeSaveClass, keep);
        if (np) SetRef(cs, g_cs.pipesInstalled, np);
    }
}

// Drop orphaned entries from a flat PartInstance[]. Used for the m_status arrays
// (preloadedWaterblocks, brokenParts), which the main slot/loop walk never
// reaches. List-style, not positional, so compact instead of nulling in place
// (a null hole could NRE when the game iterates them).
static int PrunePartArray(void* owner, int off) {
    if (!owner || off < 0) return 0;
    void* arr = GetRef(owner, off);
    uintptr_t n = ArrLen(arr);
    if (n == 0) return 0;
    void** d = ArrData(arr);
    std::vector<void*> keep;
    int removed = 0;
    for (uintptr_t i = 0; i < n; i++) {
        void* pi = d[i];
        if (!pi) continue;
        if (IsOrphan(pi)) { removed++; continue; }
        keep.push_back(pi);
    }
    if (removed) {
        void* na = NewArrayFrom(g_partInstanceClass, keep);
        if (na) SetRef(owner, off, na);
    }
    return removed;
}

// Per-source breakdown of a single SaveFix pass, so a future crash log shows
// which path removed parts instead of just a lump sum.
struct SaveFixStats {
    int pcs = 0;            // installed parts in storage/bench/carried/career PCs
    int inventory = 0;     // career inventory/peripheral/deliveries + carried package
    int jobs = 0;          // job-embedded PCs (m_startComputer/m_finishComputer)
    int persistentPCs = 0; // named/saved PCs dictionary
    int total() const { return pcs + inventory + jobs + persistentPCs; }
};

static int FixComputerSave(void* cs) {
    if (!cs || !g_fixReady) return 0;
    if (!SaveFixWhitelistOK()) return 0;                // never prune without a sane whitelist

    const int scalarOffs[] = { g_cs.caseID, g_cs.motherboardID, g_cs.cpuID,
        g_cs.cpuCoolerID, g_cs.psuID };
    const int arrOffs[] = { g_cs.psuSplitterIDs, g_cs.storageSlots, g_cs.caseFanSlots,
        g_cs.radiatorSlots, g_cs.reservoirSlots, g_cs.pciSlots, g_cs.ramSlots,
        g_cs.usbSlots, g_cs.m2Slots, g_cs.powerAdapterIDs };

    std::set<void*> orphans, seen;
    for (int off : scalarOffs) CollectScalar(cs, off, orphans, seen);
    for (int off : arrOffs) CollectFromArray(GetRef(cs, off), orphans, seen);
    if (g_off_loopComps >= 0) {                                  // parts that live only in a loop
        ManagedList loops = OpenList(GetRef(cs, g_cs.m_loops));
        for (int i = 0; i < loops.size; i++)
            CollectFromArray(GetRef(loops.items[i], g_off_loopComps), orphans, seen);
    }

    int removed = (int)orphans.size();

    if (!orphans.empty()) {
        for (int off : scalarOffs)
            if (orphans.count(GetRef(cs, off))) SetRef(cs, off, nullptr);
        for (int off : arrOffs) NullOrphansInArray(GetRef(cs, off), orphans);
        PruneLoops(cs, orphans);
        PrunePipes(cs, orphans);
    }

    // m_status arrays aren't reachable from the slot/loop walk above, so prune
    // them independently - a deleted waterblock or broken part may live ONLY here.
    void* status = GetRef(cs, g_cs_status);
    if (status) {
        removed += PrunePartArray(status, g_ccs_preloadedWb);
        removed += PrunePartArray(status, g_ccs_brokenParts);
    }

    return removed;
}

// SEH wrapper: a bad offset/AV leaves the save unchanged instead of crashing.
// Returns -1 on exception, else the orphan count.
static int SafeFixComputerSave(void* cs) {
    __try { return FixComputerSave(cs); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void Hook_LoadPC(void* self, void* save, void* header,
    void* compSave, void* slot, bool powered, void* mi) {
    GcThreadGuard guard;
    FlushDeserTally();
    int removed = SafeFixComputerSave(compSave);
    if (removed > 0)      Logger::Log("[=] SaveFix: removed " + std::to_string(removed) + " orphaned part(s)");
    else if (removed < 0) Logger::Log("[-] SaveFix: exception; save left unchanged");
    g_loadPC_orig(self, save, header, compSave, slot, powered, mi);
}

// Fix every ComputerSave in a List<ComputerSave>.
static int FixComputerList(void* list) {
    ManagedList v = OpenList(list);
    int total = 0;
    for (int i = 0; i < v.size; i++)
        if (v.items[i]) total += FixComputerSave(v.items[i]);
    return total;
}

// Remove orphaned PartInstances from a List<PartInstance> (e.g. carried package).
static int PrunePartList(void* list) {
    ManagedList v = OpenList(list);
    if (!v.valid()) return 0;
    std::vector<void*> keep;
    int removed = 0;
    for (int i = 0; i < v.size; i++) {
        void* pi = v.items[i];
        if (!pi) continue;
        if (IsOrphan(pi)) { removed++; continue; }
        keep.push_back(pi);
    }
    if (removed) ShrinkList(v, keep);
    return removed;
}

// Fix the ComputerSave embedded in each Job of a List<Job>. Jobs hold a
// serialized m_startComputer that never goes through LoadPC/OnAfterDeserialize
// (BinaryFormatter load; OnAfterDeserialize only fires for slot-loaded PCs), so
// an orphan in a job PC survives until the calendar rebuild
// (CalendarDay.UpdateEvents) NREs on it during CareerStatus.SetState.
static int FixJobList(void* list) {
    if (g_job_startComputer < 0) return 0;
    ManagedList v = OpenList(list);
    int total = 0;
    for (int i = 0; i < v.size; i++) {
        void* job = v.items[i];
        if (!job) continue;
        total += FixComputerSave(GetRef(job, g_job_startComputer));
        if (g_job_finishComputer >= 0)
            total += FixComputerSave(GetRef(job, g_job_finishComputer));
    }
    return total;
}

// Fix every ComputerSave value in a Dictionary<string, ComputerSave> (e.g.
// State.m_persistentPCs). Entries live inline in an Entry[] ("_entries" on .NET
// Core BCL, "entries" on .NET Framework); Entry = { hashCode:4, next:4, key:ptr,
// value:ptr }, so for ref/ref maps on x64 value sits at 0x10 with a 0x18 stride.
// Removed/empty entries have a null value, so a flat walk over the first _count
// entries is null-safe without consulting the free list. Runs under
// SafeFixSaveGame's SEH, so a layout mismatch faults harmlessly.
static int FixComputerDict(void* dict) {
    if (!dict) return 0;
    Il2CppClass* dk = *(Il2CppClass**)dict;
    int entriesOff = FieldOff(dk, "_entries");
    if (entriesOff < 0) entriesOff = FieldOff(dk, "entries");
    int countOff = FieldOff(dk, "_count");
    if (countOff < 0) countOff = FieldOff(dk, "count");
    if (entriesOff < 0 || countOff < 0) return 0;
    void* entries = GetRef(dict, entriesOff);
    if (!entries) return 0;
    int count = *(int*)((uint8_t*)dict + countOff);
    if (count <= 0) return 0;
    uintptr_t cap = ArrLen(entries);
    if ((uintptr_t)count > cap) count = (int)cap;       // clamp to array bounds
    uint8_t* data = (uint8_t*)ArrData(entries);
    const size_t kEntryStride = 0x18;                   // ref key + ref value on x64
    const size_t kValueOff = 0x10;
    int total = 0;
    for (int i = 0; i < count; i++) {
        void* value = *(void**)(data + (size_t)i * kEntryStride + kValueOff);
        total += FixComputerSave(value);                // null-safe
    }
    return total;
}

// Prune orphaned parts from the career State. These hang off SaveGame.careerState,
// not inside any ComputerSave, so the container prune never reaches them.
// State.FixForVersion (1-arg, NOT the hooked PartInstance.FixForVersion) and
// Inventory.GetEligibleParts both walk these and NRE on an orphan's null
// PartDesc. Runs at LoadGame entry, before SetState.
static void PruneCareerState(void* sg, SaveFixStats& st) {
    if (g_sg_careerState < 0) return;
    void* state = GetRef(sg, g_sg_careerState);
    if (!state) return;                            // freebuild / no career state
    Il2CppClass* sk = *(Il2CppClass**)state;       // actual CareerStatus.State class

    static const char* const kPartLists[] = { "m_inventory", "m_peripheralInventory", "m_deliveries" };
    for (const char* name : kPartLists) {
        int off = FieldOff(sk, name);
        if (off >= 0) st.inventory += PrunePartList(GetRef(state, off));
    }
    static const char* const kComputerSaves[] = { "m_FirstPCBayComputerSave", "m_tabletComputerSave" };
    for (const char* name : kComputerSaves) {
        int off = FieldOff(sk, name);
        if (off >= 0) st.pcs += FixComputerSave(GetRef(state, off));
    }

    // Named/saved PCs not currently on a bench (Dictionary<string, ComputerSave>).
    // Not slot-loaded on every session, so OnAfterDeserialize may never reach them.
    int ppcOff = FieldOff(sk, "m_persistentPCs");
    if (ppcOff >= 0) st.persistentPCs += FixComputerDict(GetRef(state, ppcOff));

    // Job-embedded PCs (m_startComputer) - the calendar rebuild reads these.
    static const char* const kJobLists[] = { "m_jobs", "m_todaysJobs", "m_doneJobs",
        "m_acceptedJobs", "m_deletedJobs", "m_rejectedJobs", "m_flaggedJobs" };
    for (const char* jl : kJobLists) {
        int off = FieldOff(sk, jl);
        if (off >= 0) st.jobs += FixJobList(GetRef(state, off));
    }
}

// Prune the whole SaveGame (storage/bench/carried PCs + carried parts) at
// LoadGame entry, before FixForVersion/GetPart NREs on an orphaned id.
static int FixSaveGame(void* sg, SaveFixStats& st) {
    if (!sg || !g_fixReady) return 0;
    if (!SaveFixWhitelistOK()) return 0;
    st.pcs += FixComputerList(GetRef(sg, g_sg_inStorage));
    st.pcs += FixComputerList(GetRef(sg, g_sg_onBenches));
    st.pcs += FixComputerSave(GetRef(sg, g_sg_carrying));
    st.inventory += PrunePartList(GetRef(sg, g_sg_pkg));
    PruneCareerState(sg, st);
    return st.total();
}
static int SafeFixSaveGame(void* sg, SaveFixStats& st) {
    __try { return FixSaveGame(sg, st); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void Hook_LoadGame(void* self, void* name, void* header, void* saveGame, void* mi) {
    GcThreadGuard guard;
    FlushDeserTally();   // emit the deserialize-phase summary before the save summary
    SaveFixStats st;
    int removed = SafeFixSaveGame(saveGame, st);
    if (removed > 0)
        Logger::Log("[=] SaveFix: removed " + std::to_string(removed) +
            " orphaned part(s) from save (PCs=" + std::to_string(st.pcs) +
            " inventory=" + std::to_string(st.inventory) +
            " jobs=" + std::to_string(st.jobs) +
            " persistentPCs=" + std::to_string(st.persistentPCs) + ")");
    else if (removed < 0) Logger::Log("[-] SaveFix: exception in LoadGame; save left unchanged");
    g_loadGame_orig(self, name, header, saveGame, mi);
}

// Safety net: FixForVersion -> GetPart NREs on an orphan. Return false
// ("not fixed") to skip the game's fix-up; the LoadGame prune already removed
// these from the build, so they won't be installed.
static bool IsOrphanGuarded(void* self) {
    if (!self || !g_fixReady || !SaveFixWhitelistOK()) return false;
    return IsOrphan(self);
}
static bool Hook_FixForVersion(void* self, int version, bool csOnly, void* mi) {
    if (IsOrphanGuarded(self)) return false;
    return g_fixForVersion_orig(self, version, csOnly, mi);
}

// Net: CheckBiosError NREs after SaveFix nulled an installed part (empty slot,
// null m_expectedBios), which aborts the load coroutine and the bench boot. On
// fault report "no bios error" and let boot continue on the now-incomplete PC.
static bool Hook_CheckBiosError(void* self, void* mi) {
    __try { return g_checkBiosError_orig(self, mi); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Systemic net: GetBaseRAMInstance is the deepest common frame of the
// orphan-deref NREs, reached via BiosConfig.Update from many callers. Rather
// than hunting each caller, guard where the NRE originates: on fault return
// null, which BiosConfig.Update already treats as "no base RAM" (a valid state).
typedef void* (*CS_GetBaseRAMInstance_t)(void* self, void* mi);
static CS_GetBaseRAMInstance_t g_getBaseRAM_orig = nullptr;
static void* Hook_GetBaseRAMInstance(void* self, void* mi) {
    __try { return g_getBaseRAM_orig(self, mi); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// Net (instantiation): HandleRadiator NREs when a valid radiator's water loop
// lost a neighbour to pruning and connectPipe can't link it. void return, so
// swallowing skips that pipe hookup and the rest of the PC finishes loading.
// Picked over InstantiatePreloadedPart, whose null return could re-NRE upstream.
typedef void (*Slot_HandleRadiator_t)(void* self, void* comp, void* radiator,
    bool isPlaceholder, bool connectPipe, void* mi);
static Slot_HandleRadiator_t g_handleRadiator_orig = nullptr;
static void Hook_HandleRadiator(void* self, void* comp, void* radiator,
    bool isPlaceholder, bool connectPipe, void* mi) {
    __try { g_handleRadiator_orig(self, comp, radiator, isPlaceholder, connectPipe, mi); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Net (power-on): UpdateTabletApps runs every frame while a PC is on and NREs
// if its tablet/app list references a removed-mod part. Swallow: the apps just
// don't refresh, the PC still powers on.
typedef void (*VC_UpdateTabletApps_t)(void* self, void* mi);
static VC_UpdateTabletApps_t g_updateTabletApps_orig = nullptr;
static void Hook_UpdateTabletApps(void* self, void* mi) {
    __try { g_updateTabletApps_orig(self, mi); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Net (load path): OS.OnStartup boots the player PC inside the LoadGame
// coroutine; a PC with no usable GPU NREs there and aborts the whole load
// ("savegame error"). Swallow: the OS just doesn't boot, load completes.
typedef void (*OS_OnStartup_t)(void* self, void* computer, bool skipAchievements, void* mi);
static OS_OnStartup_t g_osOnStartup_orig = nullptr;
static void Hook_OSOnStartup(void* self, void* computer, bool skipAchievements, void* mi) {
    __try { g_osOnStartup_orig(self, computer, skipAchievements, mi); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Net (runtime): SetSelected -> BenchSlot.GetInteractions NREs every frame the
// player aims at a bench holding a broken PC. Guard the void caller (a null
// from GetInteractions would re-NRE): the selection just skips that frame.
typedef void (*WS_SetSelected_t)(void* self, void* selected, void* mi);
static WS_SetSelected_t g_setSelected_orig = nullptr;
static void Hook_SetSelected(void* self, void* selected, void* mi) {
    __try { g_setSelected_orig(self, selected, mi); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Net: CalendarDay.UpdateEvents NREs if a job/delivery event still references a
// removed-mod part by string (pruning the job PCs doesn't cover that). Guard the
// rebuild so one bad day can't abort the career load; a skipped day just renders
// without its event icons.
typedef void (*CalendarDay_UpdateEvents_t)(void* self, void* cal, int day, void* mi);
static CalendarDay_UpdateEvents_t g_updateEvents_orig = nullptr;
static void Hook_UpdateEvents(void* self, void* cal, int day, void* mi) {
    __try { g_updateEvents_orig(self, cal, day, mi); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// OnAfterDeserialize runs on EVERY ComputerSave, on EVERY load path, before any
// bios/boot logic - including the main-menu showcase PC via FreebuildConfig ->
// LoadComputerToSlot -> InitComputerLoading, which LoadGame/LoadPC never reach.
// Pruning here, before the original runs, is the universal fix: BiosConfig is
// never built from a deleted part. Covers menu, freebuild, career and
// PCFileSharing imports at once.
static void Hook_OnAfterDeserialize(void* self, void* mi) {
    GcThreadGuard guard;
    int removed = SafeFixComputerSave(self);
    if (removed > 0) { g_deserParts += removed; ++g_deserPCs; }
    g_onAfterDeser_orig(self, mi);
}

// Job.OnDeserialization is invoked by the BinaryFormatter itself, BEFORE
// LoadGame runs, and evaluates the job's objectives against the job PC - which
// NREs on a removed-mod part. FixSaveGame/PruneCareerState come too late, so
// prune the job PC here. SEH around the original catches objectives that
// reference an id by string, so one bad job can't kill the load.
typedef void (*Job_OnDeserialization_t)(void* self, void* sender, void* mi);
static Job_OnDeserialization_t g_jobOnDeser_orig = nullptr;
// SEH must live in its own object-free function (C2712).
static void SafeCallJobOnDeser(void* self, void* sender, void* mi) {
    __try { g_jobOnDeser_orig(self, sender, mi); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void Hook_JobOnDeserialization(void* self, void* sender, void* mi) {
    GcThreadGuard guard;
    if (self && g_job_startComputer >= 0) {
        int removed = SafeFixComputerSave(GetRef(self, g_job_startComputer));
        if (g_job_finishComputer >= 0)
            removed += SafeFixComputerSave(GetRef(self, g_job_finishComputer));
        if (removed > 0) { g_deserParts += removed; ++g_deserJobs; }
    }
    SafeCallJobOnDeser(self, sender, mi);
}

static void SaveFix_Init() {
    Il2CppClass* cs = IL2CPP_FindClass("", "ComputerSave");
    g_partInstanceClass = IL2CPP_FindClass("", "PartInstance");
    g_waterPipeSaveClass = IL2CPP_FindClass("", "WaterPipeSave");
    Il2CppClass* wl = IL2CPP_FindClass("", "WaterLoopSave");
    if (!cs || !g_partInstanceClass || !g_waterPipeSaveClass || !wl) {
        Logger::Log("[!] SaveFix disabled: a save class was not found");
        return;
    }

    static const struct { const char* name; int CSOff::* off; } kCsFields[] = {
        { "caseID", &CSOff::caseID }, { "motherboardID", &CSOff::motherboardID },
        { "cpuID", &CSOff::cpuID }, { "cpuCoolerID", &CSOff::cpuCoolerID },
        { "psuID", &CSOff::psuID }, { "psuSplitterIDs", &CSOff::psuSplitterIDs },
        { "storageSlots", &CSOff::storageSlots }, { "caseFanSlots", &CSOff::caseFanSlots },
        { "radiatorSlots", &CSOff::radiatorSlots }, { "reservoirSlots", &CSOff::reservoirSlots },
        { "pciSlots", &CSOff::pciSlots }, { "ramSlots", &CSOff::ramSlots },
        { "usbSlots", &CSOff::usbSlots }, { "m2Slots", &CSOff::m2Slots },
        { "powerAdapterIDs", &CSOff::powerAdapterIDs }, { "m_loops", &CSOff::m_loops },
        { "pipesInstalled", &CSOff::pipesInstalled },
    };
    for (const auto& f : kCsFields) g_cs.*f.off = FieldOff(cs, f.name);

    g_off_partId = FieldOff(g_partInstanceClass, "m_partId");
    g_off_partFans = FieldOff(g_partInstanceClass, "m_caseFanSlots");
    g_off_loopComps = FieldOff(wl, "m_components");
    g_off_pipeFrom = FieldOff(g_waterPipeSaveClass, "m_from");   // ConnectorId.m_component is its first field
    g_off_pipeTo = FieldOff(g_waterPipeSaveClass, "m_to");

    Il2CppClass* sg = IL2CPP_FindClass("", "SaveGame");
    g_sg_inStorage = FieldOff(sg, "computersInStorage");
    g_sg_onBenches = FieldOff(sg, "computersOnBenches");
    g_sg_carrying = FieldOff(sg, "carryingComputer");
    g_sg_pkg = FieldOff(sg, "carryingPackageContents");
    g_sg_careerState = FieldOff(sg, "careerState");

    g_cs_status = FieldOff(cs, "m_status");
    Il2CppClass* ccs = IL2CPP_FindClass("", "ComputerCareerStatus");
    g_ccs_preloadedWb = FieldOff(ccs, "preloadedWaterblocks");
    g_ccs_brokenParts = FieldOff(ccs, "brokenParts");

    Il2CppClass* jobCls = IL2CPP_FindClass("", "Job");
    g_job_startComputer = FieldOff(jobCls, "m_startComputer");
    g_job_finishComputer = FieldOff(jobCls, "m_finishComputer");

    g_fixReady = (g_off_partId >= 0 && g_cs.pciSlots >= 0 && g_cs.cpuID >= 0);
    Logger::Log(g_fixReady ? "[+] SaveFix ready"
        : "[!] SaveFix disabled: required offsets missing");
}

// --- install -----------------------------------------------------------------
static void* ResolveMethod(Il2CppClass* klass, const char* method, int args) {
    const Il2CppMethodInfo* m = klass
        ? il2cpp_class_get_method_from_name(klass, method, args) : nullptr;
    return m ? IL2CPP_GetMethodPointer(m) : nullptr;
}

// Resolves a method the loader can't run without; logs and returns null on failure.
// failName/ptrName preserve the historical log wording per call site.
static void* ResolveRequired(Il2CppClass* klass, const char* method, int args,
    const char* failName, const char* ptrName) {
    const Il2CppMethodInfo* m = il2cpp_class_get_method_from_name(klass, method, args);
    if (!m) {
        Logger::Log(std::string("[-] ") + failName + " not found");
        return nullptr;
    }
    void* p = IL2CPP_GetMethodPointer(m);
    if (!p) Logger::Log(std::string("[-] ") + ptrName + " has no method pointer");
    return p;
}

// Optional hook: log success/failure, never abort installation.
static bool InstallHook(void* target, void* detour, void** original, const char* name) {
    if (MH_CreateHook(target, (LPVOID)detour, (LPVOID*)original) == MH_OK &&
        MH_EnableHook(target) == MH_OK) {
        Logger::Log(std::string("[+] Hooked ") + name);
        return true;
    }
    Logger::Log(std::string("[-] Hook on ") + name + " failed");
    return false;
}

// Mandatory hook: detailed failure logs, caller aborts on false.
// failName/okName preserve the historical log wording per call site.
static bool InstallRequiredHook(void* target, void* detour, void** original,
    const char* failName, const char* okName) {
    MH_STATUS s = MH_CreateHook(target, (LPVOID)detour, (LPVOID*)original);
    if (s != MH_OK) {
        Logger::Log(std::string("[-] CreateHook on ") + failName +
            " failed (MH_STATUS=" + std::to_string(s) + ")");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Logger::Log(std::string("[-] EnableHook on ") + failName + " failed");
        return false;
    }
    Logger::Log(std::string("[+] Hooked ") + okName);
    return true;
}

void Hooks_SetPendingMods(std::vector<ModFile>* mods) {
    g_pendingMods = mods;
}

void Hooks_SetSaveFixEnabled(bool enabled) {
    g_saveFixEnabled = enabled;
}

bool Hooks_Install() {
    Il2CppClass* dbClass = IL2CPP_FindClass("", "PartsDatabase");
    if (!dbClass) {
        Logger::Log("[-] PartsDatabase class not found");
        return false;
    }

    void* loadPtr = ResolveRequired(dbClass, "Load", 0,
        "PartsDatabase.Load", "PartsDatabase.Load");
    if (!loadPtr) return false;

    void* importPtr = ResolveRequired(dbClass, "ImportFromHTML", 1,
        "PartsDatabase.ImportFromHTML", "ImportFromHTML");
    if (!importPtr) return false;

    g_textAssetClass = IL2CPP_FindClass("UnityEngine", "TextAsset");
    if (!g_textAssetClass) {
        Logger::Log("[-] UnityEngine.TextAsset class not found");
        return false;
    }

    g_textAssetCtor = (TextAsset_Ctor_t)ResolveRequired(g_textAssetClass, ".ctor", 2,
        "TextAsset .ctor(CreateOptions, string)", "TextAsset .ctor");
    if (!g_textAssetCtor) return false;
    Logger::Log("[+] TextAsset .ctor(CreateOptions, string) resolved");

    // get_text is used to read stock part HTML for ID collection. Non-fatal.
    g_textAssetGetText = (TextAsset_GetText_t)ResolveMethod(g_textAssetClass, "get_text", 0);
    if (!g_textAssetGetText)
        Logger::Log("[!] TextAsset.get_text not found; stock part IDs won't be collected");

    if (!InstallRequiredHook(loadPtr, (void*)Hook_PDB_Load, (void**)&g_originalLoad,
        "PartsDatabase.Load", "PartsDatabase.Load"))
        return false;

    // g_importFromHTML becomes the trampoline used by both the collect hook
    // and our injection.
    if (!InstallRequiredHook(importPtr, (void*)Hook_ImportFromHTML, (void**)&g_importFromHTML,
        "ImportFromHTML", "PartsDatabase.ImportFromHTML"))
        return false;

    // Part injection + ID collection are now live. SaveFix is optional.
    if (!g_saveFixEnabled) {
        Logger::Log("[!] SaveFix disabled via config");
        return true;
    }

    SaveFix_Init();

    // Prune orphaned parts at PC load.
    Il2CppClass* slsClass = IL2CPP_FindClass("", "SaveLoadSystem");
    void* loadPcPtr = ResolveMethod(slsClass, "LoadPC", 5);
    if (g_fixReady && loadPcPtr)
        InstallHook(loadPcPtr, (void*)Hook_LoadPC, (void**)&g_loadPC_orig,
            "SaveLoadSystem.LoadPC");
    else
        Logger::Log("[!] SaveFix not hooked (offsets missing or LoadPC not found)");

    // Save-wide prune: LoadGame runs before LoadPC, so prune the whole SaveGame
    // at its entry.
    void* loadGamePtr = ResolveMethod(slsClass, "LoadGame", 3);
    if (g_fixReady && loadGamePtr && (g_sg_inStorage >= 0 || g_sg_onBenches >= 0))
        InstallHook(loadGamePtr, (void*)Hook_LoadGame, (void**)&g_loadGame_orig,
            "SaveLoadSystem.LoadGame");
    else
        Logger::Log("[!] LoadGame not hooked (SaveGame offsets missing or method not found)");

    // Safety net for orphans the prune might not reach: skip FixForVersion.
    void* ffvPtr = ResolveMethod(g_partInstanceClass, "FixForVersion", 2);
    if (g_fixReady && ffvPtr)
        InstallHook(ffvPtr, (void*)Hook_FixForVersion, (void**)&g_fixForVersion_orig,
            "PartInstance.FixForVersion");
    else
        Logger::Log("[!] FixForVersion not hooked");

    Il2CppClass* csClass = IL2CPP_FindClass("", "ComputerSave");
    Il2CppClass* vcClass = IL2CPP_FindClass("", "VirtualComputer");

    if (void* p = ResolveMethod(vcClass, "CheckBiosError", 0))
        InstallHook(p, (void*)Hook_CheckBiosError, (void**)&g_checkBiosError_orig,
            "VirtualComputer.CheckBiosError");
    else
        Logger::Log("[!] VirtualComputer.CheckBiosError not found");

    if (void* p = ResolveMethod(vcClass, "UpdateTabletApps", 0))
        InstallHook(p, (void*)Hook_UpdateTabletApps, (void**)&g_updateTabletApps_orig,
            "VirtualComputer.UpdateTabletApps");
    else
        Logger::Log("[!] VirtualComputer.UpdateTabletApps not found");

    Il2CppClass* osClass = IL2CPP_FindClass("", "OS");
    if (void* p = ResolveMethod(osClass, "OnStartup", 2))
        InstallHook(p, (void*)Hook_OSOnStartup, (void**)&g_osOnStartup_orig,
            "OS.OnStartup");
    else
        Logger::Log("[!] OS.OnStartup not found");

    Il2CppClass* wsClass = IL2CPP_FindClass("", "WalkingState");
    if (void* p = ResolveMethod(wsClass, "SetSelected", 1))
        InstallHook(p, (void*)Hook_SetSelected, (void**)&g_setSelected_orig,
            "WalkingState.SetSelected");
    else
        Logger::Log("[!] WalkingState.SetSelected not found");

    if (void* p = ResolveMethod(csClass, "GetBaseRAMInstance", 0))
        InstallHook(p, (void*)Hook_GetBaseRAMInstance, (void**)&g_getBaseRAM_orig,
            "ComputerSave.GetBaseRAMInstance");
    else
        Logger::Log("[!] ComputerSave.GetBaseRAMInstance not found");

    Il2CppClass* slotClass = IL2CPP_FindClass("", "Slot");
    if (void* p = ResolveMethod(slotClass, "HandleRadiator", 4))
        InstallHook(p, (void*)Hook_HandleRadiator, (void**)&g_handleRadiator_orig,
            "Slot.HandleRadiator");
    else
        Logger::Log("[!] Slot.HandleRadiator not found");

    // Universal SaveFix: prune every ComputerSave right after deserialization,
    // before GetBiosConfig/BiosConfig.Update build it (covers the main-menu
    // showcase PC, which LoadGame/LoadPC never reach).
    void* oadPtr = ResolveMethod(csClass, "OnAfterDeserialize", 0);
    if (g_fixReady && oadPtr)
        InstallHook(oadPtr, (void*)Hook_OnAfterDeserialize, (void**)&g_onAfterDeser_orig,
            "ComputerSave.OnAfterDeserialize");
    else
        Logger::Log("[!] OnAfterDeserialize not hooked (offsets missing or method not found)");

    Il2CppClass* calDayClass = IL2CPP_FindClass("", "CalendarDay");
    if (void* p = ResolveMethod(calDayClass, "UpdateEvents", 2))
        InstallHook(p, (void*)Hook_UpdateEvents, (void**)&g_updateEvents_orig,
            "CalendarDay.UpdateEvents");
    else
        Logger::Log("[!] CalendarDay.UpdateEvents not found");

    // Prune job PCs during BinaryFormatter deserialization itself (before LoadGame).
    Il2CppClass* jobClass = IL2CPP_FindClass("", "Job");
    void* jodPtr = ResolveMethod(jobClass, "OnDeserialization", 1);
    if (g_fixReady && jodPtr)
        InstallHook(jodPtr, (void*)Hook_JobOnDeserialization, (void**)&g_jobOnDeser_orig,
            "Job.OnDeserialization");
    else
        Logger::Log("[!] Job.OnDeserialization not hooked (offsets missing or method not found)");

    return true;
}