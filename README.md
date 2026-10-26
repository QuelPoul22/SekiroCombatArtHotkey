# SekiroCombatArtHotkey

基于 [ReaperAnon/SekiroHotkeySystem](https://github.com/ReaperAnon/SekiroHotkeySystem) 精简改造的只读功能 MOD：

**按下热键（默认 Q）→ 释放"当前已装备"的武技。** 其他功能（游戏内配置菜单、装备切换、存档 profile、HUD 小部件、ImGui/DXGI 渲染）已全部移除，只保留一个钩子。

## 原理

沿用原项目的"代理 dinput8.dll"注入方式，但游戏侧只钩一个函数：

1. 伪装成 `dinput8.dll` 被游戏加载，仅转发导出 `DirectInput8Create`，其余走真 DLL 或 chainload 的其他 mod；
2. 每 20ms 轮询热键物理状态（`GetAsyncKeyState`），仅当 **游戏窗口在前台 + 已进入世界 + 未打开系统菜单 + 当前武技槽不是"无"** 时才放行；
3. MinHook 钩住游戏每帧输入处理函数 `ProcessInputs`（0x140B2C190），把武技动作位（值 5）OR 进输入对象 `+0x10` 的动作位域——效果与玩家自己按住武技键完全一致，所以**放出来的必然是当前装备栏里装好的武技**，不可能放出没装备的。

## 使用

1. 关闭游戏，把 `release\dinput8.dll`、`release\MinHook.x64.dll`、`release\configs\` 整个复制到 `sekiro.exe` 所在目录；
2. 编辑 `configs\sekirocombatart.ini` 里的 `CombatArtKey`（支持 `Q`、`F1-F12`、`SPACE`、`MB4` 鼠标侧键、小键盘键或十进制 VK 码等，见 ini 内注释）；
3. 进游戏即可。若游戏里本来就用键盘某个键作为武技键，本 MOD 相当于给了它第二个按键。
4. 支持链式加载，你可以把本项目的dinput8.dll改名为****.dll，然后用modengine来加载dll。
5. 本项目也可以支持其他mod的dinput8.dll，把其他项目的dinput8.dll改成****.dll，然后在"\config\chainload.ini"里面添加****.dll

## 编译

- 环境：Visual Studio 2022（MSVC v143），x64 Release；
- 工程：`SekiroCombatArtHotkey.vcxproj`（或命令行 `MSBuild.exe SekiroCombatArtHotkey.vcxproj /p:Configuration=Release /p:Platform=x64`）；
- 依赖：仅 `lib\MinHook.x64.lib`（导入库，随包提供；运行时需同目录的 `MinHook.x64.dll`）；
- 输出：直接生成到 `release\dinput8.dll`。

## 重要提醒（务必阅读）

1. **版本耦合**：所有游戏地址（`0x143D5AAC0` 指针链、`0x140B2C190` 输入函数、"无武技槽"换算函数 `0x140C3D680` 等）与原项目 release 完全一致，只对作者打包时的 Sekiro 版本有效。游戏更新后需重新定位地址（改动集中在 `source\sekiro.cpp` 顶部）。MOD 本身做了防御：地址失效时表现为热键无反应或游戏无法进世界，但不会主动写内存，崩溃风险低。目前测试1.06的只狼没有问题。
2. **无运行库依赖**：工程以 /MT 静态链接 CRT，成品只依赖 `MinHook.x64.dll` 与系统 DLL，目标机器无需安装 VC++ Redistributable。
3. 钩子安装前固定等待 10 秒（沿用原项目策略），启动游戏后稍等再按热键。
4. 有关UI之类的代码已经删除了，尽力节省各位的电脑磁盘空间。

## 文件结构

```
SekiroCombatArtHotkey\
├─ source\  dllmain.cpp（代理入口/线程）  sekiro.cpp（地址+钩子）  hotkey.cpp（热键+ini）
├─ headers\ 对应头文件 + MinHook.h
├─ lib\     MinHook.x64.lib
├─ release\ 成品：dinput8.dll + MinHook.x64.dll + configs\*.ini
├─ SekiroCombatArtHotkey.vcxproj(.filters)
├─ LICENSE
└─ README.md
```

## Credits

- [SekiroHotkeySystem](https://github.com/ReaperAnon/SekiroHotkeySystem) by ReaperAnon — original project (MIT)
- [MinHook](https://github.com/TsudaKageyu/minhook) — hooking library

Modifications © 2026 QuelPoul22, also under MIT. See [LICENSE](LICENSE).