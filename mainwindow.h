#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QByteArray>
#include <QPixmap>
#include <QNetworkReply>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QNetworkAccessManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void startStreaming();
    void stopStreaming();
    void handleReadyRead();
    void handleFinished();
    void handleError(QNetworkReply::NetworkError code);

private:
    void displayFrame(const QByteArray &frameData);
    void updateStatus(const QString &message, bool isError = false);
    void updateVideoLabelPixmap();
    void stopStreamingInternal(const QString &message, bool isError, bool abortReply);

    Ui::MainWindow *ui;
    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;
    QByteArray m_buffer;
    QPixmap m_lastFrame;
};
#endif // MAINWINDOW_H
