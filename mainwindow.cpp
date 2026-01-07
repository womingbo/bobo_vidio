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

    ui->start_button->setIcon(QIcon(":/pictrues/start.png"));


    GlobalVars::playerState = STATE_IDLE;


//    connect(ui->seekSlider, &QSlider::sliderPressed,
//            this, &MainWindow::onSeekSliderPressed);
//    connect(ui->seekSlider, &QSlider::sliderReleased,
//            this, &MainWindow::onSeekSliderReleased);
//    connect(ui->seekSlider, &QSlider::valueChanged,
//            this, &MainWindow::onSeekSliderMoved);

    connect(ui->seekSlider, &SeekSlider::valueChanged,
            this, &MainWindow::onSeekSliderMoved);
    connect(ui->seekSlider, &SeekSlider::sliderPressed,
            this, &MainWindow::onSeekSliderPressed);
    connect(ui->seekSlider, &SeekSlider::sliderReleased,
            this, &MainWindow::onSeekSliderReleased);

    //启动视频线程
    video = new VideoThread;
    t_video = new QThread;
    video->moveToThread(t_video);
    video->setDisplayWidget(ui->videoWidget);
    connect(this,SIGNAL(init_video(QString)),video,SLOT(init_video(QString)));
    connect(video,SIGNAL(UpadatButton(bool)),this,SLOT(UpadatButton(bool)));//更新按钮状态、
    connect(video,SIGNAL(UpadatStatus(QString,double)),this,SLOT(UpadatStatus(QString,double)));//更新滑动条时间状态
    connect(video,SIGNAL(UpadatseekSlider(double)),this,SLOT(UpadatseekSlider(double)));//更新滑动条最大状态显示
    connect(this,SIGNAL(UpadatStatus()),video,SLOT(UpadatStatus()));//更新播放状态
    connect(this,SIGNAL(UpadatSpeed(float)),video,SLOT(setPlaybackSpeed(float)));//更新播放速度
    connect(this,SIGNAL(UpadatSeekSlider(int,int)),video,SLOT(setSeekSlider(int,int)));//拖动滑动条
    t_video->start();


    audio = new AudioThread;
    t_audio = new QThread;
    audio->moveToThread(t_audio);
    connect(this,SIGNAL(init_audio(QString)),audio,SLOT(init_audio(QString)));
    //  t_audio->start();

    ui->speed_button->setText(QString::number(speed) +"X");

    ui->label->setVisible(true);
    ui->label_2->setVisible(true);
    ui->label_3->setVisible(true);

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onSeekSliderPressed()
{
    emit UpadatSeekSlider(0,0);
}

void MainWindow::onSeekSliderReleased()
{
    int value = ui->seekSlider->value();
    emit UpadatSeekSlider(2,value);
}

void MainWindow::onSeekSliderMoved(int value)
{
    //    if(!m_isSeeking) return;
    //    static QElapsedTimer timer;
    //    emit UpadatSeekSlider(1,value);
}


void MainWindow::on_add_button_clicked()
{

    QString filename = QFileDialog::getOpenFileName(this,"选择视频文件",QDir::homePath(), "视频文件 (*.mp4 *.avi *.mov *.mkv);;所有文件 (*.*)");
    if(filename.isEmpty()){
        return;
    }

    ui->label->setVisible(false);
    ui->label_2->setVisible(false);
    ui->label_3->setVisible(false);

    emit init_video(filename);
    //  emit init_audio(filename);
}


void MainWindow::on_start_button_clicked()
{
    switch (GlobalVars::playerState)
    {
    case STATE_IDLE:
        // 还没打开文件，先打开

        break;

    case STATE_READY:
        // 文件已就绪，开始播放
        ui->start_button->setIcon(QIcon(":/pictrues/stop.png"));

        break;

    case STATE_PLAYING:
        // 正在播放，暂停
        ui->start_button->setIcon(QIcon(":/pictrues/start.png"));
        GlobalVars::playerState = STATE_PAUSED;
        break;

    case STATE_PAUSED:
        // 已暂停，继续播放
        ui->start_button->setIcon(QIcon(":/pictrues/stop.png"));
        GlobalVars::playerState = STATE_PLAYING;
        break;

    case STATE_ENDED:
        // 播放结束，重新开始
        ui->start_button->setIcon(QIcon(":/pictrues/stop.png"));
        break;
    }
    emit UpadatStatus();
}

void MainWindow::UpadatButton(bool flag)
{
    ui->start_button->setIcon(QIcon(QString(":/pictrues/%1.png").arg(flag ? "stop" : "start")));
}

void MainWindow::UpadatStatus(QString time,double value)
{
    current_time = time.section('/', 0, 0);
    ui->time_label->setText(time);
    ui->seekSlider->setValue(value);
}

void MainWindow::UpadatseekSlider(double value)
{
    totall_time = value;
    time = value/20;
    ui->seekSlider->setMaximum(value * 1000);
}

void MainWindow::on_speed_button_clicked()
{
    if(speed == 0.5)
    {
        speed = 1.0;
    }
    else if(speed == 1.0)
    {
        speed = 1.5;
    }
    else if(speed == 1.5)
    {
        speed = 2.0;
    }
    else if(speed == 2.0)
    {
        speed = 0.5;
    }
    ui->speed_button->setText(QString::number(speed) +"X");
    emit UpadatSpeed(speed);
}

void MainWindow::on_private_button_pressed()
{
    emit UpadatSeekSlider(0,0);
}

void MainWindow::on_private_button_released()
{
    int minutes = current_time.section(':', 0, 0).toInt();
    int seconds = current_time.section(':', 1, 1).toInt();
    int current_seconds = minutes * 60 + seconds;
    int total_second_n = 0;

    total_second_n = (current_seconds - time < 0) ? 0 :(current_seconds - time);
    emit UpadatSeekSlider(2,total_second_n * 1000);
}

void MainWindow::on_next_button_pressed()
{
    emit UpadatSeekSlider(0,0);
}

void MainWindow::on_next_button_released()
{
    int minutes = current_time.section(':', 0, 0).toInt();
    int seconds = current_time.section(':', 1, 1).toInt();
    int current_seconds = minutes * 60 + seconds;
    int total_second_n = 0;

    total_second_n = (current_seconds + time > totall_time) ? totall_time :(current_seconds + time);
    emit UpadatSeekSlider(2,total_second_n * 1000);
}
