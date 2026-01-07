#include "videolistitem.h"

VideoListItem::VideoListItem(const QString &filePath, QWidget *parent)
    : QWidget(parent), filePath(filePath)
{
    setupUI();
    setFixedHeight(80);
    setStyleSheet("background-color: #555555;");  // 黑色
}

//更新ui
void VideoListItem::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 5, 5);  // 增加左右边距


    // 视频图标
    iconLabel = new QLabel();
    iconLabel->setStyleSheet("background-color: #ffffff;");
    iconLabel->setFixedSize(2, 80);  // 固定图标大小

    // 文件名标签 - 使用弹性文本显示
    nameLabel = new QLabel(QFileInfo(filePath).fileName());
    nameLabel->setStyleSheet("font-size: 16px; color: white;");
    nameLabel->setMinimumWidth(80);   // 最小宽度
    nameLabel->setMaximumWidth(240);  // 最大宽度

    // 自动省略过长的文本
    nameLabel->setTextFormat(Qt::PlainText);
    QFontMetrics metrics(nameLabel->font());
    QString elidedText = metrics.elidedText(
                QFileInfo(filePath).fileName(),
                Qt::ElideRight,
                240  // 省略后的最大宽度
                );
    nameLabel->setText(elidedText);
    nameLabel->setToolTip(QFileInfo(filePath).fileName());  // 鼠标悬停显示完整文件名

    // 删除按钮 - 增大一些
    removeButton = new QPushButton("×");
    removeButton->setFixedSize(35, 35);
    removeButton->setStyleSheet(R"(
                                QPushButton {
                                border: none;
                                background: transparent;
                                border-radius: 14px;
                                font-size: 20px;
                                font-weight: bold;
                                color: #999;
                                }
                                QPushButton:hover {
                                background: #ff4444;
                                color: white;
                                }
                                )");

    // 添加到布局
    mainLayout->addWidget(iconLabel);
    mainLayout->addWidget(nameLabel, 1);  // 关键：设置拉伸因子
    mainLayout->addWidget(removeButton);

    // 连接信号
    connect(removeButton, &QPushButton::clicked, this, &VideoListItem::onRemoveButtonClicked);
}

void VideoListItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    emit playRequested(filePath);
    QWidget::mouseDoubleClickEvent(event);
}

void VideoListItem::onRemoveButtonClicked()
{
    emit removeRequested(filePath);
}

void VideoListItem::setPlaying(bool playing)
{
    isPlaying = playing;
    updateIconColor();

    if (isPlaying) {
        // 播放状态
        setStyleSheet("background: #e3f2fd; border-radius: 5px;");
        nameLabel->setStyleSheet("font-size: 16px; color: #2196f3; font-weight: bold;");
    } else {
        // 正常状态
        setStyleSheet("background: #555555; border-radius: 5px;");
        nameLabel->setStyleSheet("font-size: 16px; color: white;");
    }
}

void VideoListItem::updateIconColor()
{
    if (iconLabel) {
        if (isPlaying) {
            iconLabel->setStyleSheet("background-color: #00ff00;");  // 绿色
        } else {
            iconLabel->setStyleSheet("background-color: #ffffff;");  // 白色
        }
    }
}
