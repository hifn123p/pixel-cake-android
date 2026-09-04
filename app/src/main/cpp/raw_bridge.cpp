// PixelCake —— LibRaw 的 JNI 桥接层
//
// 职责边界：
//   * 这层只做「解码 + 降采样 + 打包成 Bitmap」，不做任何调色。
//     调色在第 3 步会搬到 GPU 上的 fp16 管线，那时 LibRaw 只负责吐出线性 16bit 数据。
//   * 降采样必须在 native 侧完成。33MP 全分辨率的 RGB 数组是百 MB 级，
//     搬过 JNI 再交给 Kotlin 缩放既慢又浪费内存。
//
// 线程模型：RawSession 不是线程安全的。Kotlin 侧用 RawDispatcher（单线程）串行访问。
// 生命周期：Kotlin 持有 direct ByteBuffer，LibRaw 直接引用其地址，
//           因此 ByteBuffer 必须比 LibRaw 实例活得久。

#include <jni.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <libraw/libraw.h>

namespace {

// ---------------------------------------------------------------------------
// 与 Kotlin 侧 RawInfoCodec 一一对应的数组下标。
// 两组下标必须同步修改，否则读数会静默错位 —— 这是本文件最容易出错的地方，
// 所以采用「扁平数组 + 具名下标」而不是几十个 SetXxxField，
// 让契约集中在一处，改错一眼就能看出来。
// 每个分组最后一个 *_COUNT 同时用作数组长度。
// ---------------------------------------------------------------------------

// IntArray
constexpr jsize I_COLORS = 0;
constexpr jsize I_FILTERS = 1;
constexpr jsize I_RAW_COUNT = 2;
constexpr jsize I_FOVEON = 3;
constexpr jsize I_RAW_WIDTH = 4;
constexpr jsize I_RAW_HEIGHT = 5;
constexpr jsize I_WIDTH = 6;
constexpr jsize I_HEIGHT = 7;
constexpr jsize I_IWIDTH = 8;
constexpr jsize I_IHEIGHT = 9;
constexpr jsize I_RAW_PITCH = 10;
constexpr jsize I_FLIP = 11;
constexpr jsize I_BLACK = 12;
constexpr jsize I_MAXIMUM = 13;
constexpr jsize I_RAW_BPS = 14;
constexpr jsize I_COLORSPACE = 15;
constexpr jsize I_THUMB_FORMAT = 16;
constexpr jsize I_THUMB_WIDTH = 17;
constexpr jsize I_THUMB_HEIGHT = 18;
constexpr jsize I_DECODER_FLAGS = 19;
constexpr jsize I_COUNT = 20;

// FloatArray
constexpr jsize F_CAM_MUL = 0;  // 4 个：R, G1, B, G2
constexpr jsize F_PRE_MUL = 4;  // 4 个
constexpr jsize F_CAM_XYZ = 8;  // 12 个，行主序 4x3
constexpr jsize F_ISO = 20;
constexpr jsize F_SHUTTER = 21;
constexpr jsize F_APERTURE = 22;
constexpr jsize F_FOCAL = 23;
constexpr jsize F_COUNT = 24;

// Array<String>
constexpr jsize S_MAKE = 0;
constexpr jsize S_MODEL = 1;
constexpr jsize S_NORM_MAKE = 2;
constexpr jsize S_NORM_MODEL = 3;
constexpr jsize S_SOFTWARE = 4;
constexpr jsize S_CDESC = 5;
constexpr jsize S_UNPACK = 6;
constexpr jsize S_VERSION = 7;
constexpr jsize S_COUNT = 8;

// LongArray
constexpr jsize L_TIMESTAMP = 0;
constexpr jsize L_COUNT = 1;

// 去马赛克质量档位。LibRaw 0.22 没有为这些数值提供公开枚举，
// 取值来自 src/postprocessing/dcraw_process.cpp 的 quality 分支：
// 0=线性插值 1=VNG 2=PPG 3=AHD 4=DCB 11=DHT 12=AAHD
constexpr int kQualityAhd = 3;

struct RawSession {
    LibRaw* lr = nullptr;
    const void* data = nullptr;  // 指向 Kotlin 的 direct ByteBuffer，不负责释放
    size_t size = 0;
    // false 表示 imgdata 已被 recycle 清零，下次使用前需要重新 open_buffer
    bool armed = false;
};

RawSession* asSession(jlong handle) {
    return reinterpret_cast<RawSession*>(handle);
}

void throwIo(JNIEnv* env, const std::string& what, int librawCode = 0) {
    std::string msg = what;
    if (librawCode != 0) {
        msg += " [LibRaw ";
        msg += std::to_string(librawCode);
        msg += ": ";
        msg += libraw_strerror(librawCode);
        msg += "]";
    }
    if (env->ExceptionCheck()) return;
    jclass cls = env->FindClass("java/io/IOException");
    if (cls == nullptr) return;
    env->ThrowNew(cls, msg.c_str());
    env->DeleteLocalRef(cls);
}

jstring newString(JNIEnv* env, const char* s) {
    return env->NewStringUTF(s != nullptr ? s : "");
}

// LibRaw 里 make/model/cdesc 是定长 char 数组，不保证以 '\0' 结尾，必须按长度截断
jstring newFixedString(JNIEnv* env, const char* s, size_t maxLen) {
    char buf[128];
    size_t n = 0;
    for (size_t i = 0; i < maxLen && n + 1 < sizeof(buf); ++i) {
        if (s[i] == '\0') break;
        buf[n++] = s[i];
    }
    buf[n] = '\0';
    return env->NewStringUTF(buf);
}

void putString(JNIEnv* env, jobjectArray arr, jsize index, jstring value) {
    env->SetObjectArrayElement(arr, index, value);
    env->DeleteLocalRef(value);
}

// open_buffer() 内部会调用 recycle()，所以重复调用即可回到干净状态，
// 不需要手动 recycle 再 open —— 代价是重新解析一遍元数据（几十毫秒）。
bool rearm(RawSession* s) {
    const int rc = s->lr->open_buffer(s->data, s->size);
    s->armed = (rc == LIBRAW_SUCCESS);
    if (!s->armed) {
        s->lr->recycle();  // open 失败时回收可能残留的半初始化状态
    }
    return s->armed;
}

bool ensureArmed(RawSession* s) {
    return s->armed || rearm(s);
}

// 把 LibRaw 输出的 RGB（8bit 或 16bit）盒式降采样成 ARGB_8888 位图。
// 16bit 数据在内存里是原生字节序（copy_mem_image 直接按 ushort* 写入，不做字节交换）。
jobject downsampleToBitmap(JNIEnv* env, const libraw_processed_image_t* img, int maxDim) {
    const int sw = img->width;
    const int sh = img->height;
    const int channels = img->colors;
    const int bits = img->bits;
    if (sw <= 0 || sh <= 0 || channels < 3) return nullptr;
    if (bits != 8 && bits != 16) return nullptr;

    int scale = 1;
    if (maxDim > 0) {
        const int longest = sw > sh ? sw : sh;
        if (longest > maxDim) scale = (longest + maxDim - 1) / maxDim;
    }

    const int dw = sw / scale;  // 丢弃不足一整块的边缘行/列，省掉边界判断
    const int dh = sh / scale;
    if (dw <= 0 || dh <= 0) return nullptr;

    const size_t bytesPerPixel = static_cast<size_t>(channels) * (bits / 8);
    const size_t rowBytes = static_cast<size_t>(sw) * bytesPerPixel;
    const uint8_t* base = img->data;

    std::vector<uint32_t> out(static_cast<size_t>(dw) * static_cast<size_t>(dh));
    const uint64_t samples = static_cast<uint64_t>(scale) * scale;

    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            uint64_t r = 0, g = 0, b = 0;
            for (int sy = 0; sy < scale; ++sy) {
                const uint8_t* row = base + static_cast<size_t>(y * scale + sy) * rowBytes;
                for (int sx = 0; sx < scale; ++sx) {
                    const uint8_t* px = row + static_cast<size_t>(x * scale + sx) * bytesPerPixel;
                    if (bits == 16) {
                        const uint16_t* p = reinterpret_cast<const uint16_t*>(px);
                        r += p[0];
                        g += p[1];
                        b += p[2];
                    } else {
                        r += px[0];
                        g += px[1];
                        b += px[2];
                    }
                }
            }
            r /= samples;
            g /= samples;
            b /= samples;
            if (bits == 16) {
                r >>= 8;
                g >>= 8;
                b >>= 8;
            }
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            out[static_cast<size_t>(y) * dw + x] =
                (0xFFu << 24) | (static_cast<uint32_t>(r) << 16) |
                (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }

    jintArray pixels = env->NewIntArray(dw * dh);
    if (pixels == nullptr) return nullptr;
    env->SetIntArrayRegion(pixels, 0, dw * dh, reinterpret_cast<const jint*>(out.data()));

    jclass configCls = env->FindClass("android/graphics/Bitmap$Config");
    jmethodID valueOf = env->GetStaticMethodID(
        configCls, "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;");
    jstring configName = env->NewStringUTF("ARGB_8888");
    jobject config = env->CallStaticObjectMethod(configCls, valueOf, configName);

    jclass bitmapCls = env->FindClass("android/graphics/Bitmap");
    jmethodID create = env->GetStaticMethodID(
        bitmapCls, "createBitmap",
        "([IIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
    jobject bitmap = env->CallStaticObjectMethod(bitmapCls, create, pixels, dw, dh, config);

    env->DeleteLocalRef(configName);
    env->DeleteLocalRef(config);
    env->DeleteLocalRef(configCls);
    env->DeleteLocalRef(bitmapCls);
    env->DeleteLocalRef(pixels);
    return bitmap;
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_hifn_pixelcake_raw_RawNative_nativeVersion(JNIEnv* env, jobject /*thiz*/) {
    return newString(env, LibRaw::version());
}

JNIEXPORT jlong JNICALL
Java_com_hifn_pixelcake_raw_RawNative_nativeOpen(JNIEnv* env, jobject /*thiz*/,
                                                 jobject buffer, jint length) {
    if (buffer == nullptr || length <= 0) {
        throwIo(env, "缓冲区为空");
        return 0;
    }
    void* data = env->GetDirectBufferAddress(buffer);
    if (data == nullptr) {
        throwIo(env, "LibRaw 需要 direct ByteBuffer（ByteBuffer.allocateDirect）");
        return 0;
    }

    RawSession* session = nullptr;
    try {
        session = new RawSession();
        session->lr = new LibRaw();
        session->data = data;
        session->size = static_cast<size_t>(length);
    } catch (const std::bad_alloc&) {
        delete session;
        throwIo(env, "内存不足，无法创建 LibRaw 实例");
        return 0;
    }

    const int rc = session->lr->open_buffer(data, session->size);
    if (rc != LIBRAW_SUCCESS) {
        const std::string detail = std::string("无法识别为 LibRaw 支持的 RAW：") + libraw_strerror(rc);
        session->lr->recycle();
        delete session->lr;
        delete session;
        throwIo(env, detail, rc);
        return 0;
    }
    session->armed = true;
    return reinterpret_cast<jlong>(session);
}

JNIEXPORT void JNICALL
Java_com_hifn_pixelcake_raw_RawNative_nativeClose(JNIEnv* /*env*/, jobject /*thiz*/,
                                                  jlong handle) {
    RawSession* session = asSession(handle);
    if (session == nullptr) return;
    delete session->lr;
    delete session;
}

// 返回 Array<Any>：
//   [0] IntArray    定长标量
//   [1] FloatArray  色彩与拍摄参数
//   [2] Array<String>
//   [3] LongArray
// 之所以传扁平数组而不是直接构造 Kotlin data class，是为了把「字段顺序」这个契约
// 收敛到一组具名下标常量上，而不是散落在几十次 SetXxxField 调用里。
JNIEXPORT jobjectArray JNICALL
Java_com_hifn_pixelcake_raw_RawNative_nativeInfo(JNIEnv* env, jobject /*thiz*/,
                                                 jlong handle) {
    RawSession* session = asSession(handle);
    if (session == nullptr) {
        throwIo(env, "句柄为空");
        return nullptr;
    }
    if (!ensureArmed(session)) {
        throwIo(env, "重新打开 RAW 失败，数据可能已损坏");
        return nullptr;
    }

    LibRaw* lr = session->lr;
    const libraw_iparams_t& id = lr->imgdata.idata;
    const libraw_image_sizes_t& sz = lr->imgdata.sizes;
    const libraw_colordata_t& cd = lr->imgdata.color;
    const libraw_imgother_t& ot = lr->imgdata.other;
    const libraw_thumbnail_t& th = lr->imgdata.thumbnail;

    libraw_decoder_info_t decoderInfo{};
    lr->get_decoder_info(&decoderInfo);

    jintArray ints = env->NewIntArray(I_COUNT);
    jfloatArray floats = env->NewFloatArray(F_COUNT);
    jlongArray longs = env->NewLongArray(L_COUNT);
    jclass stringCls = env->FindClass("java/lang/String");
    jobjectArray strings = env->NewObjectArray(S_COUNT, stringCls, nullptr);

    jint intValues[I_COUNT] = {0};
    intValues[I_COLORS] = id.colors;
    intValues[I_FILTERS] = static_cast<jint>(id.filters);
    intValues[I_RAW_COUNT] = static_cast<jint>(id.raw_count);
    intValues[I_FOVEON] = static_cast<jint>(id.is_foveon);
    intValues[I_RAW_WIDTH] = sz.raw_width;
    intValues[I_RAW_HEIGHT] = sz.raw_height;
    intValues[I_WIDTH] = sz.width;
    intValues[I_HEIGHT] = sz.height;
    intValues[I_IWIDTH] = sz.iwidth;
    intValues[I_IHEIGHT] = sz.iheight;
    intValues[I_RAW_PITCH] = static_cast<jint>(sz.raw_pitch);
    intValues[I_FLIP] = sz.flip;
    intValues[I_BLACK] = static_cast<jint>(cd.black);
    intValues[I_MAXIMUM] = static_cast<jint>(cd.maximum);
    intValues[I_RAW_BPS] = static_cast<jint>(cd.raw_bps);
    intValues[I_COLORSPACE] = cd.ExifColorSpace;
    intValues[I_THUMB_FORMAT] = static_cast<jint>(th.tformat);
    intValues[I_THUMB_WIDTH] = th.twidth;
    intValues[I_THUMB_HEIGHT] = th.theight;
    intValues[I_DECODER_FLAGS] = static_cast<jint>(decoderInfo.decoder_flags);

    jfloat floatValues[F_COUNT] = {0.0f};
    for (int i = 0; i < 4; ++i) {
        floatValues[F_CAM_MUL + i] = cd.cam_mul[i];
        floatValues[F_PRE_MUL + i] = cd.pre_mul[i];
    }
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 3; ++col) {
            floatValues[F_CAM_XYZ + row * 3 + col] = cd.cam_xyz[row][col];
        }
    }
    floatValues[F_ISO] = ot.iso_speed;
    floatValues[F_SHUTTER] = ot.shutter;
    floatValues[F_APERTURE] = ot.aperture;
    floatValues[F_FOCAL] = ot.focal_len;

    jlong longValues[L_COUNT] = {0};
    longValues[L_TIMESTAMP] = static_cast<jlong>(ot.timestamp);

    env->SetIntArrayRegion(ints, 0, I_COUNT, intValues);
    env->SetFloatArrayRegion(floats, 0, F_COUNT, floatValues);
    env->SetLongArrayRegion(longs, 0, L_COUNT, longValues);

    putString(env, strings, S_MAKE, newFixedString(env, id.make, sizeof(id.make)));
    putString(env, strings, S_MODEL, newFixedString(env, id.model, sizeof(id.model)));
    putString(env, strings, S_NORM_MAKE,
              newFixedString(env, id.normalized_make, sizeof(id.normalized_make)));
    putString(env, strings, S_NORM_MODEL,
              newFixedString(env, id.normalized_model, sizeof(id.normalized_model)));
    putString(env, strings, S_SOFTWARE,
              newFixedString(env, id.software, sizeof(id.software)));
    putString(env, strings, S_CDESC, newFixedString(env, id.cdesc, sizeof(id.cdesc)));
    putString(env, strings, S_UNPACK, newString(env, lr->unpack_function_name()));
    putString(env, strings, S_VERSION, newString(env, LibRaw::version()));

    jclass objectCls = env->FindClass("java/lang/Object");
    jobjectArray result = env->NewObjectArray(4, objectCls, nullptr);
    env->SetObjectArrayElement(result, 0, ints);
    env->SetObjectArrayElement(result, 1, floats);
    env->SetObjectArrayElement(result, 2, strings);
    env->SetObjectArrayElement(result, 3, longs);

    env->DeleteLocalRef(objectCls);
    env->DeleteLocalRef(stringCls);
    env->DeleteLocalRef(ints);
    env->DeleteLocalRef(floats);
    env->DeleteLocalRef(longs);
    env->DeleteLocalRef(strings);
    return result;
}

// 内嵌预览图（多数相机是 1600px 级的 JPEG），几十毫秒出图，
// 用于「打开即可见」，完整解码留给用户显式触发。
JNIEXPORT jobject JNICALL
Java_com_hifn_pixelcake_raw_RawNative_nativeEmbeddedThumbnail(JNIEnv* env, jobject /*thiz*/,
                                                              jlong handle) {
    RawSession* session = asSession(handle);
    if (session == nullptr) {
        throwIo(env, "句柄为空");
        return nullptr;
    }
    // 强制重新打开：unpack_thumb 会重新分配 thumb 缓冲，
    // 不先 recycle 就会泄漏上一次的分配。
    if (!rearm(session)) {
        throwIo(env, "重新打开 RAW 失败");
        return nullptr;
    }

    LibRaw* lr = session->lr;
    int rc = lr->unpack_thumb();
    if (rc != LIBRAW_SUCCESS) {
        throwIo(env, "该 RAW 没有可用的内嵌预览图", rc);
        return nullptr;
    }

    int thumbErr = 0;
    libraw_processed_image_t* thumb = lr->dcraw_make_mem_thumb(&thumbErr);
    if (thumb == nullptr) {
        throwIo(env, "提取内嵌预览图失败", thumbErr);
        return nullptr;
    }

    jobject bitmap = nullptr;
    if (thumb->type == LIBRAW_IMAGE_JPEG) {
        jbyteArray bytes = env->NewByteArray(static_cast<jsize>(thumb->data_size));
        if (bytes != nullptr) {
            env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(thumb->data_size),
                                    reinterpret_cast<const jbyte*>(thumb->data));
            jclass factory = env->FindClass("android/graphics/BitmapFactory");
            jmethodID decode = env->GetStaticMethodID(
                factory, "decodeByteArray", "([BII)Landroid/graphics/Bitmap;");
            bitmap = env->CallStaticObjectMethod(factory, decode, bytes, 0,
                                                 static_cast<jint>(thumb->data_size));
            env->DeleteLocalRef(factory);
            env->DeleteLocalRef(bytes);
        }
    } else if (thumb->type == LIBRAW_IMAGE_BITMAP) {
        bitmap = downsampleToBitmap(env, thumb, 0);
    } else {
        throwIo(env, "内嵌预览图格式不受支持");
    }

