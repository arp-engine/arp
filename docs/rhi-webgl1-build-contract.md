# WebGL1ProductionBuild/1

状态：Contract Complete，仅指 P0-19 的构建选择与依赖隔离设计。CMake 选择矩阵、实际 Emscripten root configure 的预期拒绝和 MinGW/Null 回归已通过；不提供生产 WebGL backend。实际 target、命令、依赖闭包和运行证据由 P2-13、P4 验收。

## 当前构建事实

- 根 CMake 的 Emscripten platform 路径链接 `arp_rhi_webgpu`，编译 `render_system_webgpu.cpp`；WebGPU target 传递 `--use-port=emdawnwebgpu` 和 `-lembind`。
- `web-webgpu-debug/release` 使用各自目录；Windows 使用 MinGW。实际 presets 还包含 Metal 平台，不能按“固定四项”枚举重写整个文件。
- `scripts/build.py webgl1-probe` 是独立的 ProbeOnly builder，直接编译 GLES2 fixture，使用 MIN/MAX_WEBGL_VERSION=1；不是 `arp_engine` 或生产 ShaderPackage。
- 当前生产 `arp_rhi_webgl`、WebGL RenderSystem 与 GLSL ES100 package 尚不存在。选择 WebGL1 必须失败，不能继续编译 WebGPU 后输出 WebGL 名称。

## 选择和诊断

根 CMake 增加 `ARP_WEB_RHI` cache string，由 `cmake/ArpWebRhiSelection.cmake` 的单一选择器解析。允许的显式值是 `none`、`webgpu`、`webgl1`，区分大小写。空值只保留既有构建路由：Emscripten 解析为 webgpu，其余系统为 none。这是编译产品选择，不是 GPU capability 查询。

| system | requested | 当前结果 |
| --- | --- | --- |
| Emscripten | 空或 webgpu | 选择既有 WebGPU 产品，不承诺运行时可用 |
| Emscripten | webgl1 | `RHI-WEBGL1-BUILD-UNAVAILABLE`，停止 configure |
| Emscripten | none | `RHI-WEB-BUILD-SELECTION-REQUIRED` |
| 非 Emscripten | 空或 none | 保留既有 native platform 路由；host WebGPU contract library 不受影响 |
| 非 Emscripten | webgpu 或 webgl1 | `RHI-WEB-BUILD-PLATFORM` |
| 任意 | 其它值 | `RHI-WEB-BUILD-SELECTION-UNKNOWN`；优先于 platform/availability 判断 |

诊断必须在 backend/platform target 配置和外部 GPU dependency discovery 之前发生。不得接受 `ARP_ENABLE_WEBGL=ON` 等未定义变量后仍输出 WebGPU 产品。CMake 对其它未知变量的普通 unused-variable warning 不构成 backend 选择入口。

该顺序同时约束版本化的根CMake和根目录`build.py --regenerate-cmake`使用的`CmakeWriter`输出：重新生成必须保留同一个`arp_select_web_rhi`入口，不得另写名称解析或丢失WebGL1拒绝门禁。WebGPU的Emscripten bridge编译/link选项以及platform的WebGPU依赖/source选择必须消费`ARP_SELECTED_WEB_RHI`，不能仅由目标OS再次推导产品身份。Null不对应Web构建产品；Vulkan/Metal的既有native选择保持不变。生成器回归只证明构建选择，不提供native GPU证据；其它生成器/版本化CMake差异仍须单独审查。

根目录旧入口 `build.py configure` 的 `--backend` 是构建产品意图，也必须在工具探测、module discovery、工程/preset 写入和 subprocess 之前验证。`BuildBackendSelection/1` 只接受六个平台及其现有产品：win/linux/android→vulkan，mac/ios→metal，web→webgpu；`auto` 只解析一次为该明确默认产品，不能随后被原字符串覆盖。这不是 native capability 或目标平台执行证明。

