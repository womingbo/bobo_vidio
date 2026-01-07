
// videothread.cpp
#include "videothread.h"
#include <QDebug>
#include <QElapsedTimer>

VideoThread::VideoThread(QObject* parent)
    : QObject(parent)
{
    // 创建播放定时器
    playTimer = new QTimer(this);
    playTimer->setTimerType(Qt::PreciseTimer);  // 精确计时器
    connect(playTimer, &QTimer::timeout, this, &VideoThread::onPlayTimerTimeout);
}

VideoThread::~VideoThread()
{
    cleanupSDL();
    cleanup();

    // 销毁窗口
    if (sdlWindow) {
        SDL_DestroyWindow(sdlWindow);
        sdlWindow = nullptr;
    }

    SDL_Quit();
}

void VideoThread::init_video(QString currentVideoFile)
{

    cleanupSDL();
    cleanup();

    // 打开视频文件上下文
    int ret = avformat_open_input(&videoFormatCtx,
                                  currentVideoFile.toUtf8().constData(),
                                  NULL, NULL);
    VideoFile = currentVideoFile;
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qDebug() <<"无法打开视频文件！";
        return;
    }

    // 获取流信息
    ret = avformat_find_stream_info(videoFormatCtx, NULL);
    if (ret < 0) {
        qDebug() <<"无法获取流信息！";
        cleanup();
        return;
    }

    //查找视频流
    videoStreamIndex = findVideoStream(videoFormatCtx);
    if (videoStreamIndex == -1) {
        qDebug() <<"未找到视频流！";
        cleanup();
        return;
    }


    // 7. 显示视频信息
    //  showVideoInfo();

    // 8. 初始化视频解码器
    if (!initVideoDecoder()) {
        qDebug() <<"视频解码器初始化失败！";
        cleanup();
        return;
    }

    // 9. 初始化SDL显示
    if (!initSDLDisplay()) {
        qDebug() <<"SDL显示初始化失败！";
        cleanup();
        return;
    }

    total_time = videoFormatCtx->duration / (double)AV_TIME_BASE;
    emit UpadatseekSlider(total_time);
    startPlayback();
}

void VideoThread::setPlaybackSpeed(float speed)
{
    speed = qBound(0.25f, speed, 4.0f);

    if (qFuzzyCompare(speed, m_currentSpeed)) {
        return;
    }

    m_currentSpeed = speed;

    // 如果正在播放，更新定时器间隔
    if (GlobalVars::playerState == STATE_PLAYING  && playTimer && playTimer->isActive()) {
        updateTimerInterval();
    }
}

void VideoThread::updateTimerInterval()
{
    if (!videoFormatCtx || videoStreamIndex < 0) {
        return;
    }

    AVStream* videoStream = videoFormatCtx->streams[videoStreamIndex];
    double frameRate = av_q2d(videoStream->avg_frame_rate);
    if (frameRate <= 0) frameRate = 30.0;

    int interval = static_cast<int>(1000 / (frameRate * m_currentSpeed));
    interval = qMax(1, interval);

    if (playTimer) {
        playTimer->stop();
        playTimer->start(interval);
    }
}

void VideoThread::startPlayback()
{
    // 确保在文件开头
    resetToBeginning();

    // 启动定时器
    AVStream* videoStream = videoFormatCtx->streams[videoStreamIndex];
    double frameRate = av_q2d(videoStream->avg_frame_rate);
    if (frameRate <= 0) frameRate = 30.0;

    //  int interval = 1000 / frameRate;
    int interval = static_cast<int>(1000 / (frameRate * m_currentSpeed));
    interval = qMax(1, interval);

    if (playTimer) {
        playTimer->start(interval);
        qDebug() << "定时器启动，间隔：" << interval << "ms," << "videoStreamIndex:" <<videoStreamIndex;
    }
    emit UpadatButton(true);
    GlobalVars::playerState = STATE_PLAYING;
}

void VideoThread::pausePlayback()
{
    qDebug() << "暂停播放";

    if (playTimer && playTimer->isActive()) {
        playTimer->stop();
    }
}

void VideoThread::resumePlayback()
{
    qDebug() << "继续播放";
    if (playTimer) {
        playTimer->start();
    }
}

