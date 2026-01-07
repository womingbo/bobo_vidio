#include "audiothread.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QDateTime>

AudioThread::AudioThread(QObject *parent) : QObject(parent)
{
    qDebug() << "AudioThread 创建";
}

AudioThread::~AudioThread()
{
    stopPlayback();
    cleanup();
    qDebug() << "AudioThread 销毁";
}

// 获取精确的音频时钟
double AudioThread::getAudioClock() const
{
    QMutexLocker locker(&m_mutex);

    if (!m_isPlaying || m_startTime == 0) {
        return m_audioClock;
    }

    // 音频时钟 = 最后已知PTS + 从最后更新到现在的时间
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTime;
    return m_audioClock + (elapsed / 1000.0);
}

void AudioThread::init_audio(const QString &filename)
{
    qDebug() << "初始化音频:" << filename;

    // 清理之前的资源
    cleanup();

    // 初始化音频解码器
    if (!initAudioDecoder(filename)) {
        emit errorOccurred("音频解码器初始化失败");
        return;
    }

    // 初始化SDL音频输出
    if (!initSDLOutput()) {
        emit errorOccurred("SDL音频输出初始化失败");
        return;
    }

    // 重置状态
    m_isPlaying = false;
    m_isEOF = false;
    m_audioClock = 0.0;
    m_startTime = 0;

    qDebug() << "音频初始化完成，准备播放";
}

bool AudioThread::initAudioDecoder(const QString &filename)
{
    qDebug() << "初始化音频解码器...";

    // 1. 打开音频文件
    int ret = avformat_open_input(&m_formatCtx, filename.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        qDebug() << "无法打开音频文件";
        return false;
    }

    // 2. 获取流信息
    ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        qDebug() << "无法获取流信息";
        return false;
    }

    // 3. 查找音频流
    m_audioStreamIndex = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audioStreamIndex < 0) {
        qDebug() << "未找到音频流";
        return false;
    }

    // 4. 获取音频流
    AVStream *audioStream = m_formatCtx->streams[m_audioStreamIndex];
    AVCodecParameters *codecPar = audioStream->codecpar;

    // 5. 查找解码器
    const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        qDebug() << "找不到音频解码器";
        return false;
    }

    // 6. 创建解码器上下文
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qDebug() << "无法分配解码器上下文";
        return false;
    }

    // 7. 复制参数
    ret = avcodec_parameters_to_context(m_codecCtx, codecPar);
    if (ret < 0) {
        qDebug() << "无法复制编解码器参数";
        return false;
    }

    // 8. 打开解码器
    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        qDebug() << "无法打开音频解码器";
        return false;
    }

    // 9. 获取音频参数
    m_sampleRate = m_codecCtx->sample_rate;
    m_channels = m_codecCtx->channels;
    m_sampleFmt = m_codecCtx->sample_fmt;
    m_audioChannelLayout = m_codecCtx->channel_layout;
    if (m_audioChannelLayout == 0) {
        m_audioChannelLayout = av_get_default_channel_layout(m_channels);
    }

    qDebug() << "音频信息:";
    qDebug() << "  编码器:" << avcodec_get_name(codecPar->codec_id);
    qDebug() << "  采样率:" << m_sampleRate << "Hz";
    qDebug() << "  声道数:" << m_channels;
    qDebug() << "  采样格式:" << av_get_sample_fmt_name(m_sampleFmt);
    qDebug() << "  时长:" << m_formatCtx->duration / AV_TIME_BASE << "秒";

    // 10. 初始化重采样器（转换为SDL需要的格式）
    m_swrCtx = swr_alloc_set_opts(nullptr,
                                 m_audioChannelLayout,          // 输出声道布局
                                 AV_SAMPLE_FMT_S16,             // 输出格式：16位有符号整数（SDL标准）
                                 m_sampleRate,                  // 输出采样率
                                 m_audioChannelLayout,          // 输入声道布局
                                 m_sampleFmt,                   // 输入格式
                                 m_sampleRate,                  // 输入采样率
                                 0, nullptr);

    if (!m_swrCtx) {
        qDebug() << "无法分配重采样上下文";
        return false;
    }

    ret = swr_init(m_swrCtx);
    if (ret < 0) {
        qDebug() << "无法初始化重采样器";
        return false;
    }

    // 11. 分配帧和包
    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();

    if (!m_frame || !m_packet) {
        qDebug() << "无法分配帧或包";
        return false;
    }

    // 12. 预解码几帧填充缓冲区
    for (int i = 0; i < 3; i++) {
        if (!decodeAudioFrame()) {
            break;
        }
    }

    qDebug() << "音频解码器初始化成功";
    return true;
}