| CLI 输入 | 确定结果 |
| --- | --- |
| 未知 platform | `RHI-BUILD-PLATFORM-UNKNOWN` |
| 未知/大小写错误/路径形式 backend | `RHI-BUILD-BACKEND-UNKNOWN` |
| native platform 请求 webgpu/webgl1 | `RHI-WEB-BUILD-PLATFORM` |
| web 请求 webgl1 | `RHI-WEBGL1-BUILD-UNAVAILABLE`，不生成生产 preset |
| 其它与平台不匹配的已知产品 | `RHI-BUILD-BACKEND-PLATFORM` |
| auto 或该平台已登记产品 | 将同一解析结果传给 build directory、writer、preset generator 和 configure command |

Python 入口验证声明的构建组合；实际 CMake system/product 仍由上述唯一 `arp_select_web_rhi` 判定。两者的拒绝矩阵必须有测试关联，不能以 CLI 检查代替真实 toolchain 的 CMake gate。无效请求（含 `--regenerate-cmake`）不得改变根 CMake、presets 或创建构建目录。公开 API 直接构造 CmakeWriter 或生成 presets 时同样先验证声明产品，不能输出与声明 backend 不符的工程身份。该契约不新增 WebGL 生产命令。

### BuildPresetSelection/1：根CLI与生成矩阵一致

根`build.py`的configure/build/test必须消费同一显式preset身份，不能跨进程依赖configure留下的Python globals。六个产品stem为win-vulkan、web-webgpu、mac-metal、ios-metal、linux-vulkan、android-arm64-vulkan；debug/release分别对应`<stem>-debug/release`、独立`build/<name>`和准确的Debug/Release配置。iOS的Xcode build/test preset还必须显式携带configuration；不把CMAKE_BUILD_TYPE当multi-config选择。

保留macos-metal、ios-metal、linux-vulkan、android-arm64-vulkan旧稳定入口及已有四个Windows/Web preset；生成器仅新增缺失的六个配置入口，不把Release偷偷路由到旧Debug目录。原版本不明确的package配置不推测为Release/MinSizeRel：在任何探测/读写/子进程前返回`RHI-BUILD-CONFIG-UNAVAILABLE`；其它未知或大小写错误配置返回`RHI-BUILD-CONFIG-UNKNOWN`。backend/platform身份校验仍优先，WebGL1仍BUILD-UNAVAILABLE。

同一选择器约束root三个命令、CmakeWriter及两个preset API，错误输入不得写文件或创建目录；build/test新增显式platform/backend参数，默认保持win/auto。工具测试须验证六平台×两配置的三类preset引用、binaryDir、cache/configuration、旧入口保留、前置拒绝及真实CMake preset解析。configure命令传递测试可替换compiler/native调用，但不能据此宣称任意目标平台已构建。真实host仍保留MinGW/Null smoke。当前既有版本化CMakePresets不被本批生成器测试覆盖。

`scripts/build.py` 当前仍使用原 preset 路由；本阶段不发布伪生产命令。独立 probe 继续使用 `webgl1-probe`。生产 CLI 的确定目标如下，只有 P2 target 和隔离门槛通过后才能登记：

```text
python scripts/build.py webgl1 configure --config Debug|Release --toolchain <explicit Emscripten toolchain>
python scripts/build.py webgl1 engine --config Debug|Release
python scripts/build.py webgl1 smoke --config Debug|Release
```

这些命令当前尚不存在。它们必须消费同一个 WebGL1 构建产品身份，固定目录为 `build/web-webgl1-debug` / `build/web-webgl1-release`，配置 `ARP_WEB_RHI=webgl1`；不得将 `web-webgpu-*` 的 binaryDir 或 cache 改成 WebGL1。保留 `scripts/build.py --preset <existing> ...` 的参数与行为。需要其它输出目录时必须显式声明并校验缓存中的 toolchain/backend/config/source identity；不复用不匹配缓存，不自动删除旧缓存。

## CMakePresetDocument/1：前置校验与受控合并

