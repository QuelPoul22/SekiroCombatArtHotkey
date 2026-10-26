#include <sekiro.h>
#include <hotkey.h>
#include <MinHook.h>
#include <vector>

// ============================================================
// 以下所有地址均取自原项目 SekiroHotkeySystem 的 release 版本
// （无 ASLR，imagebase 0x140000000）。Sekiro 更新版本后需重新定位。
// ============================================================

// 技能/装备数据基址（SkillBase）
static byte* SkillBase = reinterpret_cast<byte*>(0x143D5AAC0);
// 存档世界实体基址（判断是否进入游戏世界）
static byte* MenuEntityBase = reinterpret_cast<byte*>(0x143D67408);
static byte* WorldEntityBase = reinterpret_cast<byte*>(0x143D7A1E0);
// 打开系统菜单的标志
static const bool* const IsInMenuVar = reinterpret_cast<const bool*>(0x143D8990C);

// "无"武技槽的 realID（与原项目 GetNameFromRealID / GetCombatArts 的约定一致）
static const unsigned COMBAT_ART_NONE_REALID = 5000;

// 游戏的"realID -> 菜单ID"转换函数：0x140C3D680
typedef uint64_t(__fastcall* tGetMenuID)(uint64_t equipInventoryDataPtr, unsigned* realID);
static const tGetMenuID GetMenuID = reinterpret_cast<tGetMenuID>(0x140C3D680);

// 游戏每帧输入处理函数：0x140B2C190（本项目唯一需要钩住的游戏函数）
typedef uint64_t(__fastcall* tProcessInputs)(uint64_t inputProcessBase, uint64_t arg2);
static const tProcessInputs ProcessInputs = reinterpret_cast<tProcessInputs>(0x140B2C190);
static tProcessInputs ProcessInputsOrig = nullptr;

// 逐层解引用指针链（沿用原项目实现：用 ReadProcessMemory 读，空指针安全退出）
static byte* ResolvePointerChain(byte* baseAddress, const std::vector<int>& offsets)
{
    byte* retAddress = baseAddress;
    for (const auto& offset : offsets)
    {
        if (retAddress)
            ReadProcessMemory(GetCurrentProcess(), retAddress, &retAddress, sizeof(retAddress), nullptr);

        if (retAddress == nullptr)
            break;

        retAddress += offset;
    }

    return reinterpret_cast<uint64_t>(retAddress) < 0x140000000 ? nullptr : retAddress;
}

uint64_t Sekiro::GetSkillBase()
{
    return reinterpret_cast<uint64_t>(ResolvePointerChain(SkillBase, { 0x08, 0x278 }));
}

uint64_t Sekiro::GetEquipInventoryData()
{
    // 0x143D5AAC0 -> +0x8 (玩家数据) -> +0x5B0 (装备栏数据)
    uint64_t gameData = *reinterpret_cast<uint64_t*>(SkillBase);
    if (!gameData)
        return 0;

    uint64_t playerGameData = *reinterpret_cast<uint64_t*>(gameData + 0x8);
    if (!playerGameData)
        return 0;

    return *reinterpret_cast<uint64_t*>(playerGameData + 0x5B0);
}

bool Sekiro::IsGameLoaded()
{
    uint64_t menuEntity = *reinterpret_cast<uint64_t*>(MenuEntityBase);
    if (!menuEntity)
        return false;

    uint64_t worldEntity = *reinterpret_cast<uint64_t*>(WorldEntityBase);

    return *reinterpret_cast<bool*>(menuEntity + 0x248) && worldEntity != 0;
}

bool Sekiro::IsInMenu()
{
    return *IsInMenuVar;
}

bool Sekiro::IsCombatArtEquipped()
{
    uint64_t skillBase = GetSkillBase();
    if (skillBase == 0)
        return false;

    // 当前装备武技的菜单ID存于 skillBase + 0x28（原项目 SelectCombatArt 同位置）
    unsigned currentMenuID = *reinterpret_cast<unsigned*>(skillBase + 0x28);

    // 槽值异常（未初始化）视为无有效武技
    if (currentMenuID == 0)
        return false;

    // "无"武技槽对应的菜单ID：首次需要时通过游戏函数换算并缓存
    static unsigned noneMenuID = 0xFFFFFFFF;
    if (noneMenuID == 0xFFFFFFFF)
    {
        uint64_t equipInventoryData = GetEquipInventoryData();
        if (equipInventoryData == 0)
            return true; // 暂时取不到数据：放行，由游戏自身处理

        unsigned realID = COMBAT_ART_NONE_REALID;
        uint64_t id = GetMenuID(equipInventoryData + 0x10, &realID);
        if (id == 0xFFFFFFFF)
            return true; // 换算失败：放行

        noneMenuID = static_cast<unsigned>(id);
    }

    return currentMenuID != noneMenuID;
}

static HWND GameWindowHandle = nullptr;

static BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM)
{
    static WCHAR buffer[50];
    GetWindowTextW(hwnd, buffer, 50);
    if (wcsstr(buffer, L"Sekiro"))
    {
        GameWindowHandle = hwnd;
        return FALSE;
    }

    return TRUE;
}

HWND Sekiro::GetGameWindow()
{
    if (GameWindowHandle == nullptr || !IsWindow(GameWindowHandle))
    {
        GameWindowHandle = nullptr;
        EnumWindows(FindGameWindow, 0);
    }

    return GameWindowHandle;
}

// 每帧在游戏处理输入前调用：若热键被按住，把"武技"动作位 OR 进
// 输入处理器的动作位域（inputHandler + 0x10），效果等同玩家按住武技键，
// 释放的就是玩家当前装备好的武技。
static uint64_t __fastcall ProcessInputsDetour(uint64_t inputHandler, uint64_t a2)
{
    uint64_t actionBitfield = static_cast<uint64_t>(InterlockedOr64(Hotkey::ActionMaskPtr(), 0));
    if (actionBitfield != 0)
        *reinterpret_cast<uint64_t*>(inputHandler + 0x10) |= actionBitfield;

    return ProcessInputsOrig(inputHandler, a2);
}

void Sekiro::InstallGameHooks()
{
    if (MH_CreateHook(ProcessInputs, ProcessInputsDetour, reinterpret_cast<LPVOID*>(&ProcessInputsOrig)) != MH_OK)
        return;

    if (MH_EnableHook(ProcessInputs) != MH_OK)
        ProcessInputsOrig = nullptr;
}

void Sekiro::RemoveGameHooks()
{
    if (ProcessInputsOrig != nullptr)
        MH_DisableHook(ProcessInputs);
}
