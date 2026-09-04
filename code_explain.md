# PixelCake Android — 代码结构与实现说明

> 配套文档：`plan.md`（开发计划与断点清单）、`README.md`（构建/取包/CI）
>
> 阅读顺序建议：**§2 目录结构 → §4 已实现代码 → §5 调用链 → §6 未完成伪代码 → §7 编码约定**
>
> 最后更新：2026-09-04

---

## 1. 一句话定位

Kotlin/Compose 写 UI，NDK + LibRaw 解码 Sony A7C2 的 14bit ARW，GLES 3.0 + fp16 做调色渲染，
输出 Display P3 / Ultra HDR。全程离线。

---

## 2. 目录结构（当前，as-is）

```
pixel-cake-android/
├── .gitattributes                  # 强制 gradlew 用 LF（否则 Linux runner 报 ^M）
├── .gitignore
├── .gitmodules                     # third_party/libraw → LibRaw/LibRaw.git
├── README.md
├── plan.md                         # ← 开发计划（断点续做主入口）
├── code_explain.md                 # ← 本文档
├── build.gradle.kts                # 根工程，只有插件声明
├── settings.gradle.kts
├── gradle.properties
├── gradle/
│   ├── libs.versions.toml          # 版本目录（唯一版本真源）
│   └── wrapper/
├── .github/workflows/android.yml   # build / lint / check-signing / release
│
├── third_party/
│   └── libraw/                     # gitlink @ b93f6e45 = LibRaw 0.22.2（工作区未 init）
│
└── app/
    ├── build.gradle.kts
    ├── proguard-rules.pro
    └── src/main/
        ├── AndroidManifest.xml
        ├── cpp/                              # ★ 阶段 2 新增，尚未提交
        │   ├── CMakeLists.txt                # 顶层：定位 LibRaw、链接 pixelcake_raw
        │   ├── libraw/CMakeLists.txt          # 按官方 Makefile.dist 的 79 个目标编静态库
        │   └── raw_bridge.cpp                 # JNI 桥接（6 个导出函数）
        ├── java/com/hifn/pixelcake/
        │   ├── PixelCakeApp.kt                # Application（目前是空的）
        │   ├── MainActivity.kt                # setContent + enableEdgeToEdge
        │   ├── raw/                           # ★ 阶段 2 新增
        │   │   └── RawInfo.kt                 # 元数据 data class + 扁平数组解码器
        │   └── ui/
        │       ├── home/
        │       │   ├── HomeScreen.kt           # 设备能力实测面板
        │       │   └── DeviceCapabilities.kt   # 设备探测 + fp16 内存核算
        │       └── theme/
        │           ├── Theme.kt
        │           ├── Color.kt                # Ok / Warn / Bad 三个语义色
        │           └── Type.kt
        └── res/
            ├── drawable/ic_launcher_{background,foreground}.xml
            ├── mipmap-anydpi/ic_launcher{,_round}.xml   # 注意：不是 -v26
            ├── values/{strings,themes}.xml
            └── xml/{backup_rules,data_extraction_rules}.xml
```

---

## 3. 目录结构（目标，to-be）

阶段 2 完成后应当长这样（`+` 为本次要新增，`~` 为要改）：

```
app/src/main/
├── cpp/
│   ├── CMakeLists.txt
│   ├── libraw/CMakeLists.txt
│   ├── raw_bridge.cpp                    #  JNI 边界（唯一）
+   │   ├── pixel_cake.h                   #  阶段 3 起：C++ 侧公共头
+   │   └── gl/                            #  阶段 3：EGL / FBO / 着色器工具
+   │       ├── egl_core.cpp
+   │       ├── gl_program.cpp
+   │       └── rgba16f_fbo.cpp
│
├── java/com/hifn/pixelcake/
│   ├── PixelCakeApp.kt                    # ~ 加 System.loadLibrary
│   ├── MainActivity.kt                    # ~ 加顶部 Tab 切换 + 上提 Scaffold
+   │   ├── raw/
+   │   │   ├── RawNative.kt               #  object + external 声明
+   │   │   ├── RawImage.kt                #  句柄 Closeable 封装
+   │   │   ├── RawDispatcher.kt           #  单线程调度器
+   │   │   └── RawInfo.kt                 #  ✓ 已有
+   │   ├── ui/raw/
+   │   │   ├── RawDecodeScreen.kt         #  SAF 选文件 + 结果展示
+   │   │   └── RawDecodeViewModel.kt
    │   ├── ui/home/                       #  ✓ 已有
+   │   ├── ui/gallery/                    #  阶段 P1：照片浏览网格
+   │   ├── ui/edit/                       #  阶段 F3/F4：调色与导出
+   │   │   ├── EditScreen.kt
+   │   │   └── EditViewModel.kt
+   │   ├── engine/                        #  阶段 F3：GPU 引擎 Kotlin 侧门面
+   │   │   ├── EditParams.kt              #  ★ 调色参数模型（中枢契约）
+   │   │   ├── RenderPipeline.kt
+   │   │   └── shaders/*.glsl             #  预览与导出共用
+   │   ├── data/                          #  阶段 P2：Room DB / 预设仓储
+   │   └── ai/                            #  阶段 F8：MediaPipe / ONNX
```

---

## 4. 已实现代码逐文件讲解

### 4.1 应用入口

#### `PixelCakeApp.kt`

```kotlin
class PixelCakeApp : Application() {
    override fun onCreate() {
        super.onCreate()
        // ★ 阶段 2 待补：System.loadLibrary("pixelcake_raw")
    }
}
```

目前是空的。注释里已写清后续要放什么：native 库加载、模型下载器、全局异常捕获。

