#include "audiocapture.h"
#include <QDebug>

AudioCapture::AudioCapture(int sampleRate, int channels, int bufFrames, QObject* parent)
    : QObject(parent)
    , proc_(new QProcess(this))
    , frameSize_(channels * sizeof(qint16))
    , bufBytes_(bufFrames * frameSize_)
{
    // Set process to read raw PCM from arecord
    connect(proc_, &QProcess::readyReadStandardOutput, this, &AudioCapture::handleReadyRead);
    connect(proc_, QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, &AudioCapture::handleProcessError);
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AudioCapture::handleFinished);
}

AudioCapture::~AudioCapture()
{
    stop();
}

bool AudioCapture::start()
{
    if (proc_->state() != QProcess::NotRunning)
        return true;

    QStringList args;
    // use loopback capture device
    args << "-D" << "Dhw2.0"
         << "-f" << "S16_LE"
         << "-c" << QString::number(2)
         << "-r" << QString::number(44100)
         << "--period-size" << QString::number(bufBytes_/frameSize_)
         << "--buffer-size" << QString::number(bufBytes_*4)
         << "-"; // stdout

    proc_->setProgram("arecord");
    proc_->setArguments(args);
    proc_->setReadChannel(QProcess::StandardOutput);
    proc_->start();

    if (!proc_->waitForStarted()) {
        qWarning() << "arecord 실행 실패";
        return false;
    }
    return true;
}

void AudioCapture::stop()
{
    if (proc_->state() != QProcess::NotRunning) {
        proc_->kill();
        proc_->waitForFinished(1000);
    }
}

void AudioCapture::handleReadyRead()
{
    QByteArray data = proc_->read(bufBytes_);
    if (!data.isEmpty()) {
        emit audioDataReady(data);
    }
}

void AudioCapture::handleProcessError(QProcess::ProcessError err)
{
    qWarning() << "arecord error:" << err;
}

void AudioCapture::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    qWarning() << "arecord finished:" << exitCode << status;
}
