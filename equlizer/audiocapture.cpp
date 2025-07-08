#include "audiocapture.h"
#include <QDebug>

AudioCapture::AudioCapture(int sampleRate, int channels, int bufFrames, QObject* parent)
    : QObject(parent)
    , pcmHandle_(nullptr)
    , bufFrames_(bufFrames)
    , timer_(nullptr)
{
    // 프레임당 바이트 = 채널 × 샘플당 바이트(16bit = 2바이트)
    frameSize_ = channels * sizeof(qint16);


}

AudioCapture::~AudioCapture()
{
    stop();
}

bool AudioCapture::start()
{
    int err;
    // loopback 캡처 장치: "plughw:Loopback,1"
    // 또는 asoundrc 에 default를 loopin 으로 설정했다면 "default"로 열어도 됩니다.
    if ((err = snd_pcm_open(&pcmHandle_,
                            "hw:2,0",
                            SND_PCM_STREAM_CAPTURE,
                            0)) < 0)
    {
        qWarning("Unable to open PCM device: %s", snd_strerror(err));
        return false;
    }
    // 하드웨어 파라미터 기본 설정
    snd_pcm_set_params(pcmHandle_,
                       SND_PCM_FORMAT_S16_LE,  // 16-bit little endian
                       SND_PCM_ACCESS_RW_INTERLEAVED,
                       2,                      // 스테레오
                       44100,                  // 샘플레이트
                       1,                      // 소프트웨어 resample 허용
                       500000);                // 지연 0.5초

    if (!timer_) {
            timer_ = new QTimer(this);
            // bufFrames_ / sampleRate 간격으로 호출
            //int intervalMs = static_cast<int>(1000.0 * bufFrames_ / 44100.0);
            timer_->setInterval(100);
            connect(timer_, &QTimer::timeout, this, &AudioCapture::capture);
        }
        timer_->start();
    return true;
}

void AudioCapture::stop()
{
    if (timer_) timer_->stop();
    if (pcmHandle_) {
        snd_pcm_close(pcmHandle_);
        pcmHandle_ = nullptr;
    }
}

void AudioCapture::capture()
{
//    qDebug() << "debug20";
    if (!pcmHandle_) return;
//    qDebug() << "debug22";
    int bytesToRead = bufFrames_ * frameSize_;
    QByteArray buffer(bytesToRead, 0);

    // ALSA에서 PCM 프레임 읽기
    snd_pcm_wait(pcmHandle_, 1000);
    int framesRead = snd_pcm_readi(pcmHandle_,
                                   buffer.data(),
                                   bufFrames_);
//    qDebug() << "debug21";
    if (framesRead < 0) {
        // 언더런 발생 시 다시 준비
        snd_pcm_prepare(pcmHandle_);
        return;
    }
    // 실제 읽은 바이트 수
    int bytesRead = framesRead * frameSize_;
    buffer.resize(bytesRead);

//    qDebug() << "debug2";
    emit audioDataReady(buffer);
}
