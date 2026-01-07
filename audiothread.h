#ifndef AUDIOTHREAD_H
#define AUDIOTHREAD_H

#include <QObject>
#include <QMutex>
#include <QElapsedTimer>
#include "global_status.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <SDL.h>
}

class AudioThread : public QObject
{
    Q_OBJECT

public:
    explicit AudioThread(QObject *parent = nullptr);
    ~AudioThread();

    // 获取精确的音频时钟（秒）
    double getCurrentTime() const;

public slots:
    void init_audio(const QString &filename);
    void setVolume(float volume);
    void setSpeed(float speed);
    void seekTo(qint64 positionMs);
    void UpadatStatus();

signals:
    void positionChanged(qint64 positionMs);  // 当前播放位置（毫秒）
    void playbackFinished();                  // 播放完成
    void errorOccurred(const QString &error); // 错误信息

private:
    // SDL音频回调
    static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);
    void audioCallback(Uint8 *stream, int len);

    // 核心功能
    bool initAudioDecoder(const QString &filename);
    bool initSDLOutput();
    bool decodeAudioFrame();
    void fillAudioBuffer(Uint8 *stream, int len);
    void updateAudioClock(int bytesPlayed);
    void cleanup();

    // 工具函数
    void allocateAudioBuffer(int samples);
    void freeAudioBuffer();
    void applyVolume(uint8_t *data, int len, float volume);


    void startPlayback();
    void pausePlayback();
    void stopPlayback();


private:
    // FFmpeg资源
    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    AVFrame *m_frame = nullptr;
    AVPacket *m_packet = nullptr;
    SwrContext *m_swrCtx = nullptr;
    int m_audioStreamIndex = -1;

    // 音频参数
    int m_sampleRate = 0;
    int m_channels = 0;
    AVSampleFormat m_sampleFmt = AV_SAMPLE_FMT_NONE;
    int64_t m_audioChannelLayout = 0;

    // SDL资源
    SDL_AudioSpec m_wantedSpec;
    SDL_AudioSpec m_obtainedSpec;
    SDL_AudioDeviceID m_audioDevice = 0;

    // 音频缓冲区（修复：使用单个交错缓冲区）
    uint8_t *m_audioBuffer = nullptr;      // 交错格式缓冲区
    int m_audioBufferSize = 0;             // 缓冲区总大小（字节）
    int m_audioBufferLen = 0;              // 当前有效数据长度（字节）
    int m_audioBufferIndex = 0;            // 当前读取位置

    // 音频时钟（精确计算）
    double m_audioClock = 0.0;             // 音频时钟（秒）
    double m_audioPts = 0.0;               // 当前音频帧的PTS（秒）
    qint64 m_startTime = 0;                // 开始播放的系统时间（毫秒）

    // 播放控制
    bool m_isPlaying = false;
    bool m_isEOF = false;                  // 是否到达文件末尾
    float m_volume = 1.0f;
    float m_speed = 1.0f;

    // 同步保护
    mutable QMutex m_mutex;
};

#endif // AUDIOTHREAD_H
