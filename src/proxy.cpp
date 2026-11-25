// dinput8.dll proxy: forwards DirectInput8Create and hosts the mod.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE, DWORD, const void* ,
    void** , void* );

static HMODULE g_real = nullptr;
static DirectInput8Create_t g_realCreate = nullptr;
static HMODULE g_self = nullptr;

extern "C" void ModOnAttach(HMODULE self);
extern "C" void ModOnDetach();

static void LoadReal()
{
    if (g_real) return;
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\dinput8.dll");
    g_real = LoadLibraryW(path);
    if (g_real)
        g_realCreate = (DirectInput8Create_t)GetProcAddress(g_real, "DirectInput8Create");
}

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE a, DWORD b,
    const void* c, void** d, void* e)
{
    LoadReal();
    if (!g_realCreate) return (HRESULT)0x8007007EL; 
    return g_realCreate(a, b, c, d, e);
}

extern "C" __declspec(dllexport) HRESULT WINAPI GetdfDIJoystick(void)
{
    LoadReal();
    if (g_real) {
        typedef HRESULT(WINAPI* F)(void);
        F f = (F)GetProcAddress(g_real, "GetdfDIJoystick");
        if (f) return f();
    }
    return 0;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void)
{
    LoadReal();
    typedef HRESULT(WINAPI* F)(void);
    F f = g_real ? (F)GetProcAddress(g_real, "DllCanUnloadNow") : nullptr;
    return f ? f() : S_FALSE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out)
{
    LoadReal();
    typedef HRESULT(WINAPI* F)(REFCLSID, REFIID, void**);
    F f = g_real ? (F)GetProcAddress(g_real, "DllGetClassObject") : nullptr;
    return f ? f(clsid, iid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllRegisterServer(void)
{
    LoadReal();
    typedef HRESULT(WINAPI* F)(void);
    F f = g_real ? (F)GetProcAddress(g_real, "DllRegisterServer") : nullptr;
    return f ? f() : E_FAIL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer(void)
{
    LoadReal();
    typedef HRESULT(WINAPI* F)(void);
    F f = g_real ? (F)GetProcAddress(g_real, "DllUnregisterServer") : nullptr;
    return f ? f() : E_FAIL;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);
        ModOnAttach(hModule);
    }
    return TRUE;
}
