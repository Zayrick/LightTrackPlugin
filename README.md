# LightTrackPlugin

LightTrackPlugin 是一个基于 Qt 的 OpenRGB 插件，用时间线组织灯光效果，并提供布局持久化、音频播放与波形预览等能力。项目以 C++17 编写，同时支持 Qt 5 和 Qt 6。

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