#### `MainActivity.kt`

```kotlin
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()                      // 内容绘制到系统栏之下
        setContent {
            PixelCakeTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    HomeScreen()
                }
            }
        }
    }
}
```

阶段 2 要在这里加顶部 Tab（两个 `FilterChip`）在「设备能力」/「RAW 解码」间切换，
并把 `HomeScreen` 内部的 `Scaffold`/`TopAppBar` 上提到此处，避免嵌套 Scaffold。

### 4.2 设备能力面板（阶段 1 的主力产出）

#### `DeviceCapabilities.kt`

**为什么第一版做这个而不是 Hello World**：技术方案里有三条核心假设（广色域、HDR、内存够不够跑 fp16），
不实测就往下写，返工代价极大。这一步的价值就是把假设变成数据。

```kotlin
private const val A7C2_WIDTH = 7008L
private const val A7C2_HEIGHT = 4672L
private const val FP16_BYTES_PER_PIXEL = 8L        // RGBA16F = 4 通道 × 2 字节

fun Context.probeCapabilities(): DeviceCapabilities {
    val single = A7C2_WIDTH * A7C2_HEIGHT * FP16_BYTES_PER_PIXEL / MIB   // ≈ 249 MiB
    val triple = single * 3                                              // ≈ 749 MiB
    return DeviceCapabilities(
        ...
        // 三缓冲之外再留一倍冗余给 Compose UI、位图缓存与系统抖动
        fullResFeasible = availMiB >= triple * 2
    )
}
```

**一个必须记住的坑**（已经踩过一次）：

```kotlin
// ✗ 编译不过：orEmpty() 只对 Array/List/String/Map 有定义，IntArray 不适用
hdrTypes = display?.hdrCapabilities?.supportedHdrTypes?.orEmpty()

// ✓ 正确写法
hdrTypes = display?.hdrCapabilities?.supportedHdrTypes?.map(::hdrTypeName) ?: emptyList()
```

#### `HomeScreen.kt`

纯展示型 Composable。结构：`LazyColumn` + 若干 `SectionCard`，每张卡片里是 `InfoRow(label, value, color)`。
卡片：设备 / 显示与色彩 / 内存预算 / 权限 / 路线图。

`READ_MEDIA_IMAGES` 权限目前只申请不实际使用，是给阶段 P1（照片浏览）预留的。

### 4.3 主题

`Color.kt` 里定义了三个语义色 `Ok` / `Warn` / `Bad`，`HomeScreen` 用来给实测结果上色。
`Theme.kt` **刻意没有启用 Material You 动态取色**——取用户壁纸颜色会污染照片色彩判断。

### 4.4 RAW 数据层：`raw/RawInfo.kt` ✅ 已写

#### 设计取舍：为什么不直接让 JNI 构造 data class

`RawInfo` 有 35 个字段。若让 JNI 用 `NewObjectA` 构造，就得手写一条 35 个参数的
JNI 方法签名，形如：

```
(Ljava/lang/String;Ljava/lang/String;...IIIIIIII[F[F[FIFFFFJIIILjava/lang/String;I)V
```

**一个字符写错就是运行期 `NoSuchMethodError`**，而本机编译不了，只能靠 CI 试——每轮 3 分钟。

所以改成：**JNI 返回扁平数组，Kotlin 侧解码**。契约收敛成一组具名下标常量，
两侧各持一份，改动时对照一眼即可。

#### 数据契约

`nativeInfo()` 返回 `Array<Any>`，四个槽位：

| 槽 | 类型 | 内容 |
| --- | --- | --- |
| 0 | `IntArray(20)` | 定长标量：colors / filters / 尺寸 / black / maximum / rawBps … |
| 1 | `FloatArray(24)` | camMul(4) + preMul(4) + camXyz(12) + iso / shutter / aperture / focal |
| 2 | `Array<String>(8)` | make / model / normalized ×2 / software / cdesc / 解码器名 / LibRaw 版本 |
| 3 | `LongArray(1)` | timestamp |

下标常量在两侧同名同值：

```
raw_bridge.cpp : I_COLORS=0 I_FILTERS=1 ... I_COUNT=20
                 F_CAM_MUL=0 F_PRE_MUL=4 F_CAM_XYZ=8 F_ISO=20 ... F_COUNT=24
                 S_MAKE=0 ... S_VERSION=7  S_COUNT=8
                 L_TIMESTAMP=0             L_COUNT=1
RawInfo.kt     : 完全相同的 private const val
```

> ⚠️ **改任一侧都必须同步改另一侧**，否则读数会静默错位（不报错，只是数据错）。
> 这是全项目最容易出错的地方，改之前先把两份常量并排看一遍。

#### `RawInfo` 关键字段速查

| 字段 | 来源 | A7C2 期望值 |
| --- | --- | --- |
| `make` / `model` | `imgdata.idata` | `SONY` / `ILCE-7CM2` |
| `rawWidth` / `rawHeight` | `imgdata.sizes` | 7008 / 4672 |
| `width` / `height` | `imgdata.sizes` | 去马赛克后的输出尺寸（受 flip 影响） |
| `rawBps` | `imgdata.color.raw_bps` | **14** |
| `black` / `maximum` | `imgdata.color` | 黑电平 / 白电平 |
| `camMul[4]` | `imgdata.color.cam_mul` | 相机白平衡系数 R,G1,B,G2 |
| `camXyz[12]` | `imgdata.color.cam_xyz` | 相机→XYZ 矩阵，行主序 4×3 |
| `filters` | `imgdata.idata.filters` | Bayer 布局位域；X-Trans 等 > 1000 |
| `unpackFunction` | `unpack_function_name()` | 应当是 Sony 专用解码器，**不能是 `unpack_generic`** |
| `flip` | `imgdata.sizes.flip` | 方向翻转位，输出时旋转用 |

