#pragma once

#include <Windows.h>
#include <cstdint>

// ============================================================
// 只读自原项目 ReaperAnon/SekiroHotkeySystem 的必要地址。
// 这些地址对应作者打包时的 Sekiro 版本（无 ASLR，imagebase 0x140000000）。
// 游戏大版本更新后地址可能变化，届时需要重新定位以下三处：
//   ProcessInputs 函数地址 / IsInMenu 标志 / IsGameLoaded 两处指针
// ============================================================

namespace Sekiro
{
    // 游戏输入动作位掩码（取自原项目 input_injection.h，只保留用到的值）
    enum Action : uint64_t
    {
        ACTION_COMBAT_ART = 5,   // 武技键（同时按下即释放当前装备的武技）
    };

    // 是否已进入游戏世界（存档加载完成）
    bool IsGameLoaded();

    // 是否打开着游戏系统菜单
    bool IsInMenu();

    // 当前是否装备了有效武技（"无"槽位返回 false，防止释放未装备的武技）
    bool IsCombatArtEquipped();

    // 装备数据基址与装备栏数据
    uint64_t GetSkillBase();
    uint64_t GetEquipInventoryData();

    // 查找/缓存 Sekiro 游戏窗口句柄，找不到返回 nullptr
    HWND GetGameWindow();

    // 通过 MinHook 安装 ProcessInputs 钩子
    void InstallGameHooks();

    // 卸载钩子
    void RemoveGameHooks();
}
