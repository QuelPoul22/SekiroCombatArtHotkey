#include "dllmain.h"
#include <MinHook.h>
#include <sekiro.h>
#include <hotkey.h>
#include <thread>

// 真 dinput8.dll（或其他 mod 的代理 dll）的模块句柄与原始函数指针
static HMODULE DInput8DLL = nullptr;
static tDirectInput8Create DirectInput8CreateOrig = nullptr;
static WCHAR dllPath[MAX_PATH] = L"";

// 加载 chainload.ini 指定的其他 mod DLL（同名 dinput8 冲突时链式加载用）；
// 未配置则加载系统目录下的真正 dinput8.dll，保证游戏输入功能不受影响。
void LoadDll()
{
    GetPrivateProfileString(L"Misc", L"chainDllName", L"", dllPath, 100, L".\\configs\\chainload.ini");
    if (wcslen(dllPath) > 0)
    {
        DInput8DLL = LoadLibrary(dllPath);
        if (DInput8DLL != nullptr)
            DirectInput8CreateOrig = (tDirectInput8Create)GetProcAddress(DInput8DLL, "DirectInput8Create");
    }

    if (DirectInput8CreateOrig == nullptr)
    {
        GetSystemDirectory(dllPath, MAX_PATH);
        wcscat_s(dllPath, L"\\dinput8.dll");
        DInput8DLL = LoadLibrary(dllPath);
        if (DInput8DLL != nullptr)
            DirectInput8CreateOrig = (tDirectInput8Create)GetProcAddress(DInput8DLL, "DirectInput8Create");
    }
}

// 注入后在游戏进程内执行的初始化线程。
// Sleep 10 秒是沿用原项目做法：等游戏完成初始化后再装钩子。
void HookThread()
{
    LoadDll();

    Sleep(10000);

    MH_Initialize();
    Sekiro::InstallGameHooks();
    Hotkey::LoadFromIni();
    Hotkey::StartThread();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD callReason, LPVOID lpReserved)
{
    switch (callReason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            std::thread(HookThread).detach();
            break;

        case DLL_PROCESS_DETACH:
            Hotkey::StopThread();
            Sekiro::RemoveGameHooks();
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            break;
    }

    return TRUE;
}

// 唯一的导出函数：对游戏透明地转发给真正的 DirectInput8Create，
// 这是"伪装成 dinput8.dll 的代理 DLL"机制的核心。
extern "C"
{
    FUNCWRAPPER DirectInput8Create(HINSTANCE hInst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
    {
        return DirectInput8CreateOrig(hInst, dwVersion, riidltf, ppvOut, punkOuter);
    }
}
