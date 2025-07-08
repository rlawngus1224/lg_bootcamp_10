#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QVector>
#include <QFile>
#include <complex>
#include <QProcess>
#include <QPushButton>
#include <deque>
#include <alsa/asoundlib.h>
#include "audiocapture.h"
#include <QMutex>

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onTimer();
    void onDistanceTimer();  // 거리 측정용 타이머 슬롯
    void runP2PCommands();
    void onAudioData(const QByteArray &pcm);

private:
    bool openWav(const QString &path);
    void readHeader();
    QVector<std::complex<double>> fft(const QVector<std::complex<double>> &in);
    AudioCapture* m_capturer;
    std::deque<QByteArray> m_pcmQueue;      // 들어오는 청크 저장
    QMutex                 m_pcmQueueLock; // 안전한 접근용
    // US100 관련 함수
    bool initializeUS100();
    int readUS100Distance();
    void updateVolume(int distance);
    int m_currentDistance;     // 현재 거리값 (mm)
    int m_currentVolume;       // 현재 볼륨값 (%)
    QTimer *m_timer;
    QFile  m_file;
    quint32 m_dataPos;
    quint32 m_dataSize;
    quint16 m_channels;
    quint32 m_sampleRate;
    quint16 m_bitsPerSample;
    QVector<std::complex<double>> m_fftBuffer;
    int       m_samplesPerFrame;
    QPushButton *m_button;

    void initVolumeControl();          // ALSA mixer 초기화
    void cleanupVolumeControl();       // ALSA mixer 정리

    snd_mixer_t     *m_mixerHandle;    // ALSA mixer 핸들
    snd_mixer_elem_t*m_mixerElem;      // ALSA mixer element
    long             m_volMin;         // ALSA 최소 볼륨 값
    long             m_volMax;         // ALSA 최대 볼륨 값

    QVector<double> m_levels;    // 이퀄라이저 바 높이
    int m_fftSize;               // FFT 윈도우 크기
    QProcess    *m_playProc;   // <-- aplay 프로세스 핸들
    int          m_intervalMs; // <-- 타이머 간격 (ms)
    // US100 및 볼륨 제어 관련 멤버
    int m_serialFd;                     // 시리얼 포트 파일 디스크립터
    QTimer *m_distanceTimer;           // 거리 측정용 타이머    
    // 설정값
    static constexpr int DIST_MIN_MM = 100;    // 최소 거리
    static constexpr int DIST_MAX_MM = 1000;   // 최대 거리
    static constexpr int VOL_MIN_PCT = 50;     // 최소 볼륨 %
    static constexpr int VOL_MAX_PCT = 70;     // 최대 볼륨 %
};
#endif // MAINWINDOW_H
