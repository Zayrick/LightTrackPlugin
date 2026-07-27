# LightTrackPlugin 架构

## 目标

LightTrackPlugin 使用分层结构隔离 OpenRGB 插件生命周期、时间线模型、Qt 界面、本地基础设施和第三方库。分层的目的不是增加包装层，而是让每项变化只影响一个明确边界：

- 更换或新增效果提供者时，不修改时间线编辑器。
- 调整持久化格式时，不修改图形项和硬件控制。
- 替换音频后端时，不把第三方音频类型传播到 UI。
- OpenRGB 设备模型变化时，由集成层吸收变化。

## 分层与依赖方向

```text
                         ┌──────────────> core
plugin ──装配──> ui ────┤
   │                    └──────────────> application ports ──> core
   ├────────> infrastructure ──────────> application ports
   └────────> integrations ────────────> application ports / core
                         └──────────────> third-party dependencies
```

所有跨层依赖都朝向稳定的数据和契约。反向依赖不允许出现：`core` 不依赖其他项目层，`application` 不依赖具体适配器，`ui` 不依赖硬件或文件实现，`infrastructure` 和 `integrations` 不依赖界面。plugin 是唯一知道所有具体实现并负责装配它们的组合根。

### plugin

`plugin` 是 OpenRGB 加载 LightTrack 的入口，也是应用对象的组合根。它负责：

- 实现 OpenRGB 插件元数据和 Load、Unload、GetWidget 等生命周期。
- 创建并连接 UI、基础设施服务和第三方集成适配器。
- 将 OpenRGB 回调安全地转交给 Qt 事件循环。
- 按正确顺序释放设备回调、运行时效果、音频和界面对象。

plugin 层不实现时间线编辑、布局序列化、音频解码或具体硬件策略。它只负责装配和生命周期。

### core

`core` 保存稳定、可按值传递的模型和与框架无关的规则，包括：

- `ClipId`
- `TimelineLane`
- `EffectDescriptor`
- `EffectGroup`
- `TimelineClip`
- 快照历史等不涉及 I/O 的算法

core 类型不得包含 `QWidget`、`QGraphicsItem`、OpenRGB 控制器指针、miniaudio 句柄或序列化库对象。允许使用 Qt 的轻量值类型时，也不能引入 Qt Widgets 或硬件行为。

跨层只保留一份语义相同的模型。不要为 UI、持久化和 OpenRGB 分别复制一组字段相同的 Clip 或 Effect 结构；需要适配时，在边界处进行显式转换。

### application

`application` 定义 UI 可以调用、plugin 可以注入的稳定端口：

- `TimelineBackend`：效果目录、设备 lane、效果设置和播放运行时。
- `AudioService`：打开、播放、暂停、定位、时长和波形采样。
- `LayoutRepository`：以 `LayoutSnapshot` 读写布局。

端口只暴露项目拥有的 DTO、Qt 值类型和必要的通用控件容器，不包含 `RGBController`、`RGBEffect`、`ma_*` 或 `nlohmann::json`。新增后端实现应实现现有端口；如果需求是新的独立能力，则新增小而专一的端口，不在 UI 中增加供应商判断分支。

### ui

`ui` 包含 Qt 页面、时间线 facade、图形项和纯展示组件。`LightTrackPage` 同时承担页面级交互编排，但所有外部动作都经 application 端口完成。UI 负责：

- 展示 core DTO。
- 将拖放、选择、缩放、播放和编辑操作转换为回调或命令意图。
- 管理只与绘制和交互有关的临时状态。
- 展示上层传入的错误和运行状态。

`TimelineEditor` 的公开边界只能使用 core DTO 和 `ClipId`：

- 输入使用 `TimelineLane`、`EffectGroup` 和波形值。
- 输出使用 `TimelineClip`、`ClipId` 及无硬件语义的回调。
- “没有选中 Clip”使用 `std::optional<ClipId>` 表达。
- QGraphicsItem 指针只能作为编辑器私有实现细节，不能作为稳定标识或跨层参数。

UI 不持有硬件策略。`TimelineLane` 只描述时间线需要的 id、名称和层级；它不能携带 `RGBController*`、`ControllerZone*` 或效果运行时对象。UI 发出“播放”“定位”“删除 Clip”等意图，注入的 `TimelineBackend` 负责设备解析、区域分配和效果实例化。

时间轴内部继续按职责隔离：公开 `TimelineEditor` 是稳定 facade，`internal` 下的 common、graphics items、scene、panels 和 view 是可独立维护的私有实现。内部图元类型不能重新进入公开头。

### infrastructure

`infrastructure` 实现项目拥有的本地技术能力，例如：

- 布局文件读写、格式版本兼容和错误转换。
- 与具体界面无关的文件系统操作。
- 项目级服务的资源生命周期和通用实现。

基础设施 API 实现 application 端口，接收和返回稳定的项目类型，不弹出对话框，也不访问时间线控件。文件选择和错误展示属于 UI；实际读写、原子保存和格式检查属于 infrastructure。布局 JSON 库只存在于 `LayoutFileRepository.cpp`，公共头只暴露 `LayoutSnapshot`。

硬件、音频等供应商运行时 SDK 应由 integrations 适配；文件格式使用的实现库可以留在 infrastructure 的私有 `.cpp` 中，但不能出现在端口、DTO 或公共头。

### integrations

`integrations` 是所有外部系统和第三方库的隔离边界，包括：

