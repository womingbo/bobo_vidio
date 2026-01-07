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
    connect(this,SIGNAL(UpadatStatus()),audio,SLOT(UpadatStatus()));//更新播放状态
    connect(this,SIGNAL(setVolume(float)),audio,SLOT(setVolume(float)));//更新音量
    connect(this,SIGNAL(UpadatSpeed(float)),audio,SLOT(setSpeed(float)));//更新播放速度
    t_audio->start();


    video->setAudioReference(audio);
    ui->speed_button->setText(QString::number(speed) +"X");

    ui->label->setVisible(true);
    ui->label_2->setVisible(true);
    ui->label_3->setVisible(true);
    ui->listWidget->setSpacing(3);

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


    addVideoToPlaylist(filename);

    setstarting(filename);
    emit init_video(filename);
    emit init_audio(filename);
}

void MainWindow::addVideoToPlaylist(const QString &filename)
{
    // 检查是否已存在
    for (int i = 0; i < ui->listWidget->count(); ++i) {
        QListWidgetItem *item = ui->listWidget->item(i);
        if (item->data(Qt::UserRole).toString() == filename) {
            // 已存在，更新显示
            VideoListItem *widget = qobject_cast<VideoListItem*>(
                        ui->listWidget->itemWidget(item)
                        );
            if (widget) {
                widget->setPlaying(false);
            }
            return;
        }
    }

    // 创建自定义列表项
    VideoListItem *widget = new VideoListItem(filename);

    // 创建 QListWidgetItem
    QListWidgetItem *item = new QListWidgetItem();

    // 关键：设置大小！
    item->setSizeHint(QSize(300, 80));
    item->setData(Qt::UserRole, filename);

    // 添加到列表
    ui->listWidget->addItem(item);
    ui->listWidget->setItemWidget(item, widget);

    // 连接信号
    connect(widget, &VideoListItem::playRequested,
            this, &MainWindow::onVideoPlayRequested);
    connect(widget, &VideoListItem::removeRequested,
            this, &MainWindow::onVideoRemoveRequested);
}

void MainWindow::onVideoPlayRequested(const QString &filePath)
{
    setstarting(filePath);

    ui->label->setVisible(false);
    ui->label_2->setVisible(false);
    ui->label_3->setVisible(false);
    emit init_video(filePath);

}

void MainWindow::onVideoRemoveRequested(const QString &filePath)
{
    // 1. 如果是当前正在播放的文件，先停止播放
    if (currentPlayingFile == filePath) {
        ui->start_button->setIcon(QIcon(":/pictrues/start.png"));
        emit UpadatSeekSlider(0,0);
    }

    // 2. 从播放列表中移除
    for (int i = 0; i < ui->listWidget->count(); ++i) {
        if (ui->listWidget->item(i)->data(Qt::UserRole).toString() == filePath) {
            delete ui->listWidget->takeItem(i);
            break;
        }
    }
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
    // 使用浮点数比较，注意加f后缀
    if (qFuzzyCompare(speed, 0.5f)) {
        speed = 1.0f;
    } else if (qFuzzyCompare(speed, 1.0f)) {
        speed = 1.5f;
    } else if (qFuzzyCompare(speed, 1.5f)) {
        speed = 2.0f;
    } else if (qFuzzyCompare(speed, 2.0f)) {
        speed = 0.5f;
    } else {
        // 默认值
        speed = 1.0f;
    }

    // 更新UI（去掉小数部分）
    int displaySpeed = static_cast<int>(speed * 10);
    if (displaySpeed % 10 == 0) {
        // 整数倍
        ui->speed_button->setText(QString::number(static_cast<int>(speed)) + "X");
    } else {
        // 小数倍
        ui->speed_button->setText(QString::number(speed, 'f', 1) + "X");
    }

    qDebug() << "speed:" << speed;
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

void MainWindow::setstarting(const QString &filePath)
{
    // 1. 找到要播放的项
    int targetIndex = -1;
    for (int i = 0; i < ui->listWidget->count(); ++i) {
        QListWidgetItem *item = ui->listWidget->item(i);
        if (item->data(Qt::UserRole).toString() == filePath) {
            targetIndex = i;
            break;
        }
    }

    if (targetIndex == -1) return;

    // 2. 更新所有项的播放状态
    for (int i = 0; i < ui->listWidget->count(); ++i) {
        QListWidgetItem *item = ui->listWidget->item(i);
        VideoListItem *widget = qobject_cast<VideoListItem*>(
                    ui->listWidget->itemWidget(item)
                    );

        if (widget) {
            widget->setPlaying(i == targetIndex);
        }
    }

    currentPlayingFile = filePath;
    currentPlayIndex = targetIndex;
}


void MainWindow::on_del_button_pressed()
{
    ui->start_button->setIcon(QIcon(":/pictrues/start.png"));
    emit UpadatSeekSlider(0,0);
}

void MainWindow::on_del_button_released()
{
    // 直接清空所有项
    ui->listWidget->clear();

    // 同时清理相关状态
    currentPlayingFile.clear();
    currentPlayIndex = -1;
}

void MainWindow::on_horizontalSlider_valueChanged(int value)
{
    // 将 0-300 映射到 0.0-3.0
    float floatValue = value / 100.0f;
    emit setVolume(floatValue);
}