bool AudioThread::initSDLOutput()
{
    qDebug() << "初始化SDL音频输出...";

    // 检查SDL音频是否已初始化
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        qDebug() << "警告：SDL音频子系统未初始化";
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            qDebug() << "无法初始化SDL音频子系统:" << SDL_GetError();
            return false;
        }
    }

    // 设置SDL音频参数
    SDL_zero(m_wantedSpec);
    m_wantedSpec.freq = m_sampleRate;          // 采样率
    m_wantedSpec.format = AUDIO_S16SYS;        // 16位有符号整数
    m_wantedSpec.channels = m_channels;        // 声道数
    m_wantedSpec.silence = 0;                  // 静音值
    m_wantedSpec.samples = 2048;               // 缓冲区大小（适当增大减少回调频率）
    m_wantedSpec.callback = sdlAudioCallback;  // 回调函数
    m_wantedSpec.userdata = this;              // 用户数据

    // 打开音频设备
    m_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &m_wantedSpec, &m_obtainedSpec,
                                       SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    if (m_audioDevice == 0) {
        qDebug() << "无法打开音频设备:" << SDL_GetError();
        return false;
    }

    // 检查实际获得的音频参数
    qDebug() << "SDL音频设备打开成功:";
    qDebug() << "  设备ID:" << m_audioDevice;
    qDebug() << "  采样率:" << m_obtainedSpec.freq << "Hz";
    qDebug() << "  格式:" << m_obtainedSpec.format;
    qDebug() << "  声道数:" << m_obtainedSpec.channels;
    qDebug() << "  缓冲区大小:" << m_obtainedSpec.samples << "个样本";

    // 验证参数匹配
    if (m_obtainedSpec.channels != m_channels) {
        qDebug() << "警告：声道数不匹配，SDL使用" << m_obtainedSpec.channels << "声道";
    }

    if (m_obtainedSpec.freq != m_sampleRate) {
        qDebug() << "警告：采样率不匹配，SDL使用" << m_obtainedSpec.freq << "Hz";
    }

    return true;
}

void AudioThread::startPlayback()
{
    QMutexLocker locker(&m_mutex);

    if (m_audioDevice == 0) {
        qDebug() << "音频设备未初始化";
        return;
    }

    if (m_isPlaying) {
        qDebug() << "音频已经在播放";
        return;
    }

    m_isPlaying = true;
    m_startTime = QDateTime::currentMSecsSinceEpoch();
    SDL_PauseAudioDevice(m_audioDevice, 0);  // 0=开始播放，1=暂停

    qDebug() << "音频开始播放，开始时间:" << m_startTime;
}

void AudioThread::pausePlayback()
{
    QMutexLocker locker(&m_mutex);

    if (!m_isPlaying || m_audioDevice == 0) {
        return;
    }

    m_isPlaying = false;
    SDL_PauseAudioDevice(m_audioDevice, 1);  // 暂停播放

    // 更新音频时钟（暂停时不增加）
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTime;
    m_audioClock += elapsed / 1000.0;
    m_startTime = 0;

    qDebug() << "音频暂停，当前时钟:" << m_audioClock << "秒";
}

void AudioThread::stopPlayback()
{
    QMutexLocker locker(&m_mutex);

    m_isPlaying = false;
    m_isEOF = false;

    if (m_audioDevice != 0) {
        SDL_PauseAudioDevice(m_audioDevice, 1);  // 先暂停
        SDL_CloseAudioDevice(m_audioDevice);     // 再关闭
        m_audioDevice = 0;
    }

    m_startTime = 0;
    qDebug() << "音频停止";
}

void AudioThread::setVolume(float volume)
{
    QMutexLocker locker(&m_mutex);
    m_volume = qBound(0.0f, volume, 2.0f);  // 允许0-2倍音量
    qDebug() << "设置音量:" << m_volume;
}

void AudioThread::setSpeed(float speed)
{
    QMutexLocker locker(&m_mutex);
    m_speed = qBound(0.5f, speed, 4.0f);  // 限制在0.5x到4x之间

    // 注意：简单的速度调整需要调整播放逻辑
    // 这里只是设置参数，实际实现需要修改时钟计算
    qDebug() << "设置播放速度:" << m_speed << "x";
}