### 4.5 JNI 桥接：`cpp/raw_bridge.cpp` ✅ 已写

#### 六个导出函数

| JNI 函数 | Kotlin 对应 | 作用 |
| --- | --- | --- |
| `nativeVersion()` | `RawNative.nativeVersion()` | 返回 LibRaw 版本串 |
| `nativeOpen(buffer, length)` | → `RawImage.open()` | 建 LibRaw 实例 + `open_buffer` |
| `nativeInfo(handle)` | → `RawInfoCodec.decode()` | 读元数据，返回 4 个扁平数组 |
| `nativeEmbeddedThumbnail(handle)` | → `RawImage.embeddedThumbnail()` | 取内嵌 JPEG 预览，几十毫秒 |
| `nativeRender(handle, maxDim, halfSize)` | → `RawImage.render()` | 完整解码 + 降采样，数秒 |
| `nativeClose(handle)` | → `RawImage.close()` | 释放 LibRaw 实例 |

#### 会话结构：为什么不是裸 `LibRaw*`

```cpp
struct RawSession {
    LibRaw* lr = nullptr;
    const void* data = nullptr;   // 指向 Kotlin 的 direct ByteBuffer，不负责释放
    size_t size = 0;
    bool armed = false;           // false = imgdata 已被 recycle，需重新 open_buffer
};
```

原因有三个，每一个都很关键：

1. **`recycle()` 会清空 `imgdata`**。LibRaw 的 `recycle()` 不仅释放缓冲，还会
   `ZERO(imgdata.idata / sizes / color / other / thumbnail)`。所以渲染完之后
   再查尺寸只能拿到 0。必须记住"当前是否处于已解析状态"。
2. **`open_buffer()` 内部会先调 `recycle()`**，所以**可以重复调用**来回到干净状态——
   代价只是重新解析一遍元数据（几十毫秒）。这就是 `rearm()` 的实现依据。
3. **ByteBuffer 必须保活**。`open_buffer(data, size)` 只是记住指针，不拷贝。
   Kotlin 侧必须持有这个 direct ByteBuffer 直到句柄关闭。

```cpp
bool rearm(RawSession* s) {
    const int rc = s->lr->open_buffer(s->data, s->size);   // 内部已 recycle
    s->armed = (rc == LIBRAW_SUCCESS);
    if (!s->armed) s->lr->recycle();
    return s->armed;
}
bool ensureArmed(RawSession* s) { return s->armed || rearm(s); }
```

`nativeInfo()` 用 `ensureArmed()`（只在必要时重新打开）；
`nativeRender()` / `nativeEmbeddedThumbnail()` 用 `rearm()`（强制重开，避免 `unpack_thumb` 重复分配泄漏）。

#### `nativeRender()` 的完整流程

```cpp
rearm(session);                       // 回到干净状态

params.use_camera_wb   = 1;           // 相机白平衡（用 cam_mul）
params.use_auto_wb     = 0;
params.use_camera_matrix = 1;
params.output_color    = LIBRAW_COLORSPACE_sRGB;
params.output_bps      = 8;           // 阶段 3 会改成 16bit 线性
params.no_auto_bright  = 0;
params.highlight       = 0;
params.half_size       = halfSize ? 1 : 0;   // 预览默认开，像素数降到 1/4
params.user_qual       = kQualityAhd;        // 3 = AHD

lr->unpack();                         // 解包成 Bayer/ushort 缓冲
lr->dcraw_process();                  // 去马赛克 + 白平衡 + 色彩矩阵 + gamma
img = lr->dcraw_make_mem_image();     // 拿到 RGB 位图
bitmap = downsampleToBitmap(img, maxDim);
LibRaw::dcraw_clear_mem(img);
lr->recycle();                        // 释放数百 MB 中间缓冲
session->armed = false;               // 下次操作会重新 open_buffer
```

> **关于 `user_qual`**：LibRaw 0.22 **没有为去马赛克质量提供公开枚举**（头文件里搜不到
> `LIBRAW_DEMOSAIC_*`）。取值来自 `src/postprocessing/dcraw_process.cpp` 的 `quality` 分支：
> `0=线性 1=VNG 2=PPG 3=AHD 4=DCB 11=DHT 12=AAHD`。代码里已固化成 `kQualityAhd = 3` 并加注释。

#### `downsampleToBitmap()` —— 为什么降采样必须在 native 侧

33MP 全分辨率的 RGB 数组是百 MB 级。搬到 Kotlin 再缩放既慢又费内存。
所以在 C++ 里做**盒式降采样**，只把最终的小位图（如 2048px，约 11 MB）交过 JNI。

```cpp
int scale = (longest + maxDim - 1) / maxDim;   // 向上取整
int dw = sw / scale, dh = sh / scale;          // 丢弃不足一整块的边缘，省掉边界判断

for each dst pixel:
    sum R,G,B over scale×scale block
    r /= scale*scale; g /= ...; b /= ...
    if (bits == 16) { r >>= 8; ... }           // 16bit → 8bit
    out[i] = 0xFF000000 | r<<16 | g<<8 | b;    // ARGB_8888

// 用反射调用 Bitmap.createBitmap(int[], w, h, Config.ARGB_8888)
```

**16bit 数据的字节序是原生字节序**——`copy_mem_image()` 直接按 `ushort*` 写入，不做字节交换。
这一点已核对源码确认，不要自作主张加 `htons`。

### 4.6 构建脚本

