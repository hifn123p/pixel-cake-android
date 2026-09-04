# 把 LibRaw 编译成静态库供 pixelcake_raw 链接。
#
# LibRaw 0.22 的主力构建是 autotools（Makefile.dist），仓库根没有 CMakeLists.txt，
# buildfiles/ 里那套 CMake 是次要路径、在 Android 交叉编译时容易踩 find_package 误判宿主机库。
# 因此这里不用它，改为直接按官方 Makefile.dist 的 LIB_OBJECTS 清单自行组织源文件。
#
# 清单来自 LibRaw 0.22.2 的 Makefile.dist，共 79 个目标。
# src/ 下实际有 82 个 .cpp，多出的 3 个是 *_ph.cpp 占位空文件，必须排除。

set(LIBRAW_OBJECT_NAMES
    # core
    libraw_datastream
    libraw_c_api
    # tables
    cameralist
    colorconst
    colordata
    wblists
    # decoders
    canon_600
    crx
    decoders_dcraw
    decoders_libraw
    decoders_libraw_dcrdefs
    dng
    fp_dng
    fuji_compressed
    generic
    kodak_decoders
    load_mfbacks
    olympus14
    pana8
    smal
    sonycc
    unpack
    unpack_thumb
    # decompressors
    losslessjpeg
    # demosaic
    aahd_demosaic
    ahd_demosaic
    dcb_demosaic
    dht_demosaic
    misc_demosaic
    xtrans_demosaic
    # 可选后端的胶水层（未启用 RawSpeed / DNG SDK，但符号需要存在）
    dngsdk_glue
    rawspeed_glue
    # metadata
    adobepano
    canon
    ciff
    cr3_parser
    epson
    exif_gps
    fuji
    hasselblad_model
    identify
    identify_tools
    kodak
    leica
    makernotes
    mediumformat
    minolta
    misc_parsers
    nikon
    normalize_model
    olympus
    p1
    pentax
    samsung
    sony
    tiff
    # postprocessing
    aspect_ratio
    dcraw_process
    mem_image
    postprocessing_aux
    postprocessing_utils
    postprocessing_utils_dcrdefs
    # preprocessing
    ext_preprocess
    raw2image
    subtract_black
    # utils
    curves
    decoder_info
    init_close_utils
    open
    phaseone_processing
    read_utils
    thumb_utils
    utils_dcraw
    utils_libraw
    # write
    apply_profile
    file_write
    tiff_writer
    # x3f
    x3f_parse_process
    x3f_utils_patched
)

file(GLOB_RECURSE _libraw_all_sources "${LIBRAW_ROOT}/src/*.cpp")
list(LENGTH _libraw_all_sources _libraw_all_count)

set(LIBRAW_SOURCES "")
foreach(_src IN LISTS _libraw_all_sources)
    get_filename_component(_name "${_src}" NAME_WE)
    if(_name IN_LIST LIBRAW_OBJECT_NAMES)
        list(APPEND LIBRAW_SOURCES "${_src}")
    endif()
endforeach()

list(LENGTH LIBRAW_SOURCES _libraw_selected_count)
message(STATUS "LibRaw: 源文件 ${_libraw_selected_count}/${_libraw_all_count}")

# 数量对不上说明 LibRaw 版本变了，宁可让构建失败也不要悄悄编出残缺的库
if(NOT _libraw_selected_count EQUAL 79)
    message(FATAL_ERROR
        "LibRaw 源文件筛选得到 ${_libraw_selected_count} 个，期望 79 个。\n"
        "submodule 可能不在 0.22.2，请核对 third_party/libraw 的版本。")
endif()

add_library(libraw_static STATIC ${LIBRAW_SOURCES})

# LibRaw 的头文件以 <libraw/libraw.h> 形式引用，因此包含根要指向源码根
target_include_directories(libraw_static PUBLIC "${LIBRAW_ROOT}")

# -w：第三方代码，不必让它的告警淹没 CI 日志
# -fno-strict-aliasing：dcraw 血缘代码有类型双关，开严格别名优化会出错
# -fexceptions：LibRaw 用 C++ 异常（LIBRAW_THROW）传递错误
target_compile_options(libraw_static PRIVATE
    -w
    -fexceptions
    -fno-strict-aliasing)

# USE_ZLIB：浮点 DNG 的 deflate 解压需要。Sony ARW 用不到，但代价极低
find_package(ZLIB)
if(ZLIB_FOUND)
    message(STATUS "LibRaw: ZLIB = ON")
    target_compile_definitions(libraw_static PUBLIC USE_ZLIB)
    target_link_libraries(libraw_static PUBLIC ZLIB::ZLIB)
else()
    message(STATUS "LibRaw: ZLIB = OFF")
endif()

# OpenMP：去马赛克可并行。33MP 上差别是数秒 vs 亚秒级，值得开。
# NDK 上万一探测不到就退回单线程，不让构建失败。
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "LibRaw: OpenMP = ON")
    target_link_libraries(libraw_static PUBLIC OpenMP::OpenMP_CXX)
    # LibRaw 会检查编译器版本决定是否启用 OpenMP，NDK clang 可能不被认出来
    target_compile_definitions(libraw_static PUBLIC LIBRAW_FORCE_OPENMP)
else()
    message(STATUS "LibRaw: OpenMP = OFF（回退单线程）")
    target_compile_definitions(libraw_static PUBLIC LIBRAW_NOTHREADS)
endif()