void AudioThread::seekTo(qint64 positionMs)
{
    QMutexLocker locker(&m_mutex);

    if (!m_formatCtx || m_audioStreamIndex < 0) {
        return;
    }

    qDebug() << "音频跳转到:" << positionMs << "ms";

    // 1. 清空缓冲区
    freeAudioBuffer();
    m_audioBufferLen = 0;
    m_audioBufferIndex = 0;

    // 2. 清除解码器缓冲区
    avcodec_flush_buffers(m_codecCtx);

    // 3. 跳转
    int64_t targetPts = av_rescale(positionMs, AV_TIME_BASE, 1000);
    int ret = av_seek_frame(m_formatCtx, m_audioStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD);

    if (ret >= 0) {
        m_audioClock = positionMs / 1000.0;
        m_audioPts = m_audioClock;
        m_isEOF = false;

        // 预解码几帧填充缓冲区
        for (int i = 0; i < 5; i++) {
            if (!decodeAudioFrame()) {
                break;
            }
        }

        qDebug() << "音频跳转成功，新时钟:" << m_audioClock << "秒";
    } else {
        qDebug() << "音频跳转失败";
    }
}

// SDL音频回调函数（静态）
void AudioThread::sdlAudioCallback(void *userdata, Uint8 *stream, int len)
{
    AudioThread *audioThread = static_cast<AudioThread*>(userdata);
    audioThread->audioCallback(stream, len);
}

// 音频回调函数（成员）
void AudioThread::audioCallback(Uint8 *stream, int len)
{
    QMutexLocker locker(&m_mutex);

    if (!m_isPlaying) {
        // 静音输出
        memset(stream, 0, len);
        return;
    }

    if (m_isEOF && m_audioBufferLen == 0) {
        // 文件结束且缓冲区空，填充静音
        memset(stream, 0, len);
        emit playbackFinished();
        return;
    }

    // 填充音频数据
    fillAudioBuffer(stream, len);
}

void AudioThread::fillAudioBuffer(Uint8 *stream, int len)
{
    int filled = 0;

    while (filled < len) {
        // 如果缓冲区空，解码更多数据
        if (m_audioBufferIndex >= m_audioBufferLen) {
            if (!decodeAudioFrame()) {
                // 解码失败或文件结束
                if (m_isEOF) {
                    // 文件结束，剩余部分填充静音
                    memset(stream + filled, 0, len - filled);
                    return;
                }
                // 解码失败但未到文件结束，继续尝试
                continue;
            }
        }

        // 计算可以拷贝的数据量
        int remainingBuffer = m_audioBufferLen - m_audioBufferIndex;
        int remainingStream = len - filled;
        int copySize = qMin(remainingBuffer, remainingStream);

        if (copySize > 0) {
            // 关键修复：直接内存拷贝（交错格式）
            memcpy(stream + filled,
                   m_audioBuffer + m_audioBufferIndex,
                   copySize);

            m_audioBufferIndex += copySize;
            filled += copySize;

            // 更新音频时钟
            updateAudioClock(copySize);
        }
    }
}