#### `cpp/CMakeLists.txt`（顶层）

```cmake
get_filename_component(PC_REPO_ROOT "${CMAKE_SOURCE_DIR}/../../../.." ABSOLUTE)
set(LIBRAW_ROOT "${PC_REPO_ROOT}/third_party/libraw" CACHE PATH "...")

if(NOT EXISTS "${LIBRAW_ROOT}/libraw/libraw.h")
    message(FATAL_ERROR "找不到 LibRaw 源码：${LIBRAW_ROOT}\n请先执行：git submodule update --init --recursive")
endif()

add_subdirectory(libraw)
add_library(pixelcake_raw SHARED raw_bridge.cpp)
target_link_libraries(pixelcake_raw PRIVATE libraw_static log)
```

注意 `../..` 的级数：本文件在 `app/src/main/cpp/`，往上 4 级才是仓库根，
而 `third_party/` 与 `app/` 同级——不能用相对 app 的路径。

#### `cpp/libraw/CMakeLists.txt`

**为什么不用 LibRaw 自带的 CMake**：仓库根没有 `CMakeLists.txt`（主力构建是 autotools 的
`Makefile.dist`），`buildfiles/` 里那套是次要路径，交叉编译时容易 `find_package` 误判宿主机库。

做法：**GLOB `src/*.cpp`，按 basename 对 79 个名字的白名单筛选**，白名单来自官方 `Makefile.dist` 的 `LIB_OBJECTS`。

```cmake
list(LENGTH LIBRAW_SOURCES _libraw_selected_count)
if(NOT _libraw_selected_count EQUAL 79)
    message(FATAL_ERROR "LibRaw 源文件筛选得到 ${n} 个，期望 79 个。submodule 可能不在 0.22.2")
endif()
```

> **这个数量校验不要删。** 版本一变，宁可构建失败也不要悄悄编出残缺的库。

编译选项：

```cmake
-w                    # 第三方代码，别让它淹没 CI 日志
-fexceptions          # LibRaw 用 C++ 异常（LIBRAW_THROW）传递错误
-fno-strict-aliasing  # dcraw 血缘代码有类型双关，开严格别名优化会出错
```

可选项（**当前都关着**）：

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `PC_LIBRAW_OPENMP` | OFF（→ `LIBRAW_NOTHREADS`） | 开启后 AHD 能并行，但要确认 `libomp.so` 会被打进 APK；否则用 `-static-openmp` |
| ZLIB | 已移除 | 只有浮点 DNG 的 deflate 解压需要，Sony ARW 用不到 |

**两个 glue 文件（`dngsdk_glue.cpp` / `rawspeed_glue.cpp`）是安全的可编译进来的**——
已核对源码，它们的 DNG SDK / RawSpeed 头文件引用都被 `#ifdef USE_DNGSDK` / `#ifdef USE_RAWSPEED` 完整包裹。

### 4.7 CI：`.github/workflows/android.yml`

四个 job：

| job | 作用 | 备注 |
| --- | --- | --- |
| `build` | `assembleDebug`，上传 APK（保留 14 天） | |
| `lint` | `lintDebug`，上传报告（保留 7 天） | `continue-on-error: true`，不阻断 |
| `check-signing` | 探测 `KEYSTORE_BASE64` 是否非空，输出 `has-keystore` | **必须的间接层** |
| `release` | 签名 Release APK（保留 90 天） | `if: needs.check-signing.outputs.has-keystore == 'true'` |

**`check-signing` 为什么存在**：GitHub **不允许 `secrets` 上下文出现在 job 级 `if`**。
一旦写了，整个 workflow 会在解析期被判非法，一个 job 都不创建——
表现为 run 瞬间失败、`total_count: 0`、日志 404。
所以用一个轻量 job 把 secret 转成 output 再传给 `release`。

> **阶段 2 必改**：三个 job 的 `actions/checkout@v4` 都要加 `submodules: recursive`，
> 否则 CMake 会 FATAL_ERROR「找不到 LibRaw 源码」。

---

## 5. 调用链全景

### 5.1 阶段 1（已跑通）

```
MainActivity.onCreate
  └─ enableEdgeToEdge()
  └─ setContent { PixelCakeTheme { Surface { HomeScreen() } } }
        └─ HomeScreen
              ├─ Context.probeCapabilities()          [DeviceCapabilities.kt]
              │    ├─ ActivityManager.MemoryInfo  → availMem / totalMem
              │    ├─ display.isWideColorGamut    → P3 支持
              │    ├─ display.hdrCapabilities     → HDR 类型
              │    └─ 7008×4672×8B → 单缓冲 249MiB / 三缓冲 749MiB
              │         → fullResFeasible = avail >= triple * 2
              ├─ ContextCompat.checkSelfPermission(READ_MEDIA_IMAGES)
              │    └─ 未授权 → rememberLauncherForActivityResult(RequestPermission)
              └─ LazyColumn { SectionCard × 5 }
                    └─ InfoRow(label, value, Ok/Warn/Bad)
```

### 5.2 阶段 2（目标，待补完）