本契约只管理构建工具的文档边界，不授权任何GPU执行。当前仓库与生成器使用CMake presets schema 6；只接受明确的整数version=6，不猜测旧版迁移或未来版本语义。不存在的文件允许创建schema6文档；已存在但不可读、非UTF-8、非法JSON、重复key（含嵌套）、非有限数、错误根类型或缺失/不支持的版本均拒绝，保留原文件字节。读取错误不能解释为“新文档”。

两个preset生成API复用同一文档对象。configure/build/test/package/workflow类别若存在，必须为具备非空字符串name的对象数组，同类别名称唯一；include必须为非空路径字符串数组，vendor为对象，cmakeMinimumRequired为只含major/minor/patch非负整数的对象。该检查是明确的envelope校验，不等于完整CMake schema、include依赖图或目标平台验证；其余字段原样保留，不猜测其执行语义。真实CMake解析作为独立测试证据。

生成器只替换自己声明管理的configure/build/test名称；保留其余条目、include、vendor、cmakeMinimumRequired、package/workflow以及其它根字段。single API只管理本次生成的一个名称，不清空其它平台矩阵；matrix API管理现有稳定名称集合。受管理条目的完整内容由生成器负责，不隐式继承旧条目中的toolchain/cache覆盖；相关toolchain/cache身份审计仍独立open。

root configure在backend/platform/config校验之后、工具探测/module discovery/CMake写入/建目录/subprocess之前预检文档；非法文档不得使--regenerate-cmake覆盖根工程。选择错误仍先于文档错误。直接生成API也在探测工具前读取并校验。CLI输出稳定诊断并exit2，不输出Traceback。

输出先完整序列化，再写同目录临时文件并原子replace；写入或replace失败时保留原目标并清理本次临时文件。发布前核对读取快照，检测到目标改变或意外出现时拒绝，不能覆盖已知的新输入。此检查不声称多进程文件事务或全工程两文件原子性；不支持并发生成同一路径。

稳定诊断：RHI-BUILD-PRESETS-IO、ENCODING、JSON、DUPLICATE-KEY、SHAPE、VERSION、CHANGED、WRITE（均带RHI-BUILD-PRESETS前缀）。拒绝及replace故障测试须核对原始字节；合法合并须由真实cmake --list-presets=all解析，使用include、vendor、package和workflow等根字段验证保留。每批同时保留MinGW/Null smoke，不以工具测试提升WebGL Backend/Platform readiness。

## CMakeBuildCacheAdmission/1：根CLI的缓存身份

本增量覆盖根build.py的已登记产品，不替代CMake解析器、交叉编译SDK验证或GPU capability来源认证。scripts/build.py及其它入口仍单独审计；不能由本项宣称全部toolchain/cache问题关闭。

缓存由CMakeCacheDocument/1按CMake文本记录读取：忽略空行与#、//注释，解析KEY:TYPE=VALUE，保留value原文，拒绝不可读/非UTF8/NUL/非法记录/重复key。不存在的cache只允许configure新建；build/test须先configure。拒绝不得创建目录、删除cache或调用构建/测试子进程。

configure按产品/config/document优先级验证后，在工具探测前核对已存在cache的CMAKE_HOME_DIRECTORY、CMAKE_CACHEFILE_DIR；工具探测后由现有preset matrix的同一个plan输出构建intent，在module discovery及工程/preset写入前完成其余检查。不得另写一套compiler/toolchain默认值。build/test直接消费所选根document的flat configure preset；缺失/inherits/未解析宏等本增量不支持的语义明确拒绝，不推测include或继承关系。

intent比较generator、源码/输出目录、configuration、显式compiler及toolchain路径和ARP_WEB_RHI。单配置比较CMAKE_BUILD_TYPE；多配置比较实际CMAKE_CONFIGURATION_TYPES包含请求项，不能用build type代替。root test必须把同一configuration传给ctest，避免Xcode选择默认配置。typed cache variable支持明确的type/value对象；路径只展开sourceDir/sourceParentDir/presetName以及已知实际env/penv值，未知/缺失宏拒绝。相对binaryDir按CMake规则相对于source root；toolchain要求展开后的绝对路径。bare compiler使用实际PATH查询；preset覆盖PATH而无法确定该查询环境时拒绝。不能凭compiler文件名推断其target或ABI。

