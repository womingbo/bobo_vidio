#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QDir>
#include <QFileDialog>
#include <QDebug>
#include <QDesktopServices>
#include <QTimer>
#include <QMutex>
#include <QQueue>
#include <QDateTime>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>  // 添加这行
#include <SDL.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>
}

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:

    void on_add_button_clicked();

    void on_start_button_clicked();

    void onPlayTimerTimeout();

private:

    //播放状态
    enum PlayerState {
        STATE_IDLE,        // 空闲（未加载文件）
        STATE_READY,       // 就绪（文件已加载，可播放）
        STATE_PLAYING,     // 正在播放
        STATE_PAUSED,      // 已暂停
        STATE_ENDED        // 播放结束
    };
    PlayerState playerState = STATE_IDLE;

    //其他参数
    QString  currentVideoFile = nullptr;
    int total_time = 0;



    // 视频相关 - 明确以Video开头
    AVFormatContext* videoFormatCtx = nullptr;  // 视频文件上下文
    AVCodecContext* videoCodecCtx = nullptr;    // 视频解码器
    AVFrame* videoFrameYUV = nullptr;           // YUV帧（解码后）
    AVFrame* videoFrameRGB = nullptr;           // RGB帧（转换后）
    SwsContext* videoSwsCtx = nullptr;          // 视频格式转换器
    AVPacket* videoPacket = nullptr;            // 视频数据包
    int videoStreamIndex = -1;                  // 视频流索引



    // 音频相关 - 明确以Audio开头
    AVFormatContext* audioFormatCtx = nullptr;  // 音频文件上下文
    AVCodecContext* audioCodecCtx = nullptr;    // 音频解码器
    int audioStreamIndex = -1;


    // SDL相关
    SDL_Window* sdlWindow = nullptr;
    SDL_Renderer* sdlRenderer = nullptr;
    SDL_Texture* sdlTexture = nullptr;

    // 播放控制
    QTimer *playTimer = nullptr;               // 视频播放定时器
    QTimer* audioTimer = nullptr;               // 音频解码定时器


    //ui
    Ui::MainWindow *ui;

private:
    void cleanup();
    void cleanupSDL();
    QString formatTime(qint64 seconds);

    //查找 视频 音频流
    int findVideoStream(AVFormatContext* formatCtx);
    int findAudioStream(AVFormatContext* formatCtx);

    //初始化视频解码器
    bool initVideoDecoder();
    bool initSDLDisplay();

    //显示视频信息
    void showVideoInfo();

    //视频的播放暂停
    bool openVideoFile();
    void startPlayback();
    void pausePlayback();
    void resumePlayback();
    void restartPlayback();
    void onPlayFinished();
    void resetToBeginning();

};
#endif // MAINWINDOW_H