```
[主线程] RawDecodeScreen
   │  用户点「选择 ARW 文件」
   ├─ ActivityResultContracts.OpenDocument().launch(arrayOf("*/*"))
   │     （ARW 的 MIME 不统一，有的设备报 application/octet-stream，所以不筛 image/*）
   ▼
[ViewModel] onFilePicked(uri)
   ├─ withContext(Dispatchers.IO)   → 读 Uri 全部字节到 ByteBuffer.allocateDirect
   └─ withContext(RawDispatcher)    → RawImage.open(buffer)
                                        │
   ┌────────────────────────────────────┘
   ▼ [JNI] nativeOpen(buffer, limit)
        ├─ env->GetDirectBufferAddress(buffer)     （非 direct 直接报错）
        ├─ new RawSession{ lr = new LibRaw(), data, size }
        └─ lr->open_buffer(data, size)   → 成功则 session->armed = true
   ▼ [JNI] nativeInfo(handle)    ← 立刻抓一次，因为 render 之后 imgdata 会被清零
        ├─ ensureArmed()                 （必要时重新 open_buffer）
        ├─ 读 idata / sizes / color / other / thumbnail + get_decoder_info
        └─ 打包成 [IntArray, FloatArray, Array<String>, LongArray]
   ▼ [Kotlin] RawInfoCodec.decode(bundle) → RawInfo
   ▼
[uiState] info = RawInfo  → 卡片展示机身 / 尺寸 / 位深 / 白平衡 / 镜头

   ── 用户点「内嵌预览图」（几十毫秒）──
   ▼ [JNI] nativeEmbeddedThumbnail(handle)
        ├─ rearm()                       （强制重开，防 unpack_thumb 重复分配泄漏）
        ├─ lr->unpack_thumb()
        ├─ lr->dcraw_make_mem_thumb()    → type == LIBRAW_IMAGE_JPEG
        ├─ BitmapFactory.decodeByteArray(bytes, 0, size)     ← 交给 Android 解码
        ├─ LibRaw::dcraw_clear_mem(thumb)
        └─ lr->recycle(); session->armed = false

   ── 用户点「完整解码」（数秒，显示 spinner + 耗时）──
   ▼ [JNI] nativeRender(handle, maxDim, halfSize)
        ├─ rearm()
        ├─ 设 params（相机白平衡 / sRGB / 8bit / AHD / half_size）
        ├─ unpack() → dcraw_process() → dcraw_make_mem_image()
        ├─ downsampleToBitmap()         ← native 侧盒式降采样
        └─ lr->recycle(); session->armed = false
   ▼
[uiState] rendered = Bitmap + renderMs → Image(bitmap.asImageBitmap())
```

### 5.3 生命周期与内存

```
ByteBuffer.allocateDirect(33MB)   ← Kotlin 持有，LibRaw 直接引用其地址
        │                            ⚠️ 必须比 LibRaw 实例活得久
        ▼
RawSession (C++)                  ← 非线程安全，由 RawDispatcher 串行访问
        │
        ├─ armed = true   可查 info / 可 render
        └─ armed = false  imgdata 已清零，下次操作先 rearm()

RawImage.close()  → nativeClose → delete lr; delete session
                    （ByteBuffer 交给 GC）
```

---

## 6. 未完成部分的伪代码

> 以下是**阶段 2 收尾**的实现草案。可直接落地，落地后按 `plan.md` §7 的清单推进。

### 6.1 `raw/RawNative.kt`

```kotlin
package com.hifn.pixelcake.raw

import android.graphics.Bitmap
import java.nio.ByteBuffer

/**
 * LibRaw 的 Kotlin 侧门面。
 *
 * ⚠️ 重要：这里的函数是 **object 的实例方法**，不是静态方法。
 * Kotlin 的 `object` 成员在字节码里是实例方法（要静态得加 @JvmStatic），
 * 所以 raw_bridge.cpp 里对应的 JNI 函数第二个参数是 `jobject thiz` 而不是 `jclass`。
 * 两侧必须一致，否则运行期 UnsatisfiedLinkError。
 */
internal object RawNative {

    init {
        System.loadLibrary("pixelcake_raw")
    }

    /** LibRaw 版本串，如 "0.22.2-Release" */
    external fun nativeVersion(): String

    /** 打开 RAW。失败抛 IOException。返回 0 表示失败。 */
    external fun nativeOpen(buffer: ByteBuffer, length: Int): Long

    /** 释放句柄。handle 为 0 时无操作。 */
    external fun nativeClose(handle: Long)

    /**
     * 读元数据。
     * 返回 Array<Any>：[0]=IntArray [1]=FloatArray [2]=Array<String> [3]=LongArray
     * 由 [RawInfoCodec] 解码。
     */
    external fun nativeInfo(handle: Long): Array<Any>

    /** 内嵌预览图（多为 1600px 级 JPEG）。无内嵌预览时抛 IOException。 */
    external fun nativeEmbeddedThumbnail(handle: Long): Bitmap?

    /** 完整解码 + 降采样。数秒级，必须在后台线程调用。 */
    external fun nativeRender(handle: Long, maxDim: Int, halfSize: Boolean): Bitmap?
}
```

### 6.2 `raw/RawImage.kt`

```kotlin
package com.hifn.pixelcake.raw

import android.graphics.Bitmap
import java.io.Closeable
import java.nio.ByteBuffer

/**
 * 一次 RAW 会话。
 *
 * [source] 必须保活到句柄关闭 —— LibRaw 直接引用它的内存地址，不做拷贝。
 * [info] 在 open 时就抓取：render() 之后 LibRaw 的 imgdata 会被 recycle 清零，
 *        那时再查尺寸只会拿到 0。
 */
class RawImage private constructor(
    private val source: ByteBuffer,
    private var handle: Long,
    val info: RawInfo
) : Closeable {

    fun embeddedThumbnail(): Bitmap? = RawNative.nativeEmbeddedThumbnail(requireHandle())

    fun render(maxDim: Int, halfSize: Boolean): Bitmap =
        RawNative.nativeRender(requireHandle(), maxDim, halfSize)
            ?: throw java.io.IOException("LibRaw 返回空图像")

    override fun close() {
        val h = handle
        handle = 0L
        if (h != 0L) RawNative.nativeClose(h)
    }

    private fun requireHandle(): Long {
        val h = handle
        check(h != 0L) { "RAW 句柄已关闭" }
        return h
    }

    companion object {
        fun open(buffer: ByteBuffer): RawImage {
            require(buffer.isDirect) { "必须使用 direct ByteBuffer（ByteBuffer.allocateDirect）" }
            val handle = RawNative.nativeOpen(buffer, buffer.limit())
            return try {
                RawImage(buffer, handle, RawInfoCodec.decode(RawNative.nativeInfo(handle)))
            } catch (t: Throwable) {
                RawNative.nativeClose(handle)   // open 成功但 info 失败，别漏了句柄
                throw t
            }
        }
    }
}
```

