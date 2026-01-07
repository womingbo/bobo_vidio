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
#include <QtConcurrent>
#include "videothread.h"
#include "audiothread.h"
#include "global_status.h"
#include "videolistitem.h"

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

public:
    void seekVideoRealTime(qint64 positionMs);

    void addVideoToPlaylist(const QString &filename);

private slots:
    void on_add_button_clicked();

    void on_start_button_clicked();

    void UpadatButton(bool flag);

    void UpadatStatus(QString time,double value);

    void UpadatseekSlider(double value);

    void on_speed_button_clicked();

    void onSeekSliderPressed();

    void onSeekSliderReleased();

    void onSeekSliderMoved(int value);



    void on_private_button_pressed();

    void on_private_button_released();

    void on_next_button_pressed();

    void on_next_button_released();

    void onVideoPlayRequested(const QString &filePath);

    void onVideoRemoveRequested(const QString &filePath);

    void setstarting(const QString &filePath);


    void on_del_button_pressed();

    void on_del_button_released();

    void on_horizontalSlider_valueChanged(int value);


signals:
    void init_video(QString filename);

    void init_audio(QString filename);

    void UpadatStatus();

    void UpadatSpeed(float speed);

    void UpadatSeekSlider(int,int);

    void setVolume(float volume);


private:
    Ui::MainWindow *ui;

private:

    VideoThread * video = nullptr;
    QThread *t_video = nullptr;
    AudioThread *audio = nullptr;
    QThread *t_audio = nullptr;
    float speed = 1.0f;


    QString current_time = nullptr;
    int time = 0;  //一次前进后退的时间间隔
    int totall_time = 0;  //总时长

    QString currentPlayingFile = nullptr;
    int currentPlayIndex = 0;




};
#endif // MAINWINDOW_H





