#pragma once

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QTimer>

class AudioCapture : public QObject
{
    Q_OBJECT
public:
    explicit AudioCapture(int sampleRate = 44100,
                          int channels   = 2,
                          int bufFrames  = 4410,
                          QObject* parent = nullptr);
    ~AudioCapture();

    bool start();
    void stop();

signals:
    void audioDataReady(const QByteArray& pcm);

private slots:
    void handleReadyRead();
    void handleProcessError(QProcess::ProcessError err);
    void handleFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess*    proc_;
    int          frameSize_;    // bytes per frame
    int          bufBytes_;     // bytes per read
};