> `source` 字段在类里只用于保活（不被方法读取）。Kotlin 会警告 `unused`，
> 用 `@Suppress("unused")` 或在注释里写明用途。

### 6.3 `raw/RawDispatcher.kt`

```kotlin
package com.hifn.pixelcake.raw

import kotlinx.coroutines.asCoroutineDispatcher
import java.util.concurrent.Executors

/**
 * LibRaw 实例不是线程安全的，且单次解码峰值可达数百 MB。
 * 用单线程调度器串行化所有 native 调用 —— 顺带避免并发解码打爆内存。
 */
internal val RawDispatcher = Executors.newSingleThreadExecutor { r ->
    Thread(r, "pixelcake-raw").apply { isDaemon = true }
}.asCoroutineDispatcher()
```

### 6.4 `PixelCakeApp.kt` 增量

```kotlin
override fun onCreate() {
    super.onCreate()
    // 放在 Application 而不是 RawNative 的 init，避免懒加载时正好卡在渲染帧上
    System.loadLibrary("pixelcake_raw")
}
```

> 若已经放在 `RawNative` 的 `init {}` 里，两处不要重复加载（`loadLibrary` 重复调用是安全的，但没必要）。

### 6.5 `app/build.gradle.kts` 增量

在 `android {}` 内：

```kotlin
ndkVersion = "27.2.12479018"          // 建议同步进 gradle/libs.versions.toml

defaultConfig {
    // 已有字段不动，只加这一段
    ndk {
        abiFilters += "arm64-v8a"     // 一加 15 是 arm64，编全 ABI 白白翻倍构建时间
    }
}

// 与 defaultConfig / buildTypes 同级
externalNativeBuild {
    cmake {
        path = file("src/main/cpp/CMakeLists.txt")
        version = "3.22.1"
    }
}
```

依赖加一条（版本目录同步加 `androidx-lifecycle-viewmodel-compose`）：

```kotlin
dependencies {
    // ... 已有
    implementation(libs.androidx.lifecycle.viewmodel.compose)
}
```

`gradle/libs.versions.toml` 增量：

```toml
[versions]
lifecycleRuntimeKtx = "2.10.0"      # 已有，viewmodel 复用同一版本号

[libraries]
androidx-lifecycle-viewmodel-compose = { group = "androidx.lifecycle", name = "lifecycle-viewmodel-compose", version.ref = "lifecycleRuntimeKtx" }
```

### 6.6 `ui/raw/RawDecodeViewModel.kt`

```kotlin
package com.hifn.pixelcake.ui.raw

data class RawDecodeUiState(
    val fileName: String? = null,
    val fileSizeBytes: Long = 0L,
    val info: RawInfo? = null,
    val thumbnail: Bitmap? = null,
    val rendered: Bitmap? = null,
    val renderMs: Long = 0L,
    val renderNote: String? = null,     // 如 "half_size · 3504×2336 → 1728"
    val busy: Boolean = false,
    val error: String? = null,
    val librawVersion: String? = null
)

class RawDecodeViewModel(app: Application) : AndroidViewModel(app) {

    private var buffer: ByteBuffer? = null
    private var image: RawImage? = null

    var uiState by mutableStateOf(RawDecodeUiState())
        private set

    init {
        uiState = uiState.copy(librawVersion = RawNative.nativeVersion())
    }

    fun onFilePicked(uri: Uri) = viewModelScope.launch {
        uiState = uiState.copy(busy = true, error = null, thumbnail = null, rendered = null)
        try {
            val bytes = withContext(Dispatchers.IO) { readAll(getApplication(), uri) }
            val opened = withContext(RawDispatcher) { RawImage.open(bytes) }
            releaseCurrent()                       // 先关掉上一个再接管新的
            buffer = bytes
            image = opened
            uiState = uiState.copy(
                fileName = displayName(getApplication(), uri),
                fileSizeBytes = bytes.limit().toLong(),
                info = opened.info,
                busy = false
            )
        } catch (t: Throwable) {
            uiState = uiState.copy(busy = false, error = t.message ?: t.toString())
        }
    }

    fun loadThumbnail() = viewModelScope.launch {
        val img = image ?: return@launch
        uiState = uiState.copy(busy = true, error = null)
        runCatching { withContext(RawDispatcher) { img.embeddedThumbnail() } }
            .onSuccess { uiState = uiState.copy(thumbnail = it, busy = false) }
            .onFailure { uiState = uiState.copy(error = it.message, busy = false) }
    }

    fun render(maxDim: Int, halfSize: Boolean) = viewModelScope.launch {
        val img = image ?: return@launch
        uiState = uiState.copy(busy = true, error = null)
        val started = SystemClock.elapsedRealtime()
        runCatching { withContext(RawDispatcher) { img.render(maxDim, halfSize) } }
            .onSuccess { bmp ->
                uiState = uiState.copy(
                    rendered = bmp,
                    renderMs = SystemClock.elapsedRealtime() - started,
                    renderNote = "${if (halfSize) "half_size" else "full"} · ${bmp.width}×${bmp.height}",
                    busy = false
                )
            }
            .onFailure { uiState = uiState.copy(error = it.message, busy = false) }
    }

    private fun releaseCurrent() {
        image?.close()
        image = null
        buffer = null                              // 交给 GC
    }

    override fun onCleared() { releaseCurrent() }

    /** Uri → direct ByteBuffer。ARW 是 33MB 级，必须走 direct（LibRaw 要拿地址）。 */
    private fun readAll(context: Context, uri: Uri): ByteBuffer {
        val size = context.contentResolver.openFileDescriptor(uri, "r")?.use { it.statSize } ?: -1L
        require(size > 0) { "无法读取文件大小" }
        require(size <= Int.MAX_VALUE) { "文件过大：${size} 字节" }
        val buffer = ByteBuffer.allocateDirect(size.toInt())
        context.contentResolver.openInputStream(uri)?.use { input ->
            val channel = Channels.newChannel(input)
            while (buffer.hasRemaining()) {
                if (channel.read(buffer) <= 0) break
            }
        }
        buffer.flip()                              // limit = 实际字节数
        return buffer
    }

    private fun displayName(context: Context, uri: Uri): String =
        context.contentResolver.query(uri, null, null, null, null)?.use { c ->
            val i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (i >= 0 && c.moveToFirst()) c.getString(i) else null
        } ?: uri.lastPathSegment ?: "unknown"
}
```

