#include <hotkey.h>
#include <sekiro.h>
#include <atomic>
#include <thread>
#include <string>
#include <cwctype>

// ============================================================
// 热键轮询模块
//   每 20ms 检查一次热键物理按下状态（GetAsyncKeyState）。
//   只有在：游戏窗口在前台 + 已进入世界 + 没打开系统菜单 + 当前确实
//   装备了有效武技 时，才把"武技"动作位写入待注入掩码。
//   ProcessInputs 钩子（sekiro.cpp）每帧读取该掩码并 OR 进游戏输入位域，
//   按住期间持续生效（等同玩家按住武技键），松开即停。
// ============================================================

static volatile bool IsRunning = false;
static volatile LONGLONG PendingAction = 0;   // 全部通过 Interlocked* 访问

static UINT CombatArtVK = 'Q';          // 默认 Q
static bool RequireNoModifier = false;  // RequireNoModifier=1 时：按住 Shift/Ctrl/Alt 不触发

static bool IsKeyPhysicalDown(UINT vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool AnyModifierDown()
{
    return IsKeyPhysicalDown(VK_SHIFT) || IsKeyPhysicalDown(VK_CONTROL) || IsKeyPhysicalDown(VK_MENU);
}

// 键名 -> Virtual-Key 码（覆盖字母、数字、功能键、小键盘与常用修饰键；
// 同时也支持 ini 里直接填十进制 VK 码，如 81 = Q）
static UINT KeyNameToVK(const std::wstring& name)
{
    if (name.empty())
        return 0;

    if (name.size() == 1)
    {
        wchar_t c = static_cast<wchar_t>(std::towupper(name[0]));
        if (c >= L'A' && c <= L'Z') return static_cast<UINT>(c);
        if (c >= L'0' && c <= L'9') return static_cast<UINT>(c);
    }

    // 全大写比较
    std::wstring u;
    for (wchar_t c : name) u += static_cast<wchar_t>(std::towupper(c));

    if (u == L"SPACE")      return VK_SPACE;
    if (u == L"TAB")        return VK_TAB;
    if (u == L"ENTER")      return VK_RETURN;
    if (u == L"RETURN")     return VK_RETURN;
    if (u == L"ESCAPE")     return VK_ESCAPE;
    if (u == L"BACKSPACE")  return VK_BACK;
    if (u == L"DELETE")     return VK_DELETE;
    if (u == L"INSERT")     return VK_INSERT;
    if (u == L"HOME")       return VK_HOME;
    if (u == L"END")        return VK_END;
    if (u == L"PAGEUP")     return VK_PRIOR;
    if (u == L"PAGEDOWN")   return VK_NEXT;
    if (u == L"LEFT")       return VK_LEFT;
    if (u == L"RIGHT")      return VK_RIGHT;
    if (u == L"UP")         return VK_UP;
    if (u == L"DOWN")       return VK_DOWN;
    if (u == L"CAPSLOCK")   return VK_CAPITAL;
    if (u == L"SHIFT")      return VK_SHIFT;
    if (u == L"LCTRL" || u == L"CTRL" || u == L"CONTROL") return VK_LCONTROL;
    if (u == L"RCTRL")      return VK_RCONTROL;
    if (u == L"LSHIFT")     return VK_LSHIFT;
    if (u == L"RSHIFT")     return VK_RSHIFT;
    if (u == L"LALT" || u == L"ALT") return VK_LMENU;
    if (u == L"RALT")       return VK_RMENU;
    if (u == L"MENU")       return VK_APPS;
    if (u == L"MB1" || u == L"MOUSE1") return VK_LBUTTON;
    if (u == L"MB2" || u == L"MOUSE2") return VK_RBUTTON;
    if (u == L"MB3" || u == L"MOUSE3") return VK_MBUTTON;
    if (u == L"MB4" || u == L"MOUSE4") return VK_XBUTTON1;
    if (u == L"MB5" || u == L"MOUSE5") return VK_XBUTTON2;
    if (u == L"MINUS")      return VK_OEM_MINUS;
    if (u == L"EQUAL")      return VK_OEM_PLUS;
    if (u == L"COMMA")      return VK_OEM_COMMA;
    if (u == L"PERIOD")     return VK_OEM_PERIOD;
    if (u == L"SLASH")      return VK_OEM_2;
    if (u == L"SEMICOLON")  return VK_OEM_1;
    if (u == L"APOSTROPHE") return VK_OEM_7;
    if (u == L"LBRACKET")   return VK_OEM_6;
    if (u == L"RBRACKET")   return VK_OEM_4;
    if (u == L"BACKSLASH")  return VK_OEM_5;
    if (u == L"GRAVE")      return VK_OEM_3;

    // F1..F12 / NUM1..NUM9 / NUM0 / NUMLOCK / NUMPLUS ...
    if (u[0] == L'F')
    {
        int idx = -1;
        try { idx = std::stoi(u.substr(1)); } catch (...) {}
        if (idx >= 1 && idx <= 12) return static_cast<UINT>(VK_F1 + idx - 1);
    }
    if (u == L"NUMLOCK") return VK_NUMLOCK;
    if (u.size() >= 4 && u.compare(0, 3, L"NUM") == 0)
    {
        std::wstring rest = u.substr(3);
        if (rest == L"PLUS")   return VK_ADD;
        if (rest == L"MINUS")  return VK_SUBTRACT;
        if (rest == L"MUL")    return VK_MULTIPLY;
        if (rest == L"DIV")    return VK_DIVIDE;
        if (rest == L"ENTER")  return VK_RETURN;
        if (rest.size() == 1 && rest[0] >= L'0' && rest[0] <= L'9')
            return static_cast<UINT>(VK_NUMPAD0 + (rest[0] - L'0'));
    }

    // 纯数字：视为十进制 VK 码
    bool allDigits = true;
    for (wchar_t c : u) if (c < L'0' || c > L'9') { allDigits = false; break; }
    if (allDigits)
    {
        try {
            long v = std::stol(u);
            if (v > 0 && v <= 0xFE) return static_cast<UINT>(v);
        } catch (...) {}
    }

    return 0;
}

static std::wstring IniReadWideString(const wchar_t* section, const wchar_t* key, const wchar_t* def)
{
    std::wstring buf(64, L'\0');
    DWORD len = GetPrivateProfileStringW(section, key, def, &buf[0], static_cast<DWORD>(buf.size()), L".\\configs\\sekirocombatart.ini");
    buf.resize(len);
    return buf;
}

void Hotkey::LoadFromIni()
{
    std::wstring key = IniReadWideString(L"Hotkeys", L"CombatArtKey", L"Q");
    UINT vk = KeyNameToVK(key);
    if (vk != 0)
        CombatArtVK = vk;

    RequireNoModifier = GetPrivateProfileIntW(L"Hotkeys", L"RequireNoModifier", 0, L".\\configs\\sekirocombatart.ini") != 0;
}

static void PollLoop()
{
    while (IsRunning)
    {
        Sleep(20);

        // 未装钩子（安装失败）时不注入
        if (!IsRunning)
            break;

        if (!IsKeyPhysicalDown(CombatArtVK))
        {
            InterlockedExchange64(&PendingAction, 0);
            continue;
        }

        // 条件门槛：前台窗口是游戏、存档已加载、没在系统菜单里
        if (GetForegroundWindow() != Sekiro::GetGameWindow())
        {
            InterlockedExchange64(&PendingAction, 0);
            continue;
        }

        if (!Sekiro::IsGameLoaded() || Sekiro::IsInMenu())
        {
            InterlockedExchange64(&PendingAction, 0);
            continue;
        }

        if (RequireNoModifier && AnyModifierDown())
        {
            InterlockedExchange64(&PendingAction, 0);
            continue;
        }

        // 核心要求：只对"当前已装备的武技"生效，未装备（空槽）不注入
        if (!Sekiro::IsCombatArtEquipped())
        {
            InterlockedExchange64(&PendingAction, 0);
            continue;
        }

        InterlockedExchange64(&PendingAction, static_cast<LONGLONG>(Sekiro::ACTION_COMBAT_ART));
    }

    InterlockedExchange64(&PendingAction, 0);
}

void Hotkey::StartThread()
{
    IsRunning = true;
    std::thread(PollLoop).detach();
}

void Hotkey::StopThread()
{
    IsRunning = false;
    InterlockedExchange64(&PendingAction, 0);
}

LONGLONG* Hotkey::ActionMaskPtr()
{
    return const_cast<LONGLONG*>(&PendingAction);
}