    LibRaw::dcraw_clear_mem(thumb);
    // 释放 thumb 缓冲，避免长时间持有
    lr->recycle();
    session->armed = false;
    return bitmap;
}

// 完整解码：unpack → dcraw_process（去马赛克 + 白平衡 + 色彩矩阵 + gamma）→ 降采样。
// 33MP 单线程 AHD 需要数秒，必须放在后台线程调用。
JNIEXPORT jobject JNICALL
Java_com_hifn_pixelcake_raw_RawNative_nativeRender(JNIEnv* env, jobject /*thiz*/,
                                                   jlong handle, jint maxDim,
                                                   jboolean halfSize) {
    RawSession* session = asSession(handle);
    if (session == nullptr) {
        throwIo(env, "句柄为空");
        return nullptr;
    }
    if (!rearm(session)) {
        throwIo(env, "重新打开 RAW 失败");
        return nullptr;
    }

    LibRaw* lr = session->lr;
    libraw_output_params_t& params = lr->imgdata.params;

    // 相机白平衡 + 相机色彩矩阵 + sRGB 输出。
    // 第 3 步会改成线性 16bit（no_auto_bright=1, gamma 直通），
    // 把色调映射和色彩空间转换交给 GPU 上的 fp16 管线做。
    params.use_camera_wb = 1;
    params.use_auto_wb = 0;
    params.use_camera_matrix = 1;
    params.output_color = LIBRAW_COLORSPACE_sRGB;
    params.output_bps = 8;
    params.no_auto_bright = 0;
    params.highlight = 0;
    params.half_size = (halfSize == JNI_TRUE) ? 1 : 0;
    params.user_qual = kQualityAhd;

    int rc = lr->unpack();
    if (rc != LIBRAW_SUCCESS) {
        throwIo(env, "RAW 解包失败", rc);
        lr->recycle();
        session->armed = false;
        return nullptr;
    }

    rc = lr->dcraw_process();
    if (rc != LIBRAW_SUCCESS) {
        throwIo(env, "去马赛克/色彩处理失败", rc);
        lr->recycle();
        session->armed = false;
        return nullptr;
    }

    int imageErr = 0;
    libraw_processed_image_t* image = lr->dcraw_make_mem_image(&imageErr);
    if (image == nullptr) {
        throwIo(env, "生成输出图像失败", imageErr);
        lr->recycle();
        session->armed = false;
        return nullptr;
    }

    jobject bitmap = downsampleToBitmap(env, image, maxDim);
    LibRaw::dcraw_clear_mem(image);

    // 释放数百 MB 的中间缓冲。副作用是 imgdata 被清零，
    // 所以把 armed 置 false，下次操作会重新 open_buffer。
    lr->recycle();
    session->armed = false;

    if (bitmap == nullptr && !env->ExceptionCheck()) {
        throwIo(env, "生成预览位图失败");
    }
    return bitmap;
}

}  // extern "C"