### 6.7 `ui/raw/RawDecodeScreen.kt`

```kotlin
@Composable
fun RawDecodeScreen(viewModel: RawDecodeViewModel = viewModel()) {
    val state = viewModel.uiState
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) viewModel.onFilePicked(uri)
    }

    LazyColumn(...) {
        item { SectionCard("LibRaw") { InfoRow("版本", state.librawVersion ?: "未加载") } }

        item {
            // ARW 的 MIME 不统一（有的设备报 application/octet-stream），
            // 所以不筛 image/*，直接让用户从文件树里挑
            Button(onClick = { picker.launch(arrayOf("*/*")) }) { Text("选择 ARW 文件") }
        }

        state.info?.let { info ->
            item { SectionCard("机身") {
                InfoRow("厂商 / 型号", "${info.make} ${info.model}")
                InfoRow("归一化", "${info.normalizedMake} ${info.normalizedModel}")
                InfoRow("解码器", info.unpackFunction)
            } }
            item { SectionCard("传感器") {
                InfoRow("RAW 尺寸", "${info.rawWidth} × ${info.rawHeight}")
                InfoRow("输出尺寸", "${info.width} × ${info.height}")
                InfoRow("位深", "${info.rawBps} bit", if (info.rawBps >= 14) Ok else Warn)
                InfoRow("Bayer", "${info.cdesc} (filters=${info.filters})")
                InfoRow("黑 / 白电平", "${info.black} / ${info.maximum}")
            } }
            item { SectionCard("色彩") {
                InfoRow("相机白平衡", info.camMul.joinToString(" / ") { "%.3f".format(it) })
                InfoRow("cam_xyz[0]", info.camXyz.take(3).joinToString(" / ") { "%.4f".format(it) })
            } }
            item { SectionCard("拍摄参数") {
                InfoRow("ISO", info.iso.toString())
                InfoRow("快门", "1/${(1f / info.shutter).toInt()}s")     // shutter 单位为秒
                InfoRow("光圈", "f/${info.aperture}")
                InfoRow("焦距", "${info.focalLength}mm")
            } }

            item { Row {
                Button(onClick = { viewModel.loadThumbnail() }, enabled = !state.busy) {
                    Text("内嵌预览图")
                }
                Button(onClick = { viewModel.render(2048, halfSize = true) }, enabled = !state.busy) {
                    Text("完整解码（1/2）")
                }
                Button(onClick = { viewModel.render(2048, halfSize = false) }, enabled = !state.busy) {
                    Text("完整解码（全分辨率）")
                }
            } }
        }

        state.thumbnail?.let { item { PreviewCard("内嵌预览", it, null) } }
        state.rendered?.let {
            item { PreviewCard("解码结果", it, "${state.renderNote} · ${state.renderMs} ms") }
        }
        state.error?.let { item { ErrorCard(it) } }
        if (state.busy) item { LinearProgressIndicator(Modifier.fillMaxWidth()) }
    }
}
```

### 6.8 `MainActivity.kt` 改造

```kotlin
setContent {
    PixelCakeTheme {
        var tab by rememberSaveable { mutableIntStateOf(0) }
        Scaffold(
            topBar = {
                TopAppBar(
                    title = { Text("PixelCake") },
                    actions = {
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            FilterChip(selected = tab == 0, onClick = { tab = 0 }, label = { Text("设备能力") })
                            FilterChip(selected = tab == 1, onClick = { tab = 1 }, label = { Text("RAW 解码") })
                        }
                    }
                )
            }
        ) { padding ->
            when (tab) {
                0 -> HomeScreen(modifier = Modifier.padding(padding))
                1 -> RawDecodeScreen(modifier = Modifier.padding(padding))
            }
        }
    }
}
```

同时 `HomeScreen` 要**去掉自己的 `Scaffold`/`TopAppBar`**，签名改成
`fun HomeScreen(modifier: Modifier = Modifier)`，把 `padding` 传给内部 `LazyColumn`。
（用 FilterChip 而不是 NavigationBar，是为了不引入 `material-icons-extended` 依赖——
那会给每次 CI 多加几十秒编译。）