void VideoThread::restartPlayback()
{
    resetToBeginning();

    if (playTimer) {
        playTimer->start();
    }

    GlobalVars::playerState = STATE_PLAYING;
}

void VideoThread::onPlayFinished()
{
    qDebug() << "播放结束";
    if (playTimer && playTimer->isActive()) {
        playTimer->stop();
    }
}

void VideoThread::resetToBeginning()
{
    if (videoFormatCtx && videoStreamIndex >= 0) {
        av_seek_frame(videoFormatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
        qDebug() << "重置到视频开头";
        if (videoCodecCtx) {
            qDebug() << "重置!";
            avcodec_flush_buffers(videoCodecCtx);
        }
    }
}

bool VideoThread::initVideoDecoder()
{
    qDebug() << "开始初始化视频解码器...";

    if (!videoFormatCtx || videoStreamIndex < 0) {
        qDebug() << "错误：视频格式上下文未初始化";
        return false;
    }

    // 1. 获取视频流
    AVStream* videoStream = videoFormatCtx->streams[videoStreamIndex];
    AVCodecParameters* codecPar = videoStream->codecpar;


    // 2. 查找解码器
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        qDebug() << "错误：找不到对应的解码器";

        void* opaque = NULL;
        const AVCodec* c = NULL;
        while ((c = av_codec_iterate(&opaque))) {
            if (c->type == AVMEDIA_TYPE_VIDEO && av_codec_is_decoder(c)) {
                qDebug() << "  " << c->name << " - " << c->long_name;
            }
        }
        return false;
    }

    qDebug() << "找到解码器：" << codec->name << " (" << codec->long_name << ")";

    // 3. 创建解码器上下文
    videoCodecCtx = avcodec_alloc_context3(codec);
    if (!videoCodecCtx) {
        qDebug() << "错误：无法分配解码器上下文";
        return false;
    }

    // 4. 复制编解码器参数到上下文
    int ret = avcodec_parameters_to_context(videoCodecCtx, codecPar);
    if (ret < 0) {
        qDebug() << "错误：无法复制编解码器参数，错误码：" << ret;
        avcodec_free_context(&videoCodecCtx);
        return false;
    }

    // 5. 打开解码器
    ret = avcodec_open2(videoCodecCtx, codec, NULL);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qDebug() << "错误：无法打开解码器：" << errbuf;
        avcodec_free_context(&videoCodecCtx);
        return false;
    }

    // 6. 创建YUV帧（解码输出）
    videoFrameYUV = av_frame_alloc();
    if (!videoFrameYUV) {
        qDebug() << "错误：无法分配YUV帧";
        avcodec_free_context(&videoCodecCtx);
        return false;
    }

    // 7. 创建RGB帧（显示用）
    videoFrameRGB = av_frame_alloc();
    if (!videoFrameRGB) {
        qDebug() << "错误：无法分配RGB帧";
        av_frame_free(&videoFrameYUV);
        avcodec_free_context(&videoCodecCtx);
        return false;
    }

    // 8. 为RGB帧分配内存
    int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24,
                                              videoCodecCtx->width,
                                              videoCodecCtx->height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(bufferSize);
    if (!buffer) {
        qDebug() << "错误：无法为RGB帧分配内存";
        av_frame_free(&videoFrameRGB);
        av_frame_free(&videoFrameYUV);
        avcodec_free_context(&videoCodecCtx);
        return false;
    }

    // 9. 设置RGB帧参数并关联内存
    ret = av_image_fill_arrays(videoFrameRGB->data, videoFrameRGB->linesize,
                               buffer, AV_PIX_FMT_RGB24,
                               videoCodecCtx->width, videoCodecCtx->height, 1);
    if (ret < 0) {
        qDebug() << "错误：无法填充RGB帧数组";
        av_freep(&buffer);
        av_frame_free(&videoFrameRGB);
        av_frame_free(&videoFrameYUV);
        avcodec_free_context(&videoCodecCtx);
        return false;
    }

    // 设置RGB帧的宽度和高度
    videoFrameRGB->width = videoCodecCtx->width;
    videoFrameRGB->height = videoCodecCtx->height;
    videoFrameRGB->format = AV_PIX_FMT_RGB24;

    // 10. 创建颜色空间转换器（YUV -> RGB）
    videoSwsCtx = sws_getContext(
                videoCodecCtx->width, videoCodecCtx->height, videoCodecCtx->pix_fmt,
                videoCodecCtx->width, videoCodecCtx->height, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, NULL, NULL, NULL
                );

    if (!videoSwsCtx) {
        qDebug() << "错误：无法创建图像缩放转换器";
        av_freep(&buffer);
        av_frame_free(&videoFrameRGB);
        av_frame_free(&videoFrameYUV);
        avcodec_free_context(&videoCodecCtx);
        return false;
    }

    // 11. 创建数据包
    videoPacket = av_packet_alloc();

    av_read_frame(videoFormatCtx, videoPacket);

    if (videoPacket->pts != AV_NOPTS_VALUE) {
        double packet_time = videoPacket->pts * av_q2d(videoStream->time_base);
        qDebug() << "数据包PTS时间：" << packet_time << "秒";
    }

    if (videoPacket->dts != AV_NOPTS_VALUE) {
        double decode_time = videoPacket->dts * av_q2d(videoStream->time_base);
        qDebug() << "数据包DTS时间：" << decode_time << "秒";
    }

    if (!videoPacket) {
        qDebug() << "错误：无法分配数据包";
        sws_freeContext(videoSwsCtx);
        av_freep(&buffer);
        av_frame_free(&videoFrameRGB);
        av_frame_free(&videoFrameYUV);
        avcodec_free_context(&videoCodecCtx);
        return false;
    }
    // 检查是否是硬件解码
    if (videoCodecCtx->hw_device_ctx) {
        qDebug() << "  硬件加速：已启用";
    } else {
        qDebug() << "  硬件加速：未启用（软件解码）";
    }

    return true;
}

