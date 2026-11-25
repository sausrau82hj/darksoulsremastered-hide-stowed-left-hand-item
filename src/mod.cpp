// Hide the off-hand item's model while the right-hand weapon is two-handed.
// Dark Souls: Remastered 1.03.1; the offsets below fit that build only.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <vector>
#pragma comment(lib, "winmm.lib")

static uintptr_t g_base;
static HANDLE    g_proc;
static volatile long g_running;

static const uintptr_t OFF_WORLD_CHR_MAN      = 0x1c77e50;
static const uintptr_t OFF_PLAYERINS          = 0x68;
static const uintptr_t OFF_PLAYERINS_MODEL    = 0x60;
static const uintptr_t OFF_PLAYERINS_PGD      = 0x578;
static const uintptr_t OFF_PLAYERINS_MODELRES = 0x848;
static const uintptr_t OFF_PGD_EQUIPGD        = 0x280;
static const uintptr_t OFF_EQUIPGD_CHRASM     = 0x80;
static const uintptr_t OFF_CHRASM_STYLE       = 0x08;
static const uintptr_t OFF_CHRASM_LIDX        = 0x0c;
static const uintptr_t OFF_CHRASM_RIDX        = 0x10;
static const uintptr_t OFF_CHRASM_ITEMS       = 0x24;

static const uintptr_t MODELRES_ELEMS      = 0x30;
static const uintptr_t MODELRES_ELEM_SIZE  = 0x50;
static const uint32_t  MODELRES_ELEM_COUNT = 14;
static const uintptr_t MODELRES_CACHE_FLAG = 0x2c;   

static const uint32_t STYLE_1H       = 1;   
static const uint32_t STYLE_2H_RIGHT = 3;   

static const uint32_t WPARAM_ROWSTRIDE = 12;
static const uint32_t WPARAM_MODELOFF  = 0xB8;

static bool SR(uintptr_t a, void* b, size_t n)
{
    if (!a || a < 0x10000) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(g_proc, (LPCVOID)a, b, n, &got) && got == n;
}
static uint64_t Rq(uintptr_t a)   { uint64_t v = 0; SR(a, &v, 8); return v; }
static uint32_t Rd32(uintptr_t a) { uint32_t v = 0; SR(a, &v, 4); return v; }
static uint16_t Rd16(uintptr_t a) { uint16_t v = 0; SR(a, &v, 2); return v; }
static uint8_t  Rd8 (uintptr_t a) { uint8_t  v = 0; SR(a, &v, 1); return v; }

static bool Wr(uintptr_t a, const void* src, size_t n)
{
    if (!a) return false;
    DWORD old = 0;
    if (!VirtualProtect((void*)a, n, PAGE_READWRITE, &old)) return false;
    SIZE_T wrote = 0;
    BOOL ok = WriteProcessMemory(g_proc, (LPVOID)a, src, n, &wrote);
    DWORD tmp = 0;
    VirtualProtect((void*)a, n, old, &tmp);
    return ok && wrote == n;
}
static bool Wr32(uintptr_t a, uint32_t v) { return Wr(a, &v, 4); }
static bool Wr16(uintptr_t a, uint16_t v) { return Wr(a, &v, 2); }
static bool Wr8 (uintptr_t a, uint8_t  v) { return Wr(a, &v, 1); }

