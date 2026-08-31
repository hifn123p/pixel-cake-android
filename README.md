# PixelCake Android

[![Android CI](https://github.com/hifn123p/pixel-cake-android/actions/workflows/android.yml/badge.svg)](https://github.com/hifn123p/pixel-cake-android/actions/workflows/android.yml)

Sony A7C2 照片的本地 RAW 调色应用。全流程离线，广色域（Display P3）端到端管线。

技术栈：Kotlin + Jetpack Compose + Material 3，构建由 GitHub Actions 完成。

## 关键选择

| 项 | 值 | 理由 |
| --- | --- | --- |
| minSdk | 33 | 广色域与 Compose 现代 API 的实用下限 |
| targetSdk / compileSdk | 36 | 对齐 Android 16（一加 15 / ColorOS 16） |
| 渲染 | GLES 3.0 + RGBA16F | 预览与导出共用同一份 GLSL，杜绝"预览好看、导出不同" |
| RAW 解码 | NDK + LibRaw 0.22 | 系统 `ImageDecoder` 拿不到 Bayer 数据，无法做自己的去马赛克与色彩矩阵 |
| AI | MediaPipe Tasks + 自托管 ONNX | 国行 ColorOS 无 GMS，ML Kit 不可用 |

## 本地构建

```bash
./gradlew assembleDebug
```

需要 JDK 17。

## CI / CD

推送到 `main` 或发起 PR 时，`.github/workflows/android.yml` 会：

1. **build** — 编译 Debug APK 并上传为构建产物（保留 14 天）
2. **lint** — 产出 lint 报告（不阻断）
3. **release** — 若已配置签名 Secrets，额外产出签名 Release APK（保留 90 天）

### 配置签名（可选）

在仓库 Settings → Secrets and variables → Actions 中添加：

| Secret | 说明 |
| --- | --- |
| `KEYSTORE_BASE64` | `base64 -i your.jks \| pbcopy`（Windows 用 `certutil -encode`） |
| `KEYSTORE_PASSWORD` | keystore 密码 |
| `KEY_ALIAS` | 密钥别名 |
| `KEY_PASSWORD` | 密钥密码 |

未配置时 release job 自动跳过，不影响 Debug 构建。

取 APK：Actions → 对应 run → Artifacts → `pixelcake-debug-*`。

### 关于 release job 的探测方式

GitHub **不允许 `secrets` 出现在 job 级 `if` 条件里**——一旦写了，整个 workflow
文件会被判为非法，一个 job 都不创建（表现为 run 立刻失败、`total_count: 0`、日志 404）。
所以这里用一个 `check-signing` job 探测 `KEYSTORE_BASE64` 是否非空，再把结果以
`outputs` 传给 `release` 的 `if`。

### 当前 lint 状态

0 error。剩余 10 条 warning 均为版本提示，非阻塞：

- `OldTargetApi` — targetSdk 36 尚未对齐最新 SDK，待实际适配后再升
- `AndroidGradlePluginVersion` — 有 AGP 9.x / Gradle 8.14+；本项目刻意停在
  AGP 8.13.2（8.x 末版），避免未经适配的大版本跳跃
- `GradleDependency` / `NewerVersionAvailable` — 依赖库有更新版本

## 路线图

- [x] 工程骨架 + CI
- [ ] NDK + LibRaw（A7C2 ARW 解码）
- [ ] fp16 渲染管线 + EGL P3
- [ ] 基础调色 GPU 化
- [ ] 广色域输出（Ultra HDR / 16bit TIFF）
- [ ] 3D LUT 滤镜与局部调整
- [ ] 磨皮与液化
- [ ] AI 接入（MediaPipe + 自托管模型）

## 许可

桌面端项目 [pixel-cake](https://github.com/hifn123p/pixel-cake) 的 Android 实现，代码逻辑仅供参考。
