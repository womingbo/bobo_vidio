// SeekSlider.h
#include <QSlider>
#include <QMouseEvent>

class SeekSlider : public QSlider
{
    Q_OBJECT
public:
    explicit SeekSlider(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool isDragging = false;
};
