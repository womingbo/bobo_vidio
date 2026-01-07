#include "seekslider.h"

SeekSlider::SeekSlider(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
{
    // 设置滑动条样式和属性
    setTracking(true);  // 实时跟踪
    setPageStep(5000);  // 每页5秒
}

void SeekSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 计算点击位置对应的值
        double pos = (double)event->pos().x() / width();
        int value = minimum() + pos * (maximum() - minimum());

        // 设置值
        setValue(value);

        // 发送跳转信号
        emit sliderPressed();
        emit valueChanged(value);

        // 标记开始拖动
        isDragging = true;

        // 接受事件，防止父类处理
        event->accept();
        return;
    }

    QSlider::mousePressEvent(event);
}

void SeekSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (isDragging && (event->buttons() & Qt::LeftButton)) {
        // 计算当前位置对应的值
        double pos = (double)event->pos().x() / width();
        int value = minimum() + pos * (maximum() - minimum());

        // 限制在有效范围内
        value = qMax(minimum(), qMin(maximum(), value));

        // 设置值并发送信号
        setValue(value);
        emit sliderMoved(value);

        event->accept();
        return;
    }

    QSlider::mouseMoveEvent(event);
}

void SeekSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isDragging) {
        isDragging = false;
        emit sliderReleased();

        // 释放时再次确认跳转
        emit valueChanged(value());

        event->accept();
        return;
    }

    QSlider::mouseReleaseEvent(event);
}
