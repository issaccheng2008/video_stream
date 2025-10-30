#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QResizeEvent>
#include <QUrl>
#include <QImage>
#include <QStatusBar>
#include <QByteArray>
#include <QLineEdit>

namespace {
const QByteArray kJpegStartMarker("\xFF\xD8", 2);
const QByteArray kJpegEndMarker("\xFF\xD9", 2);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
{
    ui->setupUi(this);

    ui->infoLabel->setWordWrap(true);
    ui->infoLabel->setText(tr("Press Start to begin streaming. Ensure the ESP32 camera is connected."));
    ui->videoLabel->setStyleSheet(QStringLiteral("background-color: #202020; color: #dddddd;"));
    ui->videoLabel->setText(tr("No video"));

    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::startStreaming);
    connect(ui->stopButton, &QPushButton::clicked, this, &MainWindow::stopStreaming);
    connect(ui->sendButton, &QPushButton::clicked, this, &MainWindow::sendMessage);
    connect(ui->messageLineEdit, &QLineEdit::returnPressed, this, &MainWindow::sendMessage);
    connect(ui->messageLineEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        ui->sendButton->setEnabled(!text.trimmed().isEmpty());
    });

    ui->sendButton->setEnabled(false);

    updateStatus(tr("Idle"));
}

MainWindow::~MainWindow()
{
    stopStreamingInternal(QString(), false, true);
    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateVideoLabelPixmap();
}

void MainWindow::startStreaming()
{
    const QUrl url = QUrl::fromUserInput(ui->urlLineEdit->text().trimmed());
    if (!url.isValid()) {
        updateStatus(tr("Invalid URL"), true);
        return;
    }

    stopStreamingInternal(QString(), false, true);

    m_lastFrame = QPixmap();
    ui->startButton->setEnabled(false);
    ui->stopButton->setEnabled(true);
    ui->videoLabel->setPixmap(QPixmap());
    ui->videoLabel->setText(tr("Connecting..."));
    ui->infoLabel->setText(tr("Connecting to %1").arg(url.toString()));
    updateStatus(tr("Connecting..."));
    statusBar()->showMessage(tr("Connecting to %1").arg(url.toString()));

    m_buffer.clear();

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, true);
    request.setRawHeader("User-Agent", QByteArrayLiteral("QtESP32CameraViewer/1.0"));

    m_currentReply = m_networkManager->get(request);

    connect(m_currentReply, &QNetworkReply::readyRead, this, &MainWindow::handleReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &MainWindow::handleFinished);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_currentReply, &QNetworkReply::errorOccurred, this, &MainWindow::handleError);
#else
    connect(m_currentReply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
            this, &MainWindow::handleError);
#endif
}

void MainWindow::stopStreaming()
{
    stopStreamingInternal(tr("Stream stopped."), false, true);
}

void MainWindow::sendMessage()
{
    const QString message = ui->messageLineEdit->text();
    if (message.trimmed().isEmpty()) {
        return;
    }

    const QUrl streamUrl = QUrl::fromUserInput(ui->urlLineEdit->text().trimmed());
    if (!streamUrl.isValid() || streamUrl.host().isEmpty()) {
        updateStatus(tr("Invalid URL"), true);
        return;
    }

    QUrl messageUrl;
    const QString scheme = streamUrl.scheme().isEmpty() ? QStringLiteral("http") : streamUrl.scheme();
    messageUrl.setScheme(scheme);
    messageUrl.setHost(streamUrl.host());
    if (!streamUrl.userInfo().isEmpty()) {
        messageUrl.setUserInfo(streamUrl.userInfo());
    }

    const int streamPort = streamUrl.port();
    if (streamPort > 0) {
        const int messagePort = streamPort - 1;
        if (messagePort > 0) {
            messageUrl.setPort(messagePort);
        }
    }

    messageUrl.setPath(QStringLiteral("/message"));

    if (!messageUrl.isValid()) {
        updateStatus(tr("Invalid message URL"), true);
        return;
    }

    QNetworkRequest request(messageUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain; charset=utf-8"));

    QNetworkReply *reply = m_networkManager->post(request, message.toUtf8());
    ui->sendButton->setEnabled(false);
    statusBar()->showMessage(tr("Sending message..."));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            updateStatus(tr("Failed to send message: %1").arg(reply->errorString()), true);
        } else {
            statusBar()->showMessage(tr("Message sent"), 3000);
            ui->messageLineEdit->clear();
        }

        ui->sendButton->setEnabled(!ui->messageLineEdit->text().trimmed().isEmpty());
    });
}

