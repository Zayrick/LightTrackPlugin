# LightTrackPlugin

[English](README.md) | **简体中文**

[![License: GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue.svg)](LICENSE)
[![OpenRGB](https://img.shields.io/badge/OpenRGB-1.0%20(Plugin%20API%205)-brightgreen.svg)](https://openrgb.org)
[![Qt](https://img.shields.io/badge/Qt-5.10%2B%20%7C%206-41cd52.svg)](https://www.qt.io)

LightTrackPlugin 是一款基于时间线的 [OpenRGB](https://openrgb.org) 灯光编排插件。你可以将灯效以片段形式排布在各设备的轨道上，与音频同步播放，并将结果保存为可复用的布局。

插件使用 C++17 编写，同时支持 Qt 5 与 Qt 6 构建。

## 目录

- [功能特性](#功能特性)
- [兼容性](#兼容性)
- [安装](#安装)
- [使用说明](#使用说明)
- [从源码构建](#从源码构建)
- [运行测试](#运行测试)
- [项目结构](#项目结构)
- [参与贡献](#参与贡献)
- [许可证](#许可证)

## 功能特性

- **时间线编辑器**：在设备、zone、segment 各自的轨道上放置、移动、调整、复制和编组灯效片段。
- **OpenRGB 灯效**：直接编译集成 [OpenRGBEffectsPlugin](https://gitlab.com/OpenRGBDevelopers/OpenRGBEffectsPlugin) 的灯效库。
- **音频同步**：加载音乐（WAV、MP3、FLAC、OGG），显示波形，支持拖动试听及点击音轨跳转。
- **分层渲染**：重叠片段按轨道优先级合成，矩阵设备按 segment 路由输出。
- **布局持久化**：保存与加载布局，内容包括音乐、灯效片段，以及各设备/zone/segment 的名称和开灯/禁用状态。
- **撤销 / 重做**：完整的编辑历史，支持多片段操作。
- **与其他插件共存**：只写入时间线上有片段的设备，且不持续覆盖空闲区域（详见[播放行为](#播放行为)）。

## 兼容性

| 组件 | 支持版本 |
| --- | --- |
| OpenRGB | 1.0（插件 API 5） |
| Qt | 5.10 及以上，或 Qt 6 |
| 平台 | Windows x64、Linux x64 |

> [!IMPORTANT]
> 插件所用 Qt 的主版本和 CPU 架构必须与加载它的 OpenRGB 宿主一致。OpenRGB 1.0 官方 Windows 版本使用 **Qt 6.8.3（MSVC 2022，x64）**。

OpenRGB 相关依赖固定在 `release_1.0` 分支：

| 子模块 | 提交 |
| --- | --- |
| `deps/OpenRGB` | `81bbe18a84c2e507006f19dd252e397e40a56bfe` |
| `deps/OpenRGBEffectsPlugin` | `0e0f1b4708c302bad4270ab62349b2efe5dcc439` |

## 安装

### 预编译包

每次推送到 `main` 分支都会在 [Releases](https://github.com/Zayrick/LightTrackPlugin/releases) 页面自动发布预发布版本：

| 平台 | 安装包 |
| --- | --- |
| Windows x64 | `LightTrackPlugin-Windows-x64.zip`（包含 `LightTrackPlugin.dll`） |
| Linux x64 | `LightTrackPlugin-Linux-x64.tar.gz`（包含 `libLightTrackPlugin.so`） |

> [!NOTE]
> 预发布版本由 CI 自动生成，可能不够稳定。

### 安装插件

1. 下载并解压对应平台的安装包。
2. 在 OpenRGB 中打开 **Settings → Plugins**，点击 **Install Plugin**，选择解压得到的库文件。
   也可以手动将库文件复制到 OpenRGB 插件目录：
   - Windows：`%APPDATA%\OpenRGB\plugins`
   - Linux：`~/.config/OpenRGB/plugins`
3. 重启 OpenRGB，界面中将出现 **LightTrack** 标签页。

## 使用说明

### 快捷键

| 快捷键 | 功能 |
| --- | --- |
| <kbd>Space</kbd> | 播放 / 暂停 |
| <kbd>Ctrl</kbd>+<kbd>N</kbd> | 新建布局 |
| <kbd>Ctrl</kbd>+<kbd>O</kbd> | 打开布局 |
| <kbd>Ctrl</kbd>+<kbd>S</kbd> | 保存布局 |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> | 撤销 |
| <kbd>Ctrl</kbd>+<kbd>Y</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | 重做 |
| <kbd>Ctrl</kbd>+<kbd>C</kbd> / <kbd>Ctrl</kbd>+<kbd>V</kbd> | 复制 / 粘贴所选片段 |
| <kbd>Delete</kbd> | 删除所选片段 |
| <kbd>Ctrl</kbd>+<kbd>G</kbd> | 编组所选片段 |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>G</kbd> | 取消编组 |
| <kbd>Esc</kbd> | 取消当前拖动 |

### 选择与编辑片段

- **框选**：在时间线空白处按住左键拖出矩形，选中框内或与框相交的所有片段；按住 <kbd>Ctrl</kbd> 框选可追加选择。
- **切换选择**：按住 <kbd>Ctrl</kbd> 点击片段可追加或取消选择；点击空白处清除选择。
- **整组移动**：拖动任一已选片段的主体即可移动整个选区。各片段的时长、相对时间间距和轨道间距保持不变，整组统一吸附，并受时间零点和轨道边界限制。拖动过程中按 <kbd>Esc</kbd> 可取消并恢复原位。每次拖动对应一步撤销。
- **调整时长**：拖动片段两端的手柄可调整单个片段的时长。
- **复制粘贴**：使用快捷键或右键菜单中的 **Copy** / **Paste**。粘贴时保留各片段的轨道、时长、灯效设置和相对间距，必要时自动延长时间线。

### 布局

- **保存**：点击工具栏 **Save** 或按 <kbd>Ctrl</kbd>+<kbd>S</kbd>。布局保存为 `.lighttrack` 文件，包含音乐、所有灯效片段，以及所有设备/zone/segment 的名称和开灯/禁用状态（包括没有片段的行）。
- **加载**：兼容旧版布局文件；若旧文件中没有区域配置，则保留当前的区域配置。
- **新建**：点击 **New** 或按 <kbd>Ctrl</kbd>+<kbd>N</kbd>。若当前布局有未保存的更改，会提示保存、放弃或取消。新建操作会停止播放、清空音乐和片段、恢复默认名称和开灯/禁用状态，并清空撤销历史。

> [!NOTE]
> 加载 OpenRGB 宿主配置文件（Profile）时会暂停 LightTrack 播放。目前 LightTrack 不会将布局数据写入 OpenRGB 配置文件，也不提供 SDK 命令。

### 播放行为

每个片段通过 OpenRGB 宿主 API 创建独立的虚拟控制器进行渲染，随后按轨道优先级合成，并以设备为单位一次性提交整帧。

为避免与 Visual Map 等同样驱动这些灯珠的插件互相争抢而产生闪烁：

- 只写入时间线上至少有一个片段的设备。
- 空闲区域仅在变为空闲时（开始播放、片段结束、停止播放）补发一次黑色，之后不再覆盖。
- 空闲判断以最小输出单元为准（zone 有 segment 时按 segment，否则按 zone），活动区域的灯效与开灯/禁用状态不受影响。

## 从源码构建

### 环境要求

- 支持 C++17 的编译器（Windows 推荐 MSVC 2022）
- CMake 3.16 及以上（使用 `CMakePresets.json` 需 3.21 及以上）
- Qt 5.10+ 或 Qt 6，需包含以下模块：
  - Qt 5：Core、Gui、Widgets、OpenGL
  - Qt 6：Core、Gui、Widgets、OpenGL、OpenGLWidgets、Core5Compat

在 Debian/Ubuntu 上可通过以下命令安装 Qt 6 构建依赖：

```sh
sudo apt-get install ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools libqt6core5compat6-dev libqt6opengl6-dev \
  libgl-dev libvulkan-dev libxkbcommon-dev libopenal-dev libpipewire-0.3-dev
```

### 获取源码

OpenRGB、OpenRGBEffectsPlugin 和 [miniaudio](https://github.com/mackron/miniaudio) 以 Git 子模块形式引入。克隆时请同时拉取子模块：

```sh
git clone --recurse-submodules https://github.com/Zayrick/LightTrackPlugin.git
cd LightTrackPlugin
```

如果已克隆但未初始化子模块：

```sh
git submodule update --init --recursive
```

### 使用 CMake Preset 构建

可用的 Preset 如下：

| Preset | Qt | 构建类型 | 默认 `CMAKE_PREFIX_PATH` |
| --- | --- | --- | --- |
| `qt6-debug` | 6 | Debug | `C:/Qt/6.8.3/msvc2022_64` |
| `qt6-release` | 6 | Release | `C:/Qt/6.8.3/msvc2022_64` |
| `qt5-debug` | 5 | Debug | `C:/Qt/5.15.2/msvc2019_64` |
| `qt5-release` | 5 | Release | `C:/Qt/5.15.2/msvc2019_64` |

```sh
cmake --preset qt6-release
cmake --build --preset qt6-release
```

若 Qt 安装在其他位置，可在配置时覆盖路径：

```sh
cmake --preset qt6-release -D CMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64
cmake --build --preset qt6-release
```

构建产物位于 `build/<preset 名称>/`。

### 不使用 Preset 构建

```sh
cmake -S . -B build/qt6 -D CMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64
cmake --build build/qt6 --config Release
```

Qt 5 构建方式相同，只需将 `CMAKE_PREFIX_PATH` 指向 Qt 5 安装目录。在 Linux 上使用系统 Qt 包时，通常可省略 `CMAKE_PREFIX_PATH`。

## 运行测试

测试默认关闭，可通过 `BUILD_TESTING=ON` 开启：

```sh
cmake --preset qt6-release -D BUILD_TESTING=ON
cmake --build --preset qt6-release
ctest --test-dir build/qt6-release -C Release --output-on-failure
```

在无图形界面的环境中运行时，请先设置 `QT_QPA_PLATFORM=offscreen`。

测试覆盖范围：

- 布局持久化，包括设备/zone/segment 名称和开灯/禁用状态的保存、恢复及撤销重做
- 编辑历史
- 时间线复制粘贴、拖动与波形显示
- 重叠灯效与矩阵 segment 路由
- 通过插件 API 5 加载实际构建的插件库、设备重扫通知及反复卸载

灯效测试使用模拟控制器，不访问真实 RGB 硬件，也不播放音频。

## 项目结构

```text
src/
├── plugin/           OpenRGB 插件入口
├── core/             时间线与布局数据模型
├── ui/               页面及时间线控件
├── infrastructure/   布局文件读写
└── integrations/     OpenRGB 与音频实现
cmake/                OpenRGB Effects 源码配置
tests/                回归测试
resource/             Qt 资源文件引用的静态资源
deps/                 第三方 Git 子模块（请勿修改）
```

## 参与贡献

欢迎提交贡献，请注意以下约定：

- **不要修改 `deps/` 中的第三方代码。** 升级依赖时，请更新子模块提交，并在单独的变更中验证兼容性。
- **不要提交生成文件。** 所有构建产物统一放在 `build/` 目录。
- **新增或删除源文件时同步更新 `CMakeLists.txt`。**
- 提交 Pull Request 前请先运行测试。
- 提交信息遵循 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/) 规范（如 `feat(timeline): ...`、`fix: ...`）。

## 许可证

本项目与 OpenRGB 相同，采用 [GNU 通用公共许可证第 2 版或更高版本（GPL-2.0-or-later）](LICENSE) 授权。

`deps/` 中的第三方子模块遵循各自的许可证。
