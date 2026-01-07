// videothread.h
#ifndef VIDEOTHREAD_H
#define VIDEOTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QDebug>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QDateTime>
#include "global_status.h"
#include "audiothread.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <SDL.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>
}

class VideoThread : public QObject
{
    Q_OBJECT

public:
    explicit VideoThread(QObject* parent = nullptr);
    ~VideoThread();

signals:
    void UpadatButton(bool flag);
    void UpadatStatus(QString value,double time);
    void UpadatseekSlider(double);
public slots:
    void init_video(QString path);
    void UpadatStatus();
    void setPlaybackSpeed(float speed);//倍速设置
    void setSeekSlider(int flog,int value);





public:
    int findVideoStream(AVFormatContext* formatCtx);

    bool initVideoDecoder();
    bool initSDLDisplay();


    void startPlayback();//开始视频播放
    void showVideoInfo();//显示视频信息
    void onPlayTimerTimeout();//显示一帧图像
    QString formatTime(qint64 seconds);//时间格式转换
    void pausePlayback();//// 暂停播放（从PLAYING到PAUSED）
    void restartPlayback();// 重新播放（从ENDED到PLAYING）
    void resumePlayback();// 继续播放（从PAUSED到PLAYING）
    void onPlayFinished();// 播放结束（从PLAYING到ENDED）
    void resetToBeginning();//确定视频从头开始播放

    void cleanup();
    void cleanupSDL();


    void setDisplayWidget(QWidget *widget);

    //跳转
    void toSeek(int value);
    void displayCurrentFrame();
    void decodeUntilTarget(int targetMs, bool isBackwardSeek);
    void seekAndDecodePrecisely(int targetMs);

    void updateTimerInterval();

    void setAudioReference(AudioThread* audio);  // "认识"音频线程

private:

    int total_time = 0;
    float m_currentSpeed = 1.5f;  // 当前速度
    qint64 m_lastSeekTime = 0;      // 上次跳转时间（防抖动）
    bool seek_flag_video = false;
    float seek_time = 0;

    // 视频相关 - 明确以Video开头
    AVFormatContext* videoFormatCtx = nullptr;  // 视频文件上下文
    AVCodecContext* videoCodecCtx = nullptr;    // 视频解码器
    AVFrame* videoFrameYUV = nullptr;           // YUV帧（解码后）
    AVFrame* videoFrameRGB = nullptr;           // RGB帧（转换后）
    SwsContext* videoSwsCtx = nullptr;          // 视频格式转换器
    AVPacket* videoPacket = nullptr;            // 视频数据包
    int videoStreamIndex = -1;                  // 视频流索引


    // SDL相关
    SDL_Window* sdlWindow = nullptr;
    SDL_Renderer* sdlRenderer = nullptr;
    SDL_Texture* sdlTexture = nullptr;
    QWidget *m_displayWidget = nullptr;
    WId widgetId;

    // 播放控制
    QTimer *playTimer = nullptr;               // 视频播放定时器
    QTimer* audioTimer = nullptr;               // 音频解码定时器


    //拖动
    QString VideoFile = nullptr;

    AudioThread* m_audioRef;  // 保存音频的引用
    double getAudioTime();     // 获取音频时间


    // 同步相关
    double m_frameLastDelay;     // 上一帧的实际延迟

    // 同步方法
    double synchronizeVideo(double pts);
};

#endif // VIDEOTHREAD_H