document accessor和intent各自拥有所选preset的深拷贝；调用方随后修改原始嵌套字段不得改变已经建立的intent。typed identity的type不是受支持的字符串时返回INTENT，不泄漏Python TypeError。

Web产品none/webgpu来自既有BuildBackendSelection及ArpWebRhiSelection合同；cache必须包含ARP_WEB_RHI，空值只按该合同的既有默认路由比较。webgpu的toolchain路径必须与显式preset一致；native请求不得复用残留toolchain。WebGL1仍在更早的产品选择阶段拒绝，不发布生产支持。声明路径及cache可编辑，匹配不认证compiler二进制/工具链内容、实际target triple、所有影响构建的cache变量或传递依赖；后续仍需实际CMake/native平台及P2依赖闭包证据。

稳定诊断统一RHI-BUILD-CACHE-前缀：MISSING、IO、ENCODING、SHAPE、DUPLICATE、SOURCE、DIRECTORY、GENERATOR、CONFIG、COMPILER、TOOLCHAIN、BACKEND、INTENT、UNRESOLVED。源/目录错误须先于工具探测；其余错误先于module discovery/写文件/subprocess；CLI exit2且不输出Traceback。不自动修复或迁移不匹配cache。允许用户显式使用新的独立目录，不能通过清空原cache绕过。

测试须覆盖错源码、移动cache、错generator/config、单双配置、路径别名、compiler/toolchain不符、Web产品不符、缺失/坏文档、继承/未知宏拒绝、真实CLI无副作用，以及真实CMake生成cache的接受/拒绝。每批仍保留MinGW/Null smoke；元数据fixture不构成任何native graphics或跨平台成功证明。

## CMakePresetBuildAdmission/1：scripts/build.py入口

scripts/build.py的configure、engine/editor/game/shaders/all和test复用同一cache admission。所有CMake执行、CTest查询/执行和target-help查询必须在子进程前验证所选根flat preset及实际cache；只有configure允许cache不存在。独立webgl1-probe保持ProbeOnly，不消费生产preset或新增生产WebGL权限。原参数形式与target registry保留。

已登记preset产品来自BuildPresetSelection的完整名称表和明确旧别名表，不根据名称前后缀猜测。自定义preset必须显式声明ARP_WEB_RHI=none/webgpu；none是非Web构建身份，不代表Null/Vulkan/Metal任一运行时能力。显式产品必须与cache的显式值逐字一致，不能复用空值默认路由来认证自定义产品；只有无显式产品的已登记旧入口保留原默认路由比较。编辑器CLI及Web/native smoke目标同样消费该产品声明，不再根据web-名称前缀选目标。注册名称与显式产品冲突拒绝；webgl1仍走既有BUILD-UNAVAILABLE。该metadata不能证明toolchain实际target。

configuration优先取显式--config，再取operation preset的configuration，再取单配置configure preset的CMAKE_BUILD_TYPE；只支持Debug/Release，未知preset不默认为Debug。multi-config必须有显式/operation configuration，不能用CMAKE_BUILD_TYPE替代；已有cache的CMAKE_CONFIGURATION_TYPES是校验来源。新configure尚无cache时只允许从已登记的单配置generator Ninja/Unix Makefiles/MinGW Makefiles读取build type，其它generator要求显式configuration。新单配置configure必须声明与请求一致的build type，缺失时拒绝，不能由--config的Python值冒充传给CMake的配置；已有cache的build仍可使用显式请求加真实cache类型。显式请求仍须通过cache/configure意图一致性检查。身份enum字段要求明确字面值，不解析宏猜测产品或配置。

