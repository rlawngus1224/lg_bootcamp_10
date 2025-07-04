#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QVector>
#include <QFile>
#include <complex>
#include <QProcess>

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onTimer();

private:
    bool openWav(const QString &path);
    void readHeader();
    QVector<std::complex<double>> fft(const QVector<std::complex<double>> &in);

    QTimer *m_timer;
    QFile  m_file;
    quint32 m_dataPos;
    quint32 m_dataSize;
    quint16 m_channels;
    quint32 m_sampleRate;
    quint16 m_bitsPerSample;

    QVector<double> m_levels;    // 이퀄라이저 바 높이
    int m_fftSize;               // FFT 윈도우 크기
    QProcess    *m_playProc;   // <-- aplay 프로세스 핸들
    int          m_intervalMs; // <-- 타이머 간격 (ms)
};
#endif // MAINWINDOW_H
