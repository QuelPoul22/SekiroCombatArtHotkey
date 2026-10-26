#pragma once

#include <Windows.h>

namespace Hotkey
{
    // 从 .\configs\sekirocombatart.ini 读取热键（支持键名或 VK 码）
    void LoadFromIni();

    // 启动按键轮询线程
    void StartThread();

    // 停止按键轮询线程
    void StopThread();

    // 供 ProcessInputs 钩子原子读取/清除"当前要按住的动作位"
    LONGLONG* ActionMaskPtr();
}
