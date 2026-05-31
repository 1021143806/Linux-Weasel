# Wisdom-Weasel Linux 移植方案

## 一、现状分析

### 项目架构概览

```
Wisdom-Weasel (Windows 输入法)
├── WeaselIME/          # IME 接口层 (纯 Windows)
├── WeaselTSF/          # TSF 接口层 (纯 Windows)
├── WeaselIPC/          # IPC 通信 (Windows 命名管道)
├── WeaselIPCServer/    # IPC 服务端
├── WeaselServer/       # 核心服务 + LLM 逻辑 ★
├── WeaselUI/           # UI 渲染 (WTL/GDI+)
├── WeaselDeployer/     # 部署工具 (WTL)
├── WeaselSetup/        # 安装程序
├── RimeWithWeasel/     # Rime 引擎集成层 ★
├── hf_backend/         # Python LLM 推理后端 (跨平台 ✓)
└── arm64x_wrapper/     # ARM64 包装
```

### Windows 耦合分类

| 类别 | 模块 | 耦合程度 | 说明 |
|------|------|----------|------|
| **不可移植** | WeaselIME, WeaselTSF | 100% | Windows 输入法框架专属 |
| **不可移植** | WeaselUI | 100% | WTL + GDI+ + HWND |
| **不可移植** | WeaselIPC, WeaselIPCServer | 95% | 命名管道 + HWND 消息 |
| **不可移植** | WeaselServer (部分) | 70% | WinSparkle, ShellExecute, 托盘图标 |
| **不可移植** | WeaselDeployer, WeaselSetup | 100% | WTL UI + NSIS |
| **可复用** | RimeWithWeasel (核心逻辑) | 40% | LLM 预测、上下文管理逻辑可复用 |
| **可复用** | WeaselServer/LLMProvider.* | 60% | OpenAI/llama.cpp 提供者逻辑可复用 |
| **可复用** | WeaselServer/ContextHistory.* | 90% | 纯 C++ 逻辑，几乎无平台依赖 |
| **可复用** | WeaselServer/MemoryCompressor.* | 80% | 记忆压缩逻辑可复用 |
| **跨平台** | hf_backend/ | 100% | Python FastAPI，天然跨平台 |

---

## 二、移植策略

### 核心思路：不重写输入法，而是作为 Rime 插件/辅助进程

Linux 下已有成熟的 Rime 前端：
- **ibus-rime** (GNOME/GTK 环境)
- **fcitx5-rime** (KDE/Qt 及通用环境)

Wisdom-Weasel 的 LLM 预测功能本质上是 **在 Rime 候选词之外追加 LLM 生成的候选词**。这个逻辑可以独立于输入法框架运行。

### 推荐方案：基于 fcitx5-rime 的插件模式

```
┌──────────────────────────────────────────────────┐
│                  fcitx5-rime                      │
│  (处理键盘输入、调用 librime、显示候选窗)          │
├──────────────────────────────────────────────────┤
│  Wisdom-Weasel Linux Daemon (新增)                │
│  ├── Rime API 监听 (通过 librime notification)    │
│  ├── LLM Provider (复用 OpenAICompatibleProvider) │
│  ├── ContextHistory (复用)                        │
│  ├── MemoryCompressor (复用)                      │
│  └── DBus/IPC 接口 (替代 Windows 命名管道)        │
├──────────────────────────────────────────────────┤
│  hf_backend/ (Python, 跨平台复用)                 │
└──────────────────────────────────────────────────┘
```

---

## 三、分步实施计划

### 阶段 1：提取平台无关核心库

**目标**：将 LLM 预测相关逻辑从 Windows 依赖中解耦

| 步骤 | 内容 | 新文件/目录 |
|------|------|------------|
| 1.1 | 提取 `LLMProvider` 基类和 `OpenAICompatibleProvider`，替换 WinHTTP 为 libcurl | `weasel-core/llm_provider.h`, `weasel-core/openai_provider.cpp` |
| 1.2 | 提取 `LlamaCppProvider`，移除 Windows 特定类型 | `weasel-core/llamacpp_provider.cpp` |
| 1.3 | 提取 `HFConstraintProvider`，替换 WinHTTP 为 libcurl | `weasel-core/hf_constraint_provider.cpp` |
| 1.4 | 提取 `ContextHistory`，几乎无修改 | `weasel-core/context_history.h`, `weasel-core/context_history.cpp` |
| 1.5 | 提取 `MemoryCompressor`，替换 WinHTTP 为 libcurl | `weasel-core/memory_compressor.h`, `weasel-core/memory_compressor.cpp` |
| 1.6 | 编写 CMakeLists.txt 构建系统 | `weasel-core/CMakeLists.txt` |

