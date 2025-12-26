#include "mainwindow.h"
#include "ui_mainwindow.h"//

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("简易视频播放器");

    // 初始化FFmpeg
    avformat_network_init();
    qDebug() << "FFmpeg版本:" << av_version_info();

    // 初始化SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        qDebug() << "SDL初始化失败:" << SDL_GetError();
    } else {

    }

    // 创建播放定时器
    playTimer = new QTimer(this);
    playTimer->setTimerType(Qt::PreciseTimer);  // 精确计时器
    connect(playTimer, &QTimer::timeout, this, &MainWindow::onPlayTimerTimeout);

    // 初始状态
    playerState = STATE_IDLE;

    ui->start_button->setIcon(QIcon(":/pictrues/start.png"));

    ui->label->setVisible(false);
    ui->label_2->setVisible(false);
    ui->label_3->setVisible(false);
}

MainWindow::~MainWindow()
{
    cleanupSDL();
    cleanup();

    // 销毁窗口
    if (sdlWindow) {
        SDL_DestroyWindow(sdlWindow);
        sdlWindow = nullptr;
    }

    SDL_Quit();
    delete ui;
}

void MainWindow::cleanupSDL()
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

void MainWindow::cleanup()
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

    // 清理音频资源（为后续预留）
    if (audioFormatCtx) {
        avformat_close_input(&audioFormatCtx);
    }

    if (audioCodecCtx) {
        avcodec_free_context(&audioCodecCtx);
    }

    // 重置状态
    playerState = STATE_IDLE;
    videoStreamIndex = -1;
    audioStreamIndex = -1;
    currentVideoFile.clear();

    qDebug() << "资源清理完成";
}

void MainWindow::on_add_button_clicked()
{

    QString filename = QFileDialog::getOpenFileName(this,"选择视频文件",QDir::homePath(), "视频文件 (*.mp4 *.avi *.mov *.mkv);;所有文件 (*.*)");
    if(filename.isEmpty()){
        return;
    }

    //清除函数
    cleanup();
    cleanupSDL();
    currentVideoFile = filename;  // 保存文件名

    // 打开视频文件上下文
    int ret = avformat_open_input(&videoFormatCtx,
                                  currentVideoFile.toUtf8().constData(),
                                  NULL, NULL);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        QMessageBox::warning(this, "错误",
                             QString("无法打开视频文件：\n%1").arg(errbuf));
        return;
    }

    // 获取流信息
    ret = avformat_find_stream_info(videoFormatCtx, NULL);
    if (ret < 0) {
        QMessageBox::warning(this, "错误", "无法获取流信息");
        cleanup();
        return;
    }

    //查找视频流
    videoStreamIndex = findVideoStream(videoFormatCtx);
    if (videoStreamIndex == -1) {
        QMessageBox::warning(this, "错误", "未找到视频流");
        cleanup();
        return;
    }

    // 6. 查找音频流（只记录，不初始化）
    audioStreamIndex = findAudioStream(videoFormatCtx);
    if (audioStreamIndex == -1) {
        QMessageBox::warning(this, "错误", "未找到音频流");
        cleanup();
        return;
    }

    // 7. 显示视频信息
    //   showVideoInfo();

    // 8. 初始化视频解码器
    if (!initVideoDecoder()) {
        QMessageBox::warning(this, "错误", "视频解码器初始化失败");
        cleanup();
        return;
    }

    // 9. 初始化SDL显示
    if (!initSDLDisplay()) {
        QMessageBox::warning(this, "错误", "SDL显示初始化失败");
        cleanup();
        return;
    }

    playerState = STATE_READY;
    ui->start_button->setIcon(QIcon(":/pictrues/start.png"));

    total_time = videoFormatCtx->duration / (double)AV_TIME_BASE;
    ui->seekSlider->setMaximum(total_time * 1000);

    startPlayback();

}

bool MainWindow::initVideoDecoder()
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

bool MainWindow::initSDLDisplay()
{
    qDebug() << "开始初始化SDL显示...";

    // 2. 检查是否有视频解码器信息
    if (!videoCodecCtx) {
        qDebug() << "错误：视频解码器未初始化";
        return false;
    }

    // 3. 获取Qt视频显示部件的窗口ID
    WId widgetId = ui->videoWidget->winId();
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

int MainWindow::findVideoStream(AVFormatContext* formatCtx)
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

int MainWindow::findAudioStream(AVFormatContext* formatCtx)
{
    for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            AVStream* stream = formatCtx->streams[i];
            qDebug() << "找到音频流 #" << i;
            qDebug() << "  编码器：" << avcodec_get_name(stream->codecpar->codec_id);
            qDebug() << "  采样率：" << stream->codecpar->sample_rate << "Hz";
            qDebug() << "  声道数：" << stream->codecpar->channels;
            return i;
        }
    }
    return -1;
}

void MainWindow::showVideoInfo()
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

// 式化时间（秒转时分秒）
QString MainWindow::formatTime(qint64 seconds)
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

