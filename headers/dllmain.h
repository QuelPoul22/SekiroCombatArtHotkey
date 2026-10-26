#pragma once

// 与原项目一致的导出宏：以标准 Windows 调用约定导出，伪装成真的 dinput8.dll
#define FUNCWRAPPER HRESULT __declspec(dllexport) WINAPI

#include <Windows.h>

// 真 dinput8.dll 的 DirectInput8Create 函数指针类型
typedef HRESULT(__stdcall* tDirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

void HookThread();

// 加载 chainload.ini 指定的其他 mod DLL；未配置则加载系统真 dinput8.dll
void LoadDll();

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason_for_call, LPVOID lpReserved);