### 阶段 2：实现 Linux IPC 与守护进程

**目标**：创建 Linux 下的后台服务进程

| 步骤 | 内容 | 新文件/目录 |
|------|------|------------|
| 2.1 | 设计 DBus 接口（或 Unix Domain Socket）替代 Windows 命名管道 | `weasel-daemon/ipc.h`, `weasel-daemon/ipc.cpp` |
| 2.2 | 实现守护进程主循环 | `weasel-daemon/main.cpp` |
| 2.3 | 集成 librime，通过 Rime API 监听 commit 事件 | `weasel-daemon/rime_listener.cpp` |
| 2.4 | 实现配置加载（读取 `weasel.yaml`） | `weasel-daemon/config_loader.cpp` |
| 2.5 | 实现候选词注入（通过 fcitx5 addon API 或直接修改 Rime context） | `weasel-daemon/candidate_injector.cpp` |

### 阶段 3：fcitx5 插件集成

**目标**：将 LLM 候选词显示在 fcitx5 候选窗中

| 步骤 | 内容 | 新文件/目录 |
|------|------|------------|
| 3.1 | 编写 fcitx5 addon（C++ 插件） | `fcitx5-addon/wisdom-weasel.cpp` |
| 3.2 | 实现候选词追加逻辑 | `fcitx5-addon/candidate_provider.cpp` |
| 3.3 | 编写 addon 配置文件 | `fcitx5-addon/wisdom-weasel.conf` |
| 3.4 | 编写 CMakeLists.txt | `fcitx5-addon/CMakeLists.txt` |

### 阶段 4：构建系统与打包

| 步骤 | 内容 |
|------|------|
| 4.1 | 顶层 CMakeLists.txt，统一构建 |
| 4.2 | 编写 install 脚本 |
| 4.3 | 编写 Arch Linux PKGBUILD / Ubuntu debian 打包 |
| 4.4 | 更新 README.md 添加 Linux 安装说明 |

---

## 四、关键技术决策

### 4.1 HTTP 客户端：WinHTTP → libcurl

当前 `OpenAICompatibleProvider::ExecuteRequest()` 和 `HFConstraintProvider::ExecuteRequest()` 使用 WinHTTP API (`WinHttpOpen`, `WinHttpConnect`, `WinHttpSendRequest` 等)。需替换为 libcurl。

**改动范围**：
- `WeaselServer/LLMProvider.cpp` 中 `OpenAICompatibleProvider::ExecuteRequest()` (约 100 行)
- `WeaselServer/LLMProvider.cpp` 中 `HFConstraintProvider::ExecuteRequest()` (约 100 行)
- `WeaselServer/MemoryCompressor.cpp` 中的 HTTP 请求

### 4.2 字符串编码：wstring → UTF-8 string

当前大量使用 `std::wstring`（Windows 原生宽字符），Linux 下应统一使用 UTF-8 `std::string`。

**改动范围**：几乎所有文件，但可通过条件编译宏逐步迁移。

### 4.3 IPC：命名管道 → Unix Domain Socket / DBus