bool VideoThread::initSDLDisplay()
{
    qDebug() << "开始初始化SDL显示...";

    //缺少：初始化SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        qDebug() << "SDL初始化失败:" << SDL_GetError();
        return false;
    }

    // 2. 检查是否有视频解码器信息
    if (!videoCodecCtx) {
        qDebug() << "错误：视频解码器未初始化";
        return false;
    }

    // 3. 获取Qt视频显示部件的窗口ID
    if (!widgetId) {
        qDebug() << "错误：无法获取Qt视频部件的窗口ID";
        return false;
    }

    qDebug() << "Qt视频部件窗口ID：" << widgetId;

    // 4. 创建SDL窗口（嵌入到Qt窗口中）
    sdlWindow = SDL_CreateWindowFrom((void*)widgetId);
    if (!sdlWindow) {
        qDebug() << "错误：创建SDL窗口失败：" << SDL_GetError();
        return false;
    }

    // 5. 创建SDL渲染器
    // 尝试使用硬件加速渲染器
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1,
                                     SDL_RENDERER_ACCELERATED |
                                     SDL_RENDERER_PRESENTVSYNC);

    // 如果硬件加速失败，尝试软件渲染器
    if (!sdlRenderer) {
        qDebug() << "硬件加速渲染器创建失败，尝试软件渲染：" << SDL_GetError();
        sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_SOFTWARE);
    }

    if (!sdlRenderer) {
        qDebug() << "错误：创建SDL渲染器失败：" << SDL_GetError();
        //    cleanupSDL();
        return false;
    }

    qDebug() << "✅ SDL渲染器创建成功";

    // 6. 设置渲染器属性
    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);  // 黑色背景
    SDL_RenderClear(sdlRenderer);
    SDL_RenderPresent(sdlRenderer);

    // 设置纹理缩放质量
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    // 7. 获取渲染器信息用于调试
    SDL_RendererInfo rendererInfo;
    SDL_GetRendererInfo(sdlRenderer, &rendererInfo);

    // 8. 检查渲染器是否支持需要的功能
    bool hasAcceleration = (rendererInfo.flags & SDL_RENDERER_ACCELERATED) != 0;
    bool hasVSync = (rendererInfo.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    return true;
}

void VideoThread::setDisplayWidget(QWidget *widget)
{
    m_displayWidget = widget;
    if (widget) {
        widgetId= widget->winId();
        qDebug() << "设置显示窗口，句柄：" << widgetId;
    }
}