operation必须显式引用一个根flat configure preset；已登记operation不可重定向到另一个configure身份，自定义operation可显式引用既有configure；test路径的build/test必须指向同一configure。所有test查询和执行传同一-C，所有build/help传同一--config。缺preset、继承、redirect、产品/配置冲突和缺失/不匹配cache均稳定拒绝，CLI exit2无Traceback且不触发构建。新configure无cache也必须校验声明compiler/toolchain字段的类型和宏、toolchain绝对路径以及已登记单配置generator的build type一致性；不存在的cache不能绕过这些检查，但不在这一步认证compiler二进制。此契约不解析include/CMakeUserPresets/继承图，不认证compiler/toolchain内容或ABI，也不改写共享presets。

测试包含真实CLI拒绝与原文件字节保护、每个子进程入口的负向gate，以及真实CMake单/多配置项目的configure→build→CTest查询→执行。仅metadata项目成功不构成native graphics证据；每批另保留MinGW contract与Null hidden-frame。

## P2 依赖与 target 交付契约

1. `arp_rhi_webgl` 仅依赖中立 RHI/frontend/semantics、core、shader package 与 Emscripten GLES2 bridge。`arp_platform` 选择 `render_system_webgl.cpp`，不能同时定义第二个 RenderSystem factory。
2. WebGL1 engine/smoke 的 transitive link closure 不得包含 WebGPU target、emdawnwebgpu、Dawn pre-js/import、navigator.gpu startup；同一 superbuild 中无关 host/test target 存在不等于污染，但不能成为 WebGL1 默认构建依赖。
3. WebGPU 的 browser-only fixture/default ALL target 也必须按产品显式隔离，不能只从 `arp_platform` 删一个 link 后宣称完成。Vulkan/Metal host contract target 的状态保持独立。
4. MIN_WEBGL_VERSION=1 和 MAX_WEBGL_VERSION=1 必须作用于最终 JS/Wasm link；context 创建显式 major=1/minor=0，扩展不默认启用；实际 VERSION 查询再验证 WebGL1。禁止缺失时创建 WebGL2。
5. Shader 产物继续由 Slang 生成 SPIR-V/MSL/WGSL/reflection，增加生产 ES100 ABI；不能把 ProbeOnly 文件重命名成生产 package。构建选择不授予 shader/pipeline execution 权限。
6. 生产 context attributes、内存/帧预算、所有权、线程、canvas lease 与 loss generation 来自相应 L2 contract；不得由 CMake 默认值创建隐式运行时策略。
7. 缺 target/tool/artifact、依赖污染、错误 context 版本、构建期间输入变化都返回失败并保留日志。不得以成功创建空 `.js`/`.wasm` 或 host stub 代替产物。

## 验证边界

P0 快速测试通过真实 `cmake -P` 运行选择器的输入矩阵，检查结果/稳定诊断；这是产品选择测试，不是 Emscripten 编译或 native execution。已有独立 GLES2 probe 的真实构建与浏览器 smoke 只证明执行桥可行性。每批仍保留 Null hidden-frame。

P2 必须另取：实际 Emscripten root configure、target/link dependency 检查、最终 JS/Wasm 和 shader hash、关闭 WebGPU 后的真实 WebGL1 browser smoke、RenderDoc 捕获与 AI 检查。Debug/Release 分别验证；旧 WebGPU preset 回归及原四 backend contract 同时保留。该证据不由 P0 的产品选择测试替代。

五后端结果：Null 保留 validator-only；Vulkan/Metal 保留既有 native 路由；WebGPU 保留既有 Emscripten 路由；WebGL 当前 DiagnosticFailure。此契约不改 L2 descriptor/capability，不提升 Backend/Platform/Production 状态。所有尚未实现的生产命令和依赖证明保持 open。

证据：`build/webgl1-evidence/p0/build-selection-contract/`，包含命名输入哈希、MinGW 构建和 5/5 CTest、实际 Emscripten 拒绝命令/退出码，以及前后哈希检查。CMake 矩阵使用真实 `cmake -P`，覆盖 36 组输入；不是 Emscripten graphics backend 执行证明。