- OpenRGB 资源管理器、设备、控制器和区域。
- OpenRGBEffectsPlugin 的效果目录、设置页和运行时实例。
- miniaudio 的解码器、引擎和声音句柄。
- Qt 5 与 Qt 6 之间需要隔离的第三方兼容处理。

integrations 将第三方类型转换为 core DTO 或项目契约，第三方对象不得越过这一层进入 UI。硬件区域匹配、效果实例创建、设备变化处理等策略也属于此层。

第三方源码只在对应的 CMake 目标或构建清单中编译。`deps/` 保持为子模块，core、application 和 ui 的目标不得直接包含第三方源文件、头文件目录或编译定义。对外暴露的头文件不能出现 `RGBController`、`RGBEffect`、`ma_*`、`nlohmann::json` 或其他供应商类型。

## 构建目标

源码层和 CMake 目标一一对应：

| 目标 | 类型 | 职责 |
| --- | --- | --- |
| `lighttrack_core` | INTERFACE | core DTO 和头文件规则 |
| `lighttrack_application` | INTERFACE | application 端口 |
| `lighttrack_timeline` | STATIC | 时间轴 facade 与私有交互实现 |
| `lighttrack_page` | STATIC | 页面交互编排 |
| `lighttrack_audio` | STATIC | miniaudio 播放及波形适配，唯一的 `miniaudio.c` 实现 |
| `lighttrack_persistence` | STATIC | 布局文件与 JSON 编解码 |
| `lighttrack_openrgb` | OBJECT | OpenRGB 适配及全部 Effects 静态注册器 |
| `LightTrackPlugin` | SHARED | 宿主 ABI、组合根和 qrc |

OpenRGB Effects 使用 OBJECT 而不是普通 STATIC 库，因为各效果通过翻译单元内的静态构造器注册；普通静态归档可能裁掉未直接引用的效果对象。插件 qrc 留在最终 SHARED，避免只通过资源路径访问时被链接器裁剪。

## 关键边界

### Clip 身份

`ClipId` 是 Clip 在编辑、撤销、持久化恢复和运行时映射中的稳定身份。

- `0` 表示无效 ID。
- 容器下标不是身份，排序或删除后不能依赖下标继续引用 Clip。
- 内存地址不是身份，重建场景或恢复快照后不能依赖旧指针。
- 持久化恢复可以请求原 ID；新建 Clip 由时间线分配新 ID。
- UI 内部图元与运行时对象都通过 `ClipId` 建立短期映射。

### 数据流

推荐的数据流是：

```text
                         core DTO
                            ^
                            |
ui ---- command/intent ---> application ports
 ^                          ^
 |                          |
 +------ plugin 注入 -------+------ integrations / infrastructure
                                      |
                                      v
                             OpenRGB / miniaudio / filesystem
```

持久化也遵循相同原则：页面捕获 `LayoutSnapshot`，通过注入的 `LayoutRepository` 端口保存或加载；infrastructure 实现文件 I/O 和 JSON 兼容，加载完成后只把 DTO 交回页面。plugin 只创建并连接这些对象，不参与字段解析。

## 扩展原则

### 新增效果来源

1. 在 integrations 中实现新的效果目录或运行时适配器。
2. 将外部效果描述转换为 `EffectDescriptor` 和 `EffectGroup`。
3. 使用稳定且可持久化的 effect id；显示名称不是标识。
4. 由集成层负责创建、配置和销毁真实效果实例。
5. 时间线编辑器只消费 DTO，不因新增效果类型增加分支。
6. 供应商专用设置控件留在集成边界，UI 只承载通用的设置容器和选择状态。

如果增加一个效果提供者必须修改 TimelineEditor 的绘制或拖放规则，说明提供者细节已经泄漏到 UI，应先补充抽象边界。

### 扩展持久化

1. 版本号和格式标识由持久化边界集中管理。
2. 保存内容只包含稳定 ID、时间值和可序列化配置，不保存 QWidget、图元地址或硬件指针。
3. 新格式通过 repository 中的版本迁移兼容旧布局，避免在 UI 中散布字段兼容分支。
4. 文件 I/O 返回结构化成功状态和错误信息，由 UI 决定如何展示。
5. 写入应保持原子性；解析完成并验证后再替换当前时间线。
6. 序列化库类型不能成为 core 或 UI 的公共接口。

### 扩展音频

1. 播放、定位、时长查询和波形分析使用项目自有接口。
2. UI 只接收位置、时长、波形采样值，并发出播放、暂停和 seek 意图。
3. 波形分析与实时播放分离，避免界面逻辑依赖解码器状态。
4. miniaudio 的 `ma_*` 类型只存在于 integrations 的私有实现和构建目标中。
5. 打开新文件、卸载插件和错误路径都必须完整释放音频资源。
6. 替换音频后端时，core DTO 和 TimelineEditor API 应保持不变。

## 变更检查

提交新模块前检查：

- 新依赖是否仍然指向 core，而不是由 core 反向引用外层。
- UI 是否只包含 application/core/ui 头，application 是否只依赖 core。
- TimelineEditor 的新公开 API 是否仍然只使用 DTO、值类型和 `ClipId`。
- UI 是否出现设备、区域、效果运行时或第三方音频类型。
- 相同业务结构是否在多个层重复定义。
- 第三方 include、源码和编译定义是否只出现在对应适配器的私有实现与构建边界。
- 文件、音频和硬件错误是否在边界处转换，而不是直接驱动界面。
- 新源文件、资源和私有 include 路径是否已加入正确的 CMake 目标。
- `lighttrack_architecture.boundaries` 是否通过，避免第三方依赖重新泄漏。