int VideoThread::findVideoStream(AVFormatContext* formatCtx)
{
    for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVStream* stream = formatCtx->streams[i];
            qDebug() << "找到视频流 #" << i;
            qDebug() << "  编码器：" << avcodec_get_name(stream->codecpar->codec_id);
            qDebug() << "  分辨率：" << stream->codecpar->width << "x" << stream->codecpar->height;
            qDebug() << "  帧率：" << av_q2d(stream->avg_frame_rate) << "fps";
            return i;
        }
    }
    return -1;
}

void VideoThread::showVideoInfo()
{
    if (!videoFormatCtx || videoStreamIndex < 0) {
        qDebug() << "错误：无法显示视频信息，上下文未初始化";
        return;
    }

    AVStream* videoStream = videoFormatCtx->streams[videoStreamIndex];
    AVCodecParameters* codecPar = videoStream->codecpar;

    // 1. 收集基本信息
    QString infoText;

    infoText += QString("📁 文件信息\n");
    infoText += QString("  格式: %1\n").arg(videoFormatCtx->iformat->name);
    infoText += QString("  时长: %1\n").arg(formatTime(videoFormatCtx->duration / AV_TIME_BASE));
    infoText += QString("  总比特率: %1 kbps\n").arg(videoFormatCtx->bit_rate / 1000);

    infoText += QString("\n🎬 视频流信息\n");
    infoText += QString("  编码格式: %1\n").arg(avcodec_get_name(codecPar->codec_id));
    infoText += QString("  分辨率: %1 x %2\n").arg(codecPar->width).arg(codecPar->height);

    // 帧率计算（可能有多种表示方式）
    double frameRate = 0;
    if (videoStream->avg_frame_rate.num > 0 && videoStream->avg_frame_rate.den > 0) {
        frameRate = av_q2d(videoStream->avg_frame_rate);
        infoText += QString("  平均帧率: %1 fps\n").arg(QString::number(frameRate, 'f', 2));
    }
    if (videoStream->r_frame_rate.num > 0 && videoStream->r_frame_rate.den > 0) {
        double rFrameRate = av_q2d(videoStream->r_frame_rate);
        infoText += QString("  真实帧率: %1 fps\n").arg(QString::number(rFrameRate, 'f', 2));
    }

    // 总帧数
    if (videoStream->nb_frames > 0) {
        infoText += QString("  总帧数: %1\n").arg(videoStream->nb_frames);
    }

    // 像素格式
    const char* pixFmtName = av_get_pix_fmt_name((AVPixelFormat)codecPar->format);
    infoText += QString("  像素格式: %1\n").arg(pixFmtName ? pixFmtName : "未知");

    // 比特率
    if (codecPar->bit_rate > 0) {
        infoText += QString("  视频比特率: %1 kbps\n").arg(codecPar->bit_rate / 1000);
    }

    // 编码信息
    infoText += QString("\n🔧 编码信息\n");
    if (videoStream->codecpar->profile != FF_PROFILE_UNKNOWN) {
        const char* profileName = avcodec_profile_name(codecPar->codec_id, codecPar->profile);
        infoText += QString("  编码档次: %1\n").arg(profileName ? profileName : "未知");
    }

    if (videoStream->codecpar->level > 0) {
        infoText += QString("  编码级别: %1\n").arg(videoStream->codecpar->level / 10.0);
    }

    // 2. 控制台输出（详细调试信息）
    qDebug() << "=== 视频详细信息 ===";
    qDebug() << "文件格式:" << videoFormatCtx->iformat->name;
    qDebug() << "总时长:" << videoFormatCtx->duration / AV_TIME_BASE << "秒";
    qDebug() << "视频流索引:" << videoStreamIndex;
    qDebug() << "编码器:" << avcodec_get_name(codecPar->codec_id);
    qDebug() << "分辨率:" << codecPar->width << "x" << codecPar->height;
    qDebug() << "像素格式:" << (pixFmtName ? pixFmtName : "未知");
    qDebug() << "帧率:" << frameRate << "fps";
    qDebug() << "总帧数:" << videoStream->nb_frames;
    qDebug() << "时间基:" << videoStream->time_base.num << "/" << videoStream->time_base.den;

    // 显示一些时间相关信息
    if (videoStream->start_time != AV_NOPTS_VALUE) {
        qDebug() << "开始时间:" << videoStream->start_time * av_q2d(videoStream->time_base) << "秒";
    }

    qDebug() << "✅ 视频信息显示完成";
}

