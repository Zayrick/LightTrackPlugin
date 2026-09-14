# LightTrackPlugin

LightTrackPlugin 是一个基于 Qt 的 OpenRGB 插件，用时间线组织灯光效果，并提供布局持久化、音频播放与波形预览等能力。项目以 C++17 编写，同时支持 Qt 5 和 Qt 6。

当前版本面向 **OpenRGB 1.0（插件 API 5）**，两个 OpenRGB 子模块均固定到 `release_1.0`：

- `deps/OpenRGB`：`81bbe18a84c2e507006f19dd252e397e40a56bfe`
- `deps/OpenRGBEffectsPlugin`：`0e0f1b4708c302bad4270ab62349b2efe5dcc439`

插件使用宿主 API 创建独立的虚拟控制器保存各片段的颜色，再按时间线图层优先级输出。构建所用 Qt 的主版本和架构应与 OpenRGB 宿主一致。

播放期间，空闲区域会随播放同步持续补发黑色，直到暂停、停止或音乐结束。空闲判断以最小输出单元为准（有 segment 时为 segment，否则为 zone），保留活动区域的灯效与开灯/禁用状态，合并为设备帧后提交。

## 获取源码

项目通过 Git 子模块引用 OpenRGB、OpenRGBEffectsPlugin 和 miniaudio。首次克隆时应同时初始化子模块：

```sh
git clone --recurse-submodules <repository-url>
cd LightTrackPlugin
```

如果仓库已经克隆，或子模块目录为空，执行：

```sh
git submodule update --init --recursive
```

不要直接复制或修改 `deps/` 中的第三方源码。需要升级依赖时，应更新对应子模块提交，并单独检查兼容性。

## 构建要求

- 支持 C++17 的编译器
- CMake 3.16 或更高版本
- 使用 `CMakePresets.json` 时需要 CMake 3.21 或更高版本
- Qt 5.10 或更高版本，或 Qt 6；包含 Core、Gui、Widgets 和 OpenGL 组件
- Qt 6 构建还需要 Core5Compat 和 OpenGLWidgets

Windows 构建建议使用与 Qt 安装包匹配的 MSVC 工具链。

## 使用 CMake Preset 构建

仓库提供以下配置：

- `qt6-debug`
- `qt6-release`
- `qt5-debug`
- `qt5-release`

例如，构建 Qt 6 Debug 版本：

```sh
cmake --preset qt6-debug
cmake --build --preset qt6-debug
```

构建 Qt 5 Release 版本：

```sh
cmake --preset qt5-release
cmake --build --preset qt5-release
```

Preset 中的 `CMAKE_PREFIX_PATH` 是 Qt 安装目录示例。如果本机路径不同，可以在配置时覆盖：

```sh
cmake --preset qt6-debug -D CMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64
cmake --build --preset qt6-debug
```

## 不使用 Preset 构建

也可以直接指定源码目录、构建目录和 Qt 路径：

```sh
cmake -S . -B build/qt6 -D CMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64
cmake --build build/qt6 --config Debug
```

Qt 5 的构建方式相同，只需将 `CMAKE_PREFIX_PATH` 指向对应的 Qt 5 安装目录。生成的插件库位于所选构建目录下，具体子目录由 CMake 生成器和构建配置决定。

## 源码目录

```text
src/
  plugin/          OpenRGB 插件入口
  core/            时间线和布局数据
  ui/              页面及时间线控件
  infrastructure/  布局文件读写
  integrations/    OpenRGB 和音频实现
cmake/             OpenRGB Effects 源码配置
tests/             功能回归测试
deps/              Git 子模块，不承载项目业务代码
resource/          Qt 资源文件使用的静态资源
```

## 开发约定

- 构建产物统一放入 `build/`，不要提交生成文件。
- 新增或删除源文件时同步更新 `CMakeLists.txt`。

测试默认不参与普通构建。需要运行时显式开启：

```sh
cmake --preset qt5-release -D BUILD_TESTING=ON
cmake --build --preset qt5-release
ctest --test-dir build/qt5-release -C Release --output-on-failure
```

测试覆盖布局持久化、设备/zone/segment 名称及开灯/禁用状态的保存恢复与撤销重做、历史记录、时间线复制粘贴、重叠灯效与矩阵分段路由，以及实际插件 DLL 的 API 5 加载、设备重扫通知和反复卸载。灯效测试使用模拟控制器，不访问真实 RGB 硬件或播放音频。

布局通过工具栏保存按钮或 Ctrl+S 写入布局文件，包含音乐、灯效片段，以及所有设备/zone/segment 的名称和开灯/禁用状态（包括没有灯效片段的行）。旧版布局文件仍可加载；没有区域配置的旧文件会保留当前区域配置。加载 OpenRGB 宿主配置时会暂停 LightTrack 播放，暂不向宿主配置写入布局数据，也不提供插件 SDK 命令。

点击工具栏 New 按钮或按 Ctrl+N 新建空白布局。如果当前布局从未保存，或内容与上次保存/加载时不同，会提示保存、放弃更改或取消；取消保存或保存失败时保留当前布局。新建会停止播放、清空音乐和灯效片段、恢复设备/zone/segment 的默认名称和开灯/禁用状态，并清除当前文件路径及撤销/重做历史。