void MainWindow::handleReadyRead()
{
    if (!m_currentReply) {
        return;
    }

    m_buffer.append(m_currentReply->readAll());

    while (true) {
        int startIndex = m_buffer.indexOf(kJpegStartMarker);
        if (startIndex < 0) {
            if (m_buffer.size() > static_cast<int>(kJpegStartMarker.size())) {
                m_buffer.remove(0, m_buffer.size() - kJpegStartMarker.size());
            }
            break;
        }

        if (startIndex > 0) {
            m_buffer.remove(0, startIndex);
        }

        int endIndex = m_buffer.indexOf(kJpegEndMarker, kJpegStartMarker.size());
        if (endIndex < 0) {
            break;
        }

        const int frameSize = endIndex + kJpegEndMarker.size();
        QByteArray frameData = m_buffer.left(frameSize);
        m_buffer.remove(0, frameSize);

        displayFrame(frameData);
    }
}

void MainWindow::handleFinished()
{
    if (!m_currentReply) {
        updateStatus(tr("Idle"));
        return;
    }

    stopStreamingInternal(tr("Stream closed by server."), false, false);
}

void MainWindow::handleError(QNetworkReply::NetworkError)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    const QString message = reply ? reply->errorString() : tr("Unknown network error");
    stopStreamingInternal(tr("Error: %1").arg(message), true, true);
}

void MainWindow::displayFrame(const QByteArray &frameData)
{
    QImage image = QImage::fromData(frameData, "JPG");
    if (image.isNull()) {
        return;
    }

    m_lastFrame = QPixmap::fromImage(image);
    updateVideoLabelPixmap();
    ui->videoLabel->setText(QString());
    ui->infoLabel->setText(tr("Resolution: %1 × %2").arg(m_lastFrame.width()).arg(m_lastFrame.height()));
    updateStatus(tr("Streaming"));
}

void MainWindow::updateStatus(const QString &message, bool isError)
{
    ui->statusLabel->setText(message);
    ui->statusLabel->setStyleSheet(isError ? QStringLiteral("color:#b00020;")
                                           : QStringLiteral("color:#202020;"));
    statusBar()->showMessage(message);
}

void MainWindow::updateVideoLabelPixmap()
{
    if (m_lastFrame.isNull()) {
        ui->videoLabel->setPixmap(QPixmap());
        return;
    }

    const QPixmap scaled = m_lastFrame.scaled(ui->videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->videoLabel->setPixmap(scaled);
}

void MainWindow::stopStreamingInternal(const QString &message, bool isError, bool abortReply)
{
    if (m_currentReply) {
        if (abortReply) {
            disconnect(m_currentReply, nullptr, this, nullptr);
            m_currentReply->abort();
        }
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    m_buffer.clear();

    ui->startButton->setEnabled(true);
    ui->stopButton->setEnabled(false);

    if (isError || message == tr("Stream stopped.") || message == tr("Stream closed by server.") || message.isEmpty()) {
        m_lastFrame = QPixmap();
        ui->videoLabel->setPixmap(QPixmap());
        ui->videoLabel->setText(tr("No video"));
    }

    if (!message.isEmpty()) {
        updateStatus(message, isError);
        if (isError) {
            ui->infoLabel->setText(message);
        } else if (message != tr("Streaming")) {
            ui->infoLabel->setText(tr("Press Start to begin streaming."));
        }
    } else if (!isError) {
        updateStatus(tr("Idle"));
        ui->infoLabel->setText(tr("Press Start to begin streaming."));
    }
}