QString VideoThread::formatTime(qint64 seconds)
{
    qint64 hours = seconds / 3600;
    qint64 minutes = (seconds % 3600) / 60;
    qint64 secs = seconds % 60;

    if (hours > 0) {
        return QString("%1:%2:%3")
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(secs, 2, 10, QChar('0'));
    } else {
        return QString("%1:%2")
                .arg(minutes, 2, 10, QChar('0'))
                .arg(secs, 2, 10, QChar('0'));
    }
}

void VideoThread::onPlayTimerTimeout()
{
    // 检查状态
    if (GlobalVars::playerState != STATE_PLAYING) {
        return;
    }

    // 检查解码器
    if (!videoFormatCtx || !videoCodecCtx) {
        qDebug() << "错误：解码器未初始化";
        return;
    }

    // 最多尝试3个数据包（防止卡住）
    for (int attempt = 0; attempt < 3; attempt++) {
        // 1. 读取一个数据包
        int ret = av_read_frame(videoFormatCtx, videoPacket);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                qDebug() << "视频播放结束！";


                int totalMin = (int)total_time / 60;
                int totalSec = (int)total_time % 60;

                QString timeText = QString("%1:%2 / %3:%4")
                        .arg(totalMin, 2, 10, QChar('0'))
                        .arg(totalSec, 2, 10, QChar('0'))
                        .arg(totalMin, 2, 10, QChar('0'))
                        .arg(totalSec, 2, 10, QChar('0'));
                emit UpadatStatus(timeText,total_time  * 1000);
                emit UpadatButton(false);
                GlobalVars::playerState = STATE_ENDED;
                playTimer->stop();

            }
            return;
        }

        // 2. 检查是否是视频包
        if (videoPacket->stream_index == videoStreamIndex) {
            // 3. 发送给解码器
            ret = avcodec_send_packet(videoCodecCtx, videoPacket);
            av_packet_unref(videoPacket);

            if (ret < 0) {
                continue;
            }

            // 4. 接收解码后的帧
            ret = avcodec_receive_frame(videoCodecCtx, videoFrameYUV);
            if (ret >= 0) {

                AVStream* videoStream = videoFormatCtx->streams[videoStreamIndex];
                double currentTime = 0;


                if (videoFrameYUV->pts != AV_NOPTS_VALUE) {
                    currentTime = videoFrameYUV->pts * av_q2d(videoStream->time_base);
                }

                else {
                    static int frameCount = 0;
                    frameCount++;
                    double fps = av_q2d(videoStream->avg_frame_rate);
                    if (fps > 0) {
                        currentTime = frameCount / fps;
                    }
                }

                // 格式化为 分:秒
                int currentMin = (int)currentTime / 60;
                int currentSec = (int)currentTime % 60;
                int totalMin = (int)total_time / 60;
                int totalSec = (int)total_time % 60;

                QString timeText = QString("%1:%2 / %3:%4")
                        .arg(currentMin, 2, 10, QChar('0'))
                        .arg(currentSec, 2, 10, QChar('0'))
                        .arg(totalMin, 2, 10, QChar('0'))
                        .arg(totalSec, 2, 10, QChar('0'));

                emit UpadatStatus(timeText,currentTime  * 1000);

                // 5. 转换YUV到RGB
                sws_scale(videoSwsCtx,
                          videoFrameYUV->data, videoFrameYUV->linesize,
                          0, videoCodecCtx->height,
                          videoFrameRGB->data, videoFrameRGB->linesize);

                // 6. 显示到SDL
                if (!sdlTexture) {
                    // 第一次显示时创建纹理
                    sdlTexture = SDL_CreateTexture(sdlRenderer,
                                                   SDL_PIXELFORMAT_RGB24,
                                                   SDL_TEXTUREACCESS_STREAMING,
                                                   videoCodecCtx->width,
                                                   videoCodecCtx->height);
                }

                // 更新纹理
                void* pixels;
                int pitch;
                if (SDL_LockTexture(sdlTexture, NULL, &pixels, &pitch) == 0) {
                    memcpy(pixels, videoFrameRGB->data[0],
                            videoFrameRGB->linesize[0] * videoCodecCtx->height);
                    SDL_UnlockTexture(sdlTexture);

                    // 渲染
                    SDL_RenderClear(sdlRenderer);
                    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
                    SDL_RenderPresent(sdlRenderer);
                }

                return;
            }
        } else {
            av_packet_unref(videoPacket);
        }
    }
}