### 6.9 CI 增量（**不加必挂**）

```yaml
- name: Checkout
  uses: actions/checkout@v4
  with:
    submodules: recursive      # ← 三个 job 都要加
```

不加的话 `third_party/libraw` 是空目录，`CMakeLists.txt` 会 FATAL_ERROR「找不到 LibRaw 源码」。

### 6.10 后续阶段骨架（阶段 F3，仅供参考方向）

```kotlin
// engine/EditParams.kt —— 中枢契约，冻结后只增不改
data class EditParams(
    val exposure: Float = 0f,      // EV
    val contrast: Float = 0f,      // -1..1
    val highlights: Float = 0f,    // -1..1
    val shadows: Float = 0f,       // -1..1
    val whites: Float = 0f,
    val blacks: Float = 0f,
    val saturation: Float = 0f,
    val vibrance: Float = 0f,
    val temperature: Float = 0f,   // Kelvin 偏移
    val tint: Float = 0f,
    val clarity: Float = 0f,
    val dehaze: Float = 0f,
    val sharpen: Float = 0f,
    val denoise: Float = 0f,
    val vignette: Float = 0f,
    val grain: Float = 0f,
    val crop: RectF? = null,
    val rotation: Int = 0,
    val lutId: String? = null,
    val lutStrength: Float = 1f
)

// engine/RenderPipeline.kt
interface RenderPipeline {
    /** 上传线性 16bit 数据为 RGBA16F 纹理 */
    fun uploadLinearRgb(data: ShortArray, width: Int, height: Int)
    /** 预览：降到屏幕尺寸，输出到 P3 FBO */
    fun renderPreview(params: EditParams, viewport: IntSize): Bitmap
    /** 导出：全分辨率，走同一份 GLSL */
    fun renderFull(params: EditParams): Bitmap
    fun release()
}
```

着色器统一放在 `engine/shaders/`，**预览与导出共用同一段 GLSL**，
这是"预览好看、导出不一样"问题的唯一根治办法。

---

## 7. 编码约定（新 Agent 必须遵守）

### 7.1 提交前必做

本机**没有 Android SDK / JDK**，编译不了。CI 一轮约 3 分钟，所以提交前必须静态自检：

1. **XML 合法性**：所有 `res/**/*.xml`、`AndroidManifest.xml` 过一遍解析器
2. **括号配平**：尤其用编辑工具改过的 `.kts` 文件——历史上发生过吞掉 `buildFeatures {` 一行的情况
3. **JNI 函数名 ↔ Kotlin `external` 声明**逐字比对
4. **改完立即复读 diff**，不要凭记忆认为改对了

### 7.2 版本与依赖

- 版本号统一写在 `gradle/libs.versions.toml`，**不在 build.gradle.kts 里写死**
- Compose 相关依赖不写版本，交给 BOM 管理
- AGP 停在 8.13.2（8.x 末版），不上 9.x

### 7.3 Kotlin / JNI

- **`object` 的成员是实例方法**，对应 JNI 第二个参数是 `jobject` 而非 `jclass`
- JNI 侧的扁平数组下标常量与 Kotlin 侧**必须同名同值**，改一侧就要改另一侧
- LibRaw 的 `recycle()` 会清空 `imgdata`，调用后必须重新 `open_buffer`
- 传给 LibRaw 的 direct ByteBuffer **必须保活到句柄关闭**
- 错误统一抛 `java.io.IOException`，消息里带上 LibRaw 错误码与 `libraw_strerror` 文本

### 7.4 注释

注释写**为什么**，不写**做了什么**。特别是这几类：

- 反直觉的取舍（为什么不用 AGSL、为什么不做动态取色）
- 踩过的坑（`orEmpty()` 与 `IntArray`、secrets 不能在 job 级 `if`）
- 依赖了外部未公开实现的地方（`user_qual` 的裸整数、16bit 是原生字节序）

反面教材：`// 设置白平衡` 这种注释没有价值。

### 7.5 不要动的东西

| 不要动 | 原因 |
| --- | --- |
| `res/mipmap-anydpi`（别改回 `-v26`） | minSdk 33 > 26，限定符无意义 |
| 备份规则里删掉的 `<exclude>` | 有 `<include>` 后再 exclude 会报 lint error |
| `targetSdk = 36` / `AGP 8.13.2` | 版本停在这是刻意的，lint 警告也是刻意的 |
| LibRaw CMake 里的 79 个源文件数量校验 | 版本不符时它应该是构建失败，而不是悄悄编出残缺的库 |
| HomeScreen 的设备能力项 | 这是真机验收基线，删了就失去回归参照 |

---

## 8. 速查：改动一个功能需要动哪些文件

| 我想… | 要动的文件 |
| --- | --- |
| 加一个 RAW 元数据字段 | `raw_bridge.cpp`（扩数组 + 下标常量）+ `RawInfo.kt`（扩 data class + 下标常量 + decode） |
| 换去马赛克算法 | `raw_bridge.cpp` 的 `kQualityAhd`；或加参数从 Kotlin 传下来 |
| 加一个调色参数 | `engine/EditParams.kt` + 对应 GLSL + `EditScreen`；**先看 schema 是否已冻结** |
| 加一个导出格式 | `engine/RenderPipeline` + 编码器；导出走与预览相同的着色器 |
| 升依赖版本 | 只改 `gradle/libs.versions.toml` |
| 改 CI | `.github/workflows/android.yml`；**记住 secrets 不能进 job 级 if** |
| 加一个新页面 | `ui/<feature>/` 下建 Screen + ViewModel；在 `MainActivity` 的 Tab 里挂上 |