static bool Mapped(uintptr_t p, size_t n)
{
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static DWORD Now() { DWORD t = GetTickCount(); return t ? t : 1; }

static uintptr_t g_wparamBase  = 0;
static uint32_t  g_wparamCount = 0;

static bool RecordOk(uintptr_t p, uint32_t* idOut, uint32_t* offOut)
{
    uint32_t id = Rd32(p), off = Rd32(p + 4);
    if (id < 100000 || id > 20000000) return false;
    if (off < 0x100 || off > 0x800000) return false;
    if (idOut)  *idOut  = id;
    if (offOut) *offOut = off;
    return true;
}

static uintptr_t FindWeaponParam()
{
    static const char* needle = "EQUIP_PARAM_WEAPON_ST";
    const size_t nlen = strlen(needle);
    uintptr_t addr = 0x10000;
    MEMORY_BASIC_INFORMATION mbi;
    std::vector<unsigned char> buf;
    while (VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
        uintptr_t start = (uintptr_t)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        uintptr_t next = start + size;
        bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                  (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                  PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
        if (ok && size >= 0x40) {
            size_t chunk = 1u << 20;
            for (size_t off = 0; off < size; off += chunk) {
                size_t len = (size - off < chunk) ? (size - off) : chunk;
                if (buf.size() < len) buf.resize(len);
                if (!SR(start + off, buf.data(), len)) continue;
                for (size_t i = 0; i + nlen < len; i++) {
                    if (buf[i] != 'E' || memcmp(buf.data() + i, needle, nlen) != 0) continue;
                    uintptr_t strAddr = start + off + i;
                    for (uintptr_t probe = strAddr + 0x18; probe <= strAddr + 0x40; probe += 2) {
                        uint32_t id0, off0, id1, off1;
                        if (!RecordOk(probe, &id0, &off0)) continue;
                        if (!RecordOk(probe + 12, &id1, &off1)) continue;
                        if (id1 <= id0) continue;
                        uint32_t stride = off1 - off0;
                        if (stride < 0x40 || stride > 0x400) continue;
                        uintptr_t base = probe - 0x30;
                        uint32_t count = Rd32(base + 0x0A) & 0xFFFF;
                        if (count < 100 || count > 20000) {
                            count = 0;
                            while (count < 20000) {
                                uint32_t a, b;
                                if (!RecordOk(probe + (uintptr_t)count * 12, &a, &b)) break;
                                count++;
                            }
                        }

                        g_wparamCount = count;
                        return base;
                    }
                }
            }
        }
        if (next <= addr) break;
        addr = next;
    }
    return 0;
}

static uintptr_t FindRow(uint32_t slotValue, uint32_t* rowIdOut)
{
    if (!g_wparamBase || slotValue == 0 || slotValue == 0xFFFFFFFF) return 0;
    uint32_t want = (slotValue / 100) * 100;
    uint32_t best = 0, bestOff = 0;
    if (g_wparamCount) {
        uint32_t lo = 0, hi = g_wparamCount - 1;
        while (lo <= hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            uintptr_t inf = g_wparamBase + 0x30 + (uintptr_t)mid * WPARAM_ROWSTRIDE;
            uint32_t id = Rd32(inf), off = Rd32(inf + 4);
            if (!id) break;
            if (id == want) { best = id; bestOff = off; break; }
            if (id < want) { if (mid == 0xFFFFFFFFu) break; lo = mid + 1; }
            else           { if (mid == 0) break; hi = mid - 1; }
        }
    }
    if (!best) {
        for (uint32_t i = 0; i < g_wparamCount; i++) {
            uintptr_t inf = g_wparamBase + 0x30 + (uintptr_t)i * WPARAM_ROWSTRIDE;
            uint32_t id = Rd32(inf), off = Rd32(inf + 4);
            if (!id || !off) continue;
            if (id <= slotValue && slotValue - id <= 15 && id > best) { best = id; bestOff = off; }
        }
    }
    if (!best) return 0;
    if (rowIdOut) *rowIdOut = best;
    return g_wparamBase + bestOff;
}

static uintptr_t FindRowById(uint32_t rowId)
{
    if (!g_wparamBase || !rowId) return 0;
    if (g_wparamCount) {
        uint32_t lo = 0, hi = g_wparamCount - 1;
        while (lo <= hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            uintptr_t inf = g_wparamBase + 0x30 + (uintptr_t)mid * WPARAM_ROWSTRIDE;
            uint32_t id = Rd32(inf), off = Rd32(inf + 4);
            if (!id) break;
            if (id == rowId) return off ? g_wparamBase + off : 0;
            if (id < rowId) lo = mid + 1;
            else { if (mid == 0) break; hi = mid - 1; }
        }
    }
    for (uint32_t i = 0; i < g_wparamCount; i++) {
        uintptr_t inf = g_wparamBase + 0x30 + (uintptr_t)i * WPARAM_ROWSTRIDE;
        uint32_t id = Rd32(inf), off = Rd32(inf + 4);
        if (id == rowId && off) return g_wparamBase + off;
    }
    return 0;
}

struct RowSave { uint32_t rowId; uint16_t model; };
static std::vector<RowSave> g_rowSaves;

static uint16_t OriginalModel(uint32_t rowId, uintptr_t row)
{
    for (size_t i = 0; i < g_rowSaves.size(); i++)
        if (g_rowSaves[i].rowId == rowId) return g_rowSaves[i].model;
    RowSave s; s.rowId = rowId; s.model = Rd16(row + WPARAM_MODELOFF);
    g_rowSaves.push_back(s);
    return s.model;
}

static uintptr_t g_equipGD = 0;

static bool      s_hidden    = false;
static uint32_t  s_slot      = 0;
static uint32_t  s_itemValue = 0;
static uint32_t  s_rowId     = 0;
static uintptr_t s_modelAddr = 0;
static uint16_t  s_savedModel= 0;

static bool      s_pendActive = false;
static DWORD     s_pendAt     = 0;   
static DWORD     s_pendStart  = 0;   
static int       s_pendTries  = 0;
static bool      s_pendShortened = false;
static uint64_t  s_pendCaps[MODELRES_ELEM_COUNT];
static const char* s_pendWhy = "";

static uintptr_t ItemAddr(uintptr_t equipGD, uint32_t slot)
{
    return equipGD + OFF_EQUIPGD_CHRASM + OFF_CHRASM_ITEMS + 4 * (uintptr_t)slot;
}

static bool ModelResOk(uintptr_t mr)
{
    if (!Mapped(mr, MODELRES_ELEMS + MODELRES_ELEM_COUNT * MODELRES_ELEM_SIZE)) return false;
    int good = 0;
    for (uint32_t i = 0; i < MODELRES_ELEM_COUNT; i++) {
        uint32_t id = Rd32(mr + MODELRES_ELEMS + (uintptr_t)i * MODELRES_ELEM_SIZE);
        if (id == 0 || id == 0xFFFFFFFF || (id > 100 && id < 10000000)) good++;
    }
    return good == (int)MODELRES_ELEM_COUNT;
}

static void CapsNow(uintptr_t modelRes, uint64_t out[MODELRES_ELEM_COUNT])
{
    for (uint32_t i = 0; i < MODELRES_ELEM_COUNT; i++) {
        out[i] = modelRes ? Rq(modelRes + MODELRES_ELEMS + (uintptr_t)i * MODELRES_ELEM_SIZE + 8) : 0;
    }
}

static bool FlipVariantCache(uintptr_t modelRes)
{
    if (!ModelResOk(modelRes)) return false;
    uintptr_t a = modelRes + MODELRES_CACHE_FLAG;
    uint8_t v = Rd8(a);
    return Wr8(a, (uint8_t)(v ^ 0x01));
}

static void ArmRebuild(uintptr_t modelRes, const char* why)
{
    s_pendActive    = true;
    s_pendStart     = Now();
    s_pendAt        = s_pendStart;
    s_pendTries     = 1;
    s_pendShortened = false;
    s_pendWhy       = why;
    CapsNow(modelRes, s_pendCaps);
    FlipVariantCache(modelRes);

}

static void TickRebuild(uintptr_t modelRes)
{
    if (!s_pendActive) return;
    DWORD now = Now();
    uint64_t caps[MODELRES_ELEM_COUNT];
    CapsNow(modelRes, caps);
    int changed = 0, first = -1;
    for (uint32_t i = 0; i < MODELRES_ELEM_COUNT; i++)
        if (caps[i] != s_pendCaps[i]) { changed++; if (first < 0) first = (int)i; }

    if (changed) {

        s_pendActive = false;
        return;
    }

    

    if (!s_pendShortened && now - s_pendStart >= 6 && modelRes) {
        for (uint32_t i = 0; i < MODELRES_ELEM_COUNT; i++) {
            uintptr_t e = modelRes + MODELRES_ELEMS + (uintptr_t)i * MODELRES_ELEM_SIZE;
            if (Rq(e + 0x10)) {
                uint32_t ctr = Rd32(modelRes + 8);
                if (ctr >= 2) { Wr32(modelRes + 8, 1); s_pendShortened = true; }
                break;
            }
        }
    }

    if (now - s_pendAt >= 25) {
        if (s_pendTries < 6) {
            s_pendTries++;
            s_pendAt = now;
            FlipVariantCache(modelRes);
        } else {
            s_pendActive = false;

        }
    }
}

static void RestoreModel()
{
    if (s_modelAddr && s_savedModel) Wr16(s_modelAddr, s_savedModel);
    s_hidden = false;
}

static DWORD WINAPI ModThread(LPVOID)
{
    g_proc = GetCurrentProcess();
    g_base = (uintptr_t)GetModuleHandleW(nullptr);
    timeBeginPeriod(1);

    uintptr_t chrAsm = 0, playerIns = 0, modelRes = 0;
    uint32_t lastStyle = 0xFFFFFFFF;
    DWORD lastParamTry = 0;

    while (g_running) {
        Sleep(1);

        if (!Mapped(chrAsm, 0x60)) {
            uintptr_t world = Rq(g_base + OFF_WORLD_CHR_MAN);
            playerIns = world ? Rq(world + OFF_PLAYERINS) : 0;
            uintptr_t pgd = playerIns ? Rq(playerIns + OFF_PLAYERINS_PGD) : 0;
            g_equipGD = pgd ? pgd + OFF_PGD_EQUIPGD : 0;
            uintptr_t cand = g_equipGD ? g_equipGD + OFF_EQUIPGD_CHRASM : 0;
            if (!cand || !Mapped(cand, 0x60)) { chrAsm = 0; continue; }
            if (s_hidden) { RestoreModel(); s_pendActive = false; }
            chrAsm = cand;
            lastStyle = 0xFFFFFFFF;

        }
        if (playerIns && !Rq(playerIns + OFF_PLAYERINS_MODEL)) continue;
        modelRes = Rq(playerIns + OFF_PLAYERINS_MODELRES);

        if (!g_wparamBase && Now() - lastParamTry > 3000) {
            lastParamTry = Now();
            g_wparamBase = FindWeaponParam();
        }

        TickRebuild(modelRes);

        uint32_t style = Rd32(chrAsm + OFF_CHRASM_STYLE);
        uint32_t lSlot = (Rd32(chrAsm + OFF_CHRASM_LIDX) ? 1u : 0u) * 2;
        uint32_t rSlot = (Rd32(chrAsm + OFF_CHRASM_RIDX) ? 1u : 0u) * 2 + 1;
        uint32_t lItem = Rd32(ItemAddr(g_equipGD, lSlot));
        uint32_t rItem = Rd32(ItemAddr(g_equipGD, rSlot));

        if (style != lastStyle) {

            lastStyle = style;
        }

        

        bool wantLeft = style == STYLE_2H_RIGHT &&
                        lItem && lItem != 0xFFFFFFFF;
        uint32_t tSlot = 0, tValue = 0;
        if (wantLeft) { tSlot = lSlot; tValue = lItem; }

        if (s_pendActive) continue;

        if (!tValue) {
            if (s_hidden) {

                RestoreModel();
                ArmRebuild(modelRes, "show");
            }
            continue;
        }

        if (!s_hidden) {
            uint32_t rowId = 0;
            uintptr_t row = FindRow(tValue, &rowId);
            if (!row) {  Sleep(500); continue; }
            uintptr_t modelAddr = row + WPARAM_MODELOFF;
            uint16_t  original  = OriginalModel(rowId, row);
            if (!original) {  Sleep(500); continue; }

            s_hidden = true; s_slot = tSlot; s_itemValue = tValue;
            s_rowId = rowId; s_modelAddr = modelAddr; s_savedModel = original;

            bool ok = Wr16(modelAddr, 0);

            ArmRebuild(modelRes, "hide");
            continue;
        }

        if (s_slot != tSlot || s_itemValue != tValue) {
            uint32_t newRow = 0;
            FindRow(tValue, &newRow);
            if (newRow && newRow == s_rowId) {

                s_itemValue = tValue; s_slot = tSlot;
                continue;
            }

            
            RestoreModel();
            continue;
        }

        if (s_modelAddr && Rd16(s_modelAddr) != 0) {

            Wr16(s_modelAddr, 0);
            ArmRebuild(modelRes, "rehide");
        }
    }

    if (s_hidden) RestoreModel();
    for (size_t i = 0; i < g_rowSaves.size(); i++) {
        uintptr_t row = FindRowById(g_rowSaves[i].rowId);
        if (row) Wr16(row + WPARAM_MODELOFF, g_rowSaves[i].model);
    }
    timeEndPeriod(1);

    return 0;
}

extern "C" void ModOnAttach(HMODULE)
{
    InterlockedExchange(&g_running, 1);
    HANDLE h = CreateThread(nullptr, 0, ModThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

extern "C" void ModOnDetach()
{
    InterlockedExchange(&g_running, 0);
}