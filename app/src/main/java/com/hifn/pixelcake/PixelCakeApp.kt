package com.hifn.pixelcake

import android.app.Application

/**
 * Application 入口。
 *
 * 当前为空实现 —— 第 2 步起在此初始化：
 *   - LibRaw native 库加载（System.loadLibrary）
 *   - 模型下载器与本地缓存目录
 *   - 全局异常捕获与日志落盘
 */
class PixelCakeApp : Application() {

    override fun onCreate() {
        super.onCreate()
    }
}