void VideoThread::cleanupSDL()
{
    // 1. 销毁纹理
    if (sdlTexture) {
        SDL_DestroyTexture(sdlTexture);
        sdlTexture = nullptr;
        qDebug() << "SDL纹理已销毁";
    }

    // 2. 销毁渲染器
    if (sdlRenderer) {
        SDL_DestroyRenderer(sdlRenderer);
        sdlRenderer = nullptr;
        qDebug() << "SDL渲染器已销毁";
    }
}

void VideoThread::cleanup()
{
    qDebug() << "清理所有资源...";

    if (playTimer && playTimer->isActive()) {
        playTimer->stop();
        qDebug() << "定时器已停止";
    }

    // 清理视频资源（按创建的反顺序）
    if (videoSwsCtx) {
        sws_freeContext(videoSwsCtx);
        videoSwsCtx = nullptr;
    }

    if (videoFrameRGB) {
        av_frame_free(&videoFrameRGB);
    }

    if (videoFrameYUV) {
        av_frame_free(&videoFrameYUV);
    }

    if (videoPacket) {
        av_packet_free(&videoPacket);
    }

    if (videoCodecCtx) {
        avcodec_free_context(&videoCodecCtx);
    }

    if (videoFormatCtx) {
        avformat_close_input(&videoFormatCtx);
    }

    // 重置状态
    GlobalVars::playerState = STATE_IDLE;
    videoStreamIndex = -1;
    VideoFile.clear();

    qDebug() << "资源清理完成";
}

void VideoThread::UpadatStatus()
{
    switch (GlobalVars::playerState) {
    case STATE_IDLE:
        // 还没打开文件，先打开
        //           if (openVideoFile()) {
        //               startPlayback();
        //           }
        break;

    case STATE_READY:
        startPlayback();
        break;

    case STATE_PLAYING:
        resumePlayback();
        break;

    case STATE_PAUSED:
        pausePlayback();
        break;

    case STATE_ENDED:
        restartPlayback();
        break;
    }
}


void VideoThread::setSeekSlider(int flog,int value)
{
    if(0 == flog)
    {
        pausePlayback();
    }
    else if(1 == flog)
    {
        toSeek(value);
    }
    else if(2 == flog)
    {
        toSeek(value);
        resumePlayback();
    }
}

void VideoThread::toSeek(int value)
{
    // 防抖
    static qint64 lastCallTime = 0;
    static int lastValue = -1;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (lastValue != -1 &&
        (now - lastCallTime < 50) &&
        (qAbs(value - lastValue) < 100)) {
        return;
    }
    lastCallTime = now;
    lastValue = value;

    if (!videoFormatCtx || !videoCodecCtx) {
        return;
    }

    qDebug() << "跳转到：" << value << "ms";

    // 传递滑动方向信息
    decodeUntilTarget(value, true);
}

void VideoThread::decodeUntilTarget(int targetMs, bool isBackwardSeek)
{
    // 1. 暂停播放（如果正在播放）
       if (playTimer && playTimer->isActive()) {
           playTimer->stop();
       }

       // 2. 跳到关键帧
       double targetSeconds = targetMs / 1000.0;
       AVStream* videoStream = videoFormatCtx->streams[videoStreamIndex];

       qint64 targetTimestamp = av_rescale_q(
           (qint64)(targetSeconds * AV_TIME_BASE),
           av_get_time_base_q(),
           videoStream->time_base
       );

       int ret = av_seek_frame(videoFormatCtx, videoStreamIndex,
                              targetTimestamp, AVSEEK_FLAG_BACKWARD);

       if (ret < 0) {
           qDebug() << "跳转失败";
           return;
       }

       // 3. 清空解码器（重要！）
       avcodec_flush_buffers(videoCodecCtx);

       // 4. 显示第一帧（关键帧）给用户即时反馈
       seekAndDecodePrecisely(targetMs);

}