当前使用 `\\.\pipe\` 命名管道 + `HWND` 消息窗口。Linux 下推荐：
- **Unix Domain Socket**：简单、高性能、无需额外依赖
- **DBus**：与 fcitx5 生态集成更好

### 4.4 候选词注入方式

两种可行方案：
1. **通过 librime API 直接注入**：在 Rime context 中追加候选词（需要研究 librime 是否支持）
2. **通过 fcitx5 addon API**：fcitx5 支持插件扩展候选词列表

---

## 五、风险与注意事项

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| librime 不支持动态注入候选词 | 高 | 研究 fcitx5 addon API 作为备选 |
| llama.cpp 在 Linux 下的编译链接 | 中 | llama.cpp 原生支持 Linux，风险较低 |
| wstring 到 UTF-8 迁移工作量大 | 中 | 使用条件编译，逐步迁移 |
| fcitx5 与 ibus 双框架兼容 | 低 | 优先支持 fcitx5，ibus 后续适配 |
| 用户配置路径差异 | 低 | Linux 使用 `~/.config/rime/` |

---

## 六、预估工作量分布

| 阶段 | 工作内容 | 主要难度 |
|------|----------|----------|
| 阶段 1 | 提取核心库 | 中等：大量代码重构，但逻辑不变 |
| 阶段 2 | Linux 守护进程 | 中等：新代码编写 |
| 阶段 3 | fcitx5 插件 | 较高：需要理解 fcitx5 addon 框架 |
| 阶段 4 | 构建打包 | 低：CMake + 打包脚本 |

---

## 七、建议优先顺序

1. **先做阶段 1**（提取核心库）— 这是所有后续工作的基础
2. **再做阶段 2**（守护进程）— 验证 LLM 预测在 Linux 下能正常工作
3. **最后做阶段 3**（fcitx5 集成）— 打通端到端用户体验

---

---

## 八、当前 Linux 环境分析

### 8.1 运行环境

| 项目 | 值 |
|------|-----|
| **服务器** | `ssh_myArch` (CatOS / Arch Linux) |
| **内核** | Linux 6.18.9-arch1-2 |
| **桌面环境** | KDE Plasma (Wayland) |
| **用户** | a1 (sudo) |
| **架构** | x86_64 |
| **包管理器** | pacman |

### 8.2 当前输入法环境

| 项目 | 值 |
|------|-----|
| **输入法框架** | **fcitx5** (5.1.17-1) |
| **Rime 插件** | **fcitx5-rime** (5.1.12-1) ✅ 已安装并启用 |
| **librime 版本** | **1:1.16.1-1** (较新版本) |
| **默认输入法** | rime (月拼音/地球拼音) |
| **显示协议** | Wayland |
| **环境变量** | `XMODIFIERS=@im=fcitx`, `GTK_IM_MODULE=fcitx`, `QT_IM_MODULE=fcitx` |

### 8.3 移植依赖可用性评估

| 依赖 | 用途 | Linux 可用性 | 备注 |
|------|------|-------------|------|
| **librime** (1.16.1) | Rime 输入法引擎核心 | ✅ `pacman -S librime` | 已安装，版本很新 |
| **fcitx5** (5.1.17) | 输入法框架 | ✅ `pacman -S fcitx5` | 已安装并运行中 |
| **fcitx5-rime** (5.1.12) | fcitx5 的 Rime 插件 | ✅ `pacman -S fcitx5-rime` | 已安装，默认输入法 |
| **libcurl** | HTTP 客户端 (替代 WinHTTP) | ✅ `pacman -S curl` | 系统预装 |
| **Boost** | C++ 库 (序列化等) | ✅ `pacman -S boost` | Arch 滚动更新，版本新 |
| **yaml-cpp** | YAML 配置解析 | ✅ `pacman -S yaml-cpp` | 官方源有 |
| **llama.cpp** | 本地 LLM 推理 | ✅ 源码编译 或 AUR | 原生支持 Linux |
| **CMake** | 构建系统 | ✅ `pacman -S cmake` | 系统预装 |
| **Python 3.10+** | hf_backend | ✅ 系统自带 | Arch 通常预装最新 Python |
| **GCC/Clang** | C++17 编译器 | ✅ `pacman -S gcc` 或 clang | 满足 C++17 |

### 8.4 不可用的 Windows 专属组件

| 组件 | 替代方案 |
|------|----------|
| MSVC / MSBuild | GCC/Clang + CMake |
| WTL (Windows Template Library) | 不需要 (由 fcitx5 负责 UI) |
| WinHTTP | libcurl |
| Windows 命名管道 | Unix Domain Socket / DBus |
| WinSparkle (自动更新) | 可选：AUR 更新机制 或 暂不实现 |
| NSIS 安装包 | PKGBUILD (AUR) |
| TSF/IME 框架 | fcitx5 addon API |

### 8.5 环境就绪度

当前 Arch Linux (CatOS) 环境 **完全满足移植开发需求**，且有以下优势：

- **fcitx5 + fcitx5-rime 已就绪**：输入法框架和 Rime 插件已在运行，无需额外配置
- **librime 1.16.1**：版本较新，API 兼容性好
- **KDE Plasma + Wayland**：现代桌面环境，fcitx5 原生支持
- **AUR**：llama.cpp 等可通过 AUR 安装，也可源码编译
- **PKGBUILD**：最终可直接发布到 AUR，Arch 用户安装方便

**建议在开始前执行的一键环境准备**：
```bash
sudo pacman -S --needed \
  base-devel cmake pkg-config \
  librime fcitx5 fcitx5-rime \
  curl libcurl-compat \
  boost yaml-cpp \
  python python-pip python-virtualenv
```

---

*本计划由 Architect 模式生成，待用户审核确认后进入 Code 模式实施。*