void MainWindow::on_start_button_clicked()
{
    ui->time_label->setVisible(true);
    switch (playerState) {
    case STATE_IDLE:
        // 还没打开文件，先打开
        if (openVideoFile()) {
            startPlayback();
        }
        break;

    case STATE_READY:
        // 文件已就绪，开始播放
        ui->start_button->setIcon(QIcon(":/pictrues/stop.png"));
        startPlayback();
        break;

    case STATE_PLAYING:
        // 正在播放，暂停
        ui->start_button->setIcon(QIcon(":/pictrues/start.png"));
        pausePlayback();
        break;

    case STATE_PAUSED:
        // 已暂停，继续播放
        ui->start_button->setIcon(QIcon(":/pictrues/stop.png"));
        resumePlayback();
        break;

    case STATE_ENDED:
        // 播放结束，重新开始
        restartPlayback();
        break;
    }
}

bool MainWindow::openVideoFile()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "选择视频文件", QDir::homePath(),
                                                    "视频文件 (*.mp4 *.avi *.mov *.mkv);;所有文件 (*.*)");

    if (fileName.isEmpty()) return false;

    // 清理之前的
    cleanup();
    cleanupSDL();
    currentVideoFile = fileName;  // 保存文件名

    // 打开文件
    if (avformat_open_input(&videoFormatCtx, currentVideoFile.toUtf8().constData(), NULL, NULL) < 0) {
        QMessageBox::warning(this, "错误", "无法打开视频文件");
        return false;
    }

    // 获取流信息
    if (avformat_find_stream_info(videoFormatCtx, NULL) < 0) {
        QMessageBox::warning(this, "错误", "无法获取流信息");
        cleanup();
        return false;
    }

    // 查找视频流
    videoStreamIndex = findVideoStream(videoFormatCtx);
    if (videoStreamIndex == -1) {
        QMessageBox::warning(this, "错误", "未找到视频流");
        cleanup();
        return false;
    }

    // 初始化解码器
    if (!initVideoDecoder()) {
        QMessageBox::warning(this, "错误", "视频解码器初始化失败");
        cleanup();
        return false;
    }

    // 初始化SDL显示
    if (!initSDLDisplay()) {
        QMessageBox::warning(this, "错误", "SDL显示初始化失败");
        cleanup();
        return false;
    }

    playerState = STATE_READY;
    ui->start_button->setIcon(QIcon(":/pictrues/start.png"));

    total_time = videoFormatCtx->duration / (double)AV_TIME_BASE;
    ui->seekSlider->setMaximum(total_time * 1000);

    return true;
}

void MainWindow::startPlayback()
{

    // 确保在文件开头
    resetToBeginning();

    // 启动定时器
    AVStream* videoStream = videoFormatCtx->streams[videoStreamIndex];
    double frameRate = av_q2d(videoStream->avg_frame_rate);
    if (frameRate <= 0) frameRate = 30.0;

    int interval = 1000 / frameRate;
    if (playTimer) {
        playTimer->start(interval);
        ui->start_button->setIcon(QIcon(":/pictrues/stop.png"));
        qDebug() << "定时器启动，间隔：" << interval << "ms," << "videoStreamIndex:" <<videoStreamIndex;
    }

    playerState = STATE_PLAYING;
}

// 暂停播放（从PLAYING到PAUSED）
void MainWindow::pausePlayback()
{
    qDebug() << "暂停播放";

    if (playTimer && playTimer->isActive()) {
        playTimer->stop();
    }

    playerState = STATE_PAUSED;
}

// 继续播放（从PAUSED到PLAYING）
void MainWindow::resumePlayback()
{
    qDebug() << "继续播放";

    if (playTimer) {
        playTimer->start();
    }

    playerState = STATE_PLAYING;
}

// 重新播放（从ENDED到PLAYING）
void MainWindow::restartPlayback()
{
    ui->start_button->setIcon(QIcon(":/pictrues/stop.png"));
    resetToBeginning();

    if (playTimer) {
        playTimer->start();
    }

    playerState = STATE_PLAYING;
}

// 播放结束（从PLAYING到ENDED）
void MainWindow::onPlayFinished()
{
    qDebug() << "播放结束";

    if (playTimer && playTimer->isActive()) {
        playTimer->stop();
    }

    playerState = STATE_ENDED;
}

void MainWindow::resetToBeginning()
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

void MainWindow::onPlayTimerTimeout()
{
    // 检查状态
    if (playerState != STATE_PLAYING) {
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
                ui->time_label->setText(timeText);
                ui->start_button->setIcon(QIcon(":/pictrues/start.png"));

                playerState = STATE_ENDED;
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

                //  显示时间
                double totalTime = videoFormatCtx->duration / AV_TIME_BASE;

                // 格式化为 分:秒
                int currentMin = (int)currentTime / 60;
                int currentSec = (int)currentTime % 60;
                int totalMin = (int)totalTime / 60;
                int totalSec = (int)totalTime % 60;

                QString timeText = QString("%1:%2 / %3:%4")
                        .arg(currentMin, 2, 10, QChar('0'))
                        .arg(currentSec, 2, 10, QChar('0'))
                        .arg(totalMin, 2, 10, QChar('0'))
                        .arg(totalSec, 2, 10, QChar('0'));

                // 更新到UI
                ui->time_label->setText(timeText);
                ui->seekSlider->setValue(currentTime  * 1000);


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



