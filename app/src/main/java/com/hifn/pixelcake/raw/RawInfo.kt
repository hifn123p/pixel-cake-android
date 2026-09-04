package com.hifn.pixelcake.raw

/**
 * 一次 RAW 会话的关键元数据。
 *
 * 字段在 open() 时一次性抓取：之后调用 render() 会 recycle LibRaw 的内部缓冲，
 * 再向 LibRaw 查询尺寸/色彩数据只会拿到 0。
 */
data class RawInfo(
    // 机身标识
    val make: String,
    val model: String,
    val normalizedMake: String,
    val normalizedModel: String,
    val software: String,
    // 传感器与 RAW 结构
    /** Bayer 排列描述，如 "RGBG" */
    val cdesc: String,
    val colors: Int,
    /** Bayer 滤色片布局位域，非 Bayer（X-Trans 等）时 > 1000 */
    val filters: Int,
    val rawCount: Int,
    val isFoveon: Boolean,
    // 尺寸
    val rawWidth: Int,
    val rawHeight: Int,
    /** 去马赛克后的输出尺寸 */
    val width: Int,
    val height: Int,
    val iWidth: Int,
    val iHeight: Int,
    val rawPitch: Int,
    /** 方向翻转位，0/3..7，供输出时旋转 */
    val flip: Int,
    // 电平与位深
    val black: Int,
    val maximum: Int,
    /** 每像素位数。A7C2 的 ARW 是 14 */
    val rawBps: Int,
    // 色彩
    /** 相机白平衡系数 R,G1,B,G2 */
    val camMul: FloatArray,
    /** 预设白平衡系数 */
    val preMul: FloatArray,
    /** 相机 → XYZ 矩阵，行主序 4x3，共 12 个 */
    val camXyz: FloatArray,
    val exifColorSpace: Int,
    // 拍摄参数
    val iso: Float,
    val shutter: Float,
    val aperture: Float,
    val focalLength: Float,
    val timestampSec: Long,
    // 内嵌预览
    val thumbFormat: Int,
    val thumbWidth: Int,
    val thumbHeight: Int,
    // 解码器
    val unpackFunction: String,
    val decoderFlags: Int,
    val librawVersion: String
)

/**
 * 把 native 侧返回的扁平数组解成 [RawInfo]。
 *
 * 之所以走「扁平数组 + 具名下标」而不是让 JNI 直接构造 data class，
 * 是因为后者需要手写 35 个参数的 JNI 方法签名 —— 一个字符写错就是运行期
 * NoSuchMethodError。扁平数组把契约压缩成一组下标常量，两侧各持一份，
 * 改动时对照一眼即可。
 */
internal object RawInfoCodec {

    // ===== IntArray 下标，必须与 raw_bridge.cpp 的 I_* 常量一致 =====
    private const val I_COLORS = 0
    private const val I_FILTERS = 1
    private const val I_RAW_COUNT = 2
    private const val I_FOVEON = 3
    private const val I_RAW_WIDTH = 4
    private const val I_RAW_HEIGHT = 5
    private const val I_WIDTH = 6
    private const val I_HEIGHT = 7
    private const val I_IWIDTH = 8
    private const val I_IHEIGHT = 9
    private const val I_RAW_PITCH = 10
    private const val I_FLIP = 11
    private const val I_BLACK = 12
    private const val I_MAXIMUM = 13
    private const val I_RAW_BPS = 14
    private const val I_COLORSPACE = 15
    private const val I_THUMB_FORMAT = 16
    private const val I_THUMB_WIDTH = 17
    private const val I_THUMB_HEIGHT = 18
    private const val I_DECODER_FLAGS = 19

    // ===== FloatArray 下标，必须与 raw_bridge.cpp 的 F_* 常量一致 =====
    private const val F_CAM_MUL = 0
    private const val F_PRE_MUL = 4
    private const val F_CAM_XYZ = 8
    private const val F_ISO = 20
    private const val F_SHUTTER = 21
    private const val F_APERTURE = 22
    private const val F_FOCAL = 23

    // ===== Array<String> 下标，必须与 raw_bridge.cpp 的 S_* 常量一致 =====
    private const val S_MAKE = 0
    private const val S_MODEL = 1
    private const val S_NORM_MAKE = 2
    private const val S_NORM_MODEL = 3
    private const val S_SOFTWARE = 4
    private const val S_CDESC = 5
    private const val S_UNPACK = 6
    private const val S_VERSION = 7

    // ===== LongArray 下标，必须与 raw_bridge.cpp 的 L_* 常量一致 =====
    private const val L_TIMESTAMP = 0

    private const val BUNDLE_INTS = 0
    private const val BUNDLE_FLOATS = 1
    private const val BUNDLE_STRINGS = 2
    private const val BUNDLE_LONGS = 3

    @Suppress("UNCHECKED_CAST")
    fun decode(bundle: Array<Any>): RawInfo {
        val ints = bundle[BUNDLE_INTS] as IntArray
        val floats = bundle[BUNDLE_FLOATS] as FloatArray
        val strings = bundle[BUNDLE_STRINGS] as Array<String>
        val longs = bundle[BUNDLE_LONGS] as LongArray

        return RawInfo(
            make = strings[S_MAKE],
            model = strings[S_MODEL],
            normalizedMake = strings[S_NORM_MAKE],
            normalizedModel = strings[S_NORM_MODEL],
            software = strings[S_SOFTWARE],
            cdesc = strings[S_CDESC],
            colors = ints[I_COLORS],
            filters = ints[I_FILTERS],
            rawCount = ints[I_RAW_COUNT],
            isFoveon = ints[I_FOVEON] != 0,
            rawWidth = ints[I_RAW_WIDTH],
            rawHeight = ints[I_RAW_HEIGHT],
            width = ints[I_WIDTH],
            height = ints[I_HEIGHT],
            iWidth = ints[I_IWIDTH],
            iHeight = ints[I_IHEIGHT],
            rawPitch = ints[I_RAW_PITCH],
            flip = ints[I_FLIP],
            black = ints[I_BLACK],
            maximum = ints[I_MAXIMUM],
            rawBps = ints[I_RAW_BPS],
            camMul = floats.copyOfRange(F_CAM_MUL, F_CAM_MUL + 4),
            preMul = floats.copyOfRange(F_PRE_MUL, F_PRE_MUL + 4),
            camXyz = floats.copyOfRange(F_CAM_XYZ, F_CAM_XYZ + 12),
            exifColorSpace = ints[I_COLORSPACE],
            iso = floats[F_ISO],
            shutter = floats[F_SHUTTER],
            aperture = floats[F_APERTURE],
            focalLength = floats[F_FOCAL],
            timestampSec = longs[L_TIMESTAMP],
            thumbFormat = ints[I_THUMB_FORMAT],
            thumbWidth = ints[I_THUMB_WIDTH],
            thumbHeight = ints[I_THUMB_HEIGHT],
            unpackFunction = strings[S_UNPACK],
            decoderFlags = ints[I_DECODER_FLAGS],
            librawVersion = strings[S_VERSION]
        )
    }
}