bool AudioThread::decodeAudioFrame()
{
    if (m_isEOF) {
        return false;
    }

    while (true) {
        // 1. 读取一个数据包
        int ret = av_read_frame(m_formatCtx, m_packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                m_isEOF = true;
                qDebug() << "音频文件结束";

                // 发送最终位置
                emit positionChanged(static_cast<qint64>(m_audioClock * 1000));

                // 尝试刷新解码器缓冲区
                m_packet->data = nullptr;
                m_packet->size = 0;
                ret = avcodec_send_packet(m_codecCtx, m_packet);
                if (ret >= 0) {
                    continue;  // 继续处理缓冲区的帧
                }
            }
            return false;
        }

        // 2. 检查是否是音频包
        if (m_packet->stream_index == m_audioStreamIndex) {
            // 3. 发送给解码器
            ret = avcodec_send_packet(m_codecCtx, m_packet);
            av_packet_unref(m_packet);

            if (ret < 0) {
                qDebug() << "发送音频包到解码器失败";
                continue;
            }

            // 4. 接收解码后的帧
            ret = avcodec_receive_frame(m_codecCtx, m_frame);
            if (ret >= 0) {
                // 5. 更新音频PTS（用于精确时钟）
                if (m_frame->pts != AV_NOPTS_VALUE) {
                    AVStream *stream = m_formatCtx->streams[m_audioStreamIndex];
                    m_audioPts = m_frame->pts * av_q2d(stream->time_base);
                    m_audioClock = m_audioPts;  // 更新时钟
                }

                // 6. 重采样
                int dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(m_swrCtx, m_sampleRate) + m_frame->nb_samples,
                    m_sampleRate, m_sampleRate, AV_ROUND_UP);

                // 分配足够大的缓冲区
                allocateAudioBuffer(dst_nb_samples);

                // 执行重采样
                ret = swr_convert(m_swrCtx,
                                 &m_audioBuffer,  // 输出缓冲区
                                 dst_nb_samples,  // 输出样本数
                                 (const uint8_t**)m_frame->data,  // 输入数据
                                 m_frame->nb_samples);            // 输入样本数

                if (ret > 0) {
                    // 计算缓冲区大小（样本数 × 声道数 × 每样本字节数）
                    m_audioBufferLen = ret * m_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
                    m_audioBufferIndex = 0;

                    // 应用音量
                    if (m_volume != 1.0f) {
                        applyVolume(m_audioBuffer, m_audioBufferLen, m_volume);
                    }

                    av_frame_unref(m_frame);

                    // 调试信息
                    static int frameCount = 0;
                    if (++frameCount % 100 == 0) {
                        qDebug() << QString("解码音频帧 #%1, PTS: %2s, 缓冲区: %3字节")
                                    .arg(frameCount)
                                    .arg(m_audioPts, 0, 'f', 3)
                                    .arg(m_audioBufferLen);
                    }

                    return true;
                }
            } else if (ret == AVERROR(EAGAIN)) {
                // 需要更多输入数据
                continue;
            } else {
                // 解码错误
                qDebug() << "音频解码错误" ;
            }
        } else {
            // 不是音频包，释放并继续
            av_packet_unref(m_packet);
        }
    }

    return false;
}

void AudioThread::updateAudioClock(int bytesPlayed)
{
    // 计算播放的时长（秒）
    double secondsPlayed = bytesPlayed / (double)(m_channels * m_sampleRate * 2);

    // 更新音频时钟
    m_audioClock += secondsPlayed;

    // 定期发送位置更新（避免太频繁）
    static qint64 lastUpdateTime = 0;
    qint64 currentTime = static_cast<qint64>(m_audioClock * 1000);

    if (currentTime - lastUpdateTime >= 100) {  // 每100ms更新一次
        emit positionChanged(currentTime);
        lastUpdateTime = currentTime;
    }
}

void AudioThread::allocateAudioBuffer(int samples)
{
    int requiredSize = samples * m_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

    if (m_audioBufferSize < requiredSize) {
        // 释放旧缓冲区
        freeAudioBuffer();

        // 分配新缓冲区
        m_audioBuffer = (uint8_t*)av_malloc(requiredSize);
        if (!m_audioBuffer) {
            qDebug() << "无法分配音频缓冲区:" << requiredSize << "字节";
            m_audioBufferSize = 0;
            return;
        }

        m_audioBufferSize = requiredSize;
        qDebug() << "分配音频缓冲区:" << requiredSize << "字节";
    }
}

void AudioThread::freeAudioBuffer()
{
    if (m_audioBuffer) {
        av_free(m_audioBuffer);
        m_audioBuffer = nullptr;
    }
    m_audioBufferSize = 0;
    m_audioBufferLen = 0;
    m_audioBufferIndex = 0;
}

void AudioThread::applyVolume(uint8_t *data, int len, float volume)
{
    if (qFuzzyCompare(volume, 1.0f)) {
        return;
    }

    // 16位有符号整数
    int16_t *samples = (int16_t*)data;
    int sampleCount = len / sizeof(int16_t);

    for (int i = 0; i < sampleCount; i++) {
        float sample = samples[i] * volume;

        // 限制在16位有符号整数范围内
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;

        samples[i] = static_cast<int16_t>(sample);
    }
}

void AudioThread::cleanup()
{
    qDebug() << "清理音频资源";

    // 停止播放
    stopPlayback();

    // 清理音频缓冲区
    freeAudioBuffer();

    // 清理FFmpeg资源
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }

    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }

    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }

    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    // 重置状态
    m_audioStreamIndex = -1;
    m_sampleRate = 0;
    m_channels = 0;
    m_audioClock = 0.0;
    m_audioPts = 0.0;
    m_startTime = 0;
    m_isPlaying = false;
    m_isEOF = false;
    m_volume = 1.0f;
    m_speed = 1.0f;

    qDebug() << "音频资源清理完成";
}