void VideoThread::seekAndDecodePrecisely(int targetMs)
{
    double targetSeconds = targetMs / 1000.0;
    AVStream* videoStream = videoFormatCtx->streams[videoStreamIndex];

    qDebug() << "开始解码到目标时间：" << targetSeconds << "秒";

    bool foundTarget = false;
    int consecutiveNullPackets = 0;
    const int MAX_NULL_PACKETS = 50;

    while (!foundTarget && consecutiveNullPackets < MAX_NULL_PACKETS) {
        AVPacket packet;
        av_init_packet(&packet);

        int ret = av_read_frame(videoFormatCtx, &packet);
        if (ret < 0) {
            consecutiveNullPackets++;
            continue;
        }

        if (packet.stream_index != videoStreamIndex) {
            av_packet_unref(&packet);
            continue;
        }

        // 检查包时间
        double packetTime = packet.pts * av_q2d(videoStream->time_base);
        if (packetTime < targetSeconds - 5.0) {
            // 太早的包直接跳过不解码
            qDebug() << "跳过早期包：" << packetTime << "秒";
            av_packet_unref(&packet);
            continue;
        }

        // 发送到解码器
        ret = avcodec_send_packet(videoCodecCtx, &packet);
        av_packet_unref(&packet);

        if (ret < 0) {
            qDebug() << "发送包失败";
            continue;
        }

        // 接收解码后的帧
        while (true) {
            ret = avcodec_receive_frame(videoCodecCtx, videoFrameYUV);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                qDebug() << "解码错误";
                break;
            }

            double frameTime = videoFrameYUV->pts * av_q2d(videoStream->time_base);

            if (frameTime >= targetSeconds || qAbs(frameTime - targetSeconds) < 0.1) {
                // 找到目标帧
                displayCurrentFrame();
                foundTarget = true;
                qDebug() << "✅ 成功跳转到：" << frameTime << "秒";

                // 继续清空解码器缓冲区
                while (avcodec_receive_frame(videoCodecCtx, videoFrameYUV) >= 0) {
                    av_frame_unref(videoFrameYUV);
                }
                break;
            }

            av_frame_unref(videoFrameYUV);
        }
    }

    if (!foundTarget) {
        qDebug() << "❌ 未能精确跳转，尝试显示当前帧";
        // 尝试显示最后一帧
        if (videoFrameYUV->data[0]) {
            displayCurrentFrame();
        }
    }
}

void VideoThread::displayCurrentFrame()
{
    if (!videoFrameYUV || !videoCodecCtx || !sdlRenderer) {
        qDebug() << "显示失败：资源未初始化";
        return;
    }

    // 1. YUV转RGB
    if (videoSwsCtx) {
        sws_scale(videoSwsCtx,
                 videoFrameYUV->data, videoFrameYUV->linesize,
                 0, videoCodecCtx->height,
                 videoFrameRGB->data, videoFrameRGB->linesize);
    } else {
        qDebug() << "显示失败：videoSwsCtx为空";
        return;
    }

    // 2. 创建或更新SDL纹理
    if (!sdlTexture) {
        sdlTexture = SDL_CreateTexture(sdlRenderer,
                                      SDL_PIXELFORMAT_RGB24,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      videoCodecCtx->width,
                                      videoCodecCtx->height);
        if (!sdlTexture) {
            qDebug() << "创建纹理失败：" << SDL_GetError();
            return;
        }
        qDebug() << "创建新纹理";
    }

    // 3. 更新纹理数据
    int ret = SDL_UpdateTexture(sdlTexture,
                               NULL,
                               videoFrameRGB->data[0],
                               videoFrameRGB->linesize[0]);
    if (ret != 0) {
        qDebug() << "更新纹理失败：" << SDL_GetError();
        return;
    }

    // 4. 渲染
    SDL_RenderClear(sdlRenderer);
    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
    SDL_RenderPresent(sdlRenderer);

    // 5. 强制处理SDL事件（确保显示）
    SDL_PumpEvents();

}
