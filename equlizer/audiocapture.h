// AudioCapture.h
#pragma once

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <alsa/asoundlib.h>

class AudioCapture : public QObject
{
    Q_OBJECT
public:
    explicit AudioCapture(int sampleRate = 44100,
                          int channels   = 2,
                          int bufFrames  = 1024,
                          QObject* parent = nullptr);
    ~AudioCapture();

    bool start();
    void stop();
    void capture();

signals:
    // 캡처된 PCM 데이터를 QByteArray로 전달
    void audioDataReady(const QByteArray& pcm);





private:
    snd_pcm_t*    pcmHandle_;
    QTimer*       timer_;
    int           frameSize_;    // 한 프레임당 바이트 수
    int           bufFrames_;    // 한 번에 읽을 프레임 수

};