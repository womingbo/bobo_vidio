#ifndef VIDEOLISTITEM_H
#define VIDEOLISTITEM_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileInfo>
#include <QMouseEvent>

class VideoListItem : public QWidget
{
    Q_OBJECT
public:
    explicit VideoListItem(const QString &filePath, QWidget *parent = nullptr);

    QString getFilePath() const { return filePath; }
    void setPlaying(bool isPlaying);

signals:
    void playRequested(const QString &filePath);
    void removeRequested(const QString &filePath);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    void onRemoveButtonClicked();

private:
    void setupUI();

    QString filePath;
    QLabel *nameLabel;
    QPushButton *removeButton;
    bool isPlaying = false;
    QLabel *iconLabel;

    void updateIconColor();
};

#endif // VIDEOLISTITEM_H
