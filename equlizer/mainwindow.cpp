#include "mainwindow.h"
#include <QPainter>
#include <QtMath>
#include <complex>
#include <QProcess>
#include <QMessageBox>
#include <QResizeEvent>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <algorithm>
#include <QDebug> // 디버깅용 로그 출력을 위해 추가
#include "audiocapture.h"
#include <QThread>
#include <QObject>
#include <QString>
#include <QStringList>


// 간단 Cooley–Tuk FFT (in.size() == power of two)
QVector<std::complex<double>> MainWindow::fft(const QVector<std::complex<double>> &in)
{
    int n = in.size();
    if (n == 1) return in;

    QVector<std::complex<double>> even(n/2), odd(n/2);
    for(int i=0;i<n/2;i++){
        even[i] = in[i*2];
        odd[i]  = in[i*2+1];
    }
    auto Fe = fft(even);
    auto Fo = fft(odd);

    QVector<std::complex<double>> out(n);
    for(int k=0;k<n/2;k++){
        std::complex<double> t = std::polar(1.0, -2*M_PI*k/n) * Fo[k];
        out[k]       = Fe[k] + t;
        out[k+n/2]  = Fe[k] - t;
    }
    return out;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_timer(new QTimer(this)),
      m_playProc(nullptr),
      m_dataPos(0),
      m_dataSize(0),
      m_fftSize(1024),
      m_serialFd(-1),
      m_distanceTimer(new QTimer(this)),
      m_currentDistance(0),
      m_currentVolume(0),
      m_mixerHandle(nullptr), // ALSA 핸들 초기화
      m_mixerElem(nullptr)    // ALSA 요소 초기화
{
    setMinimumSize(600, 300);
    m_levels.resize( m_fftSize/2 );
    if (!openWav("/mnt/nfs/test_contents/test.wav")) {
        qFatal("WAV open failed");
    }
    m_button = new QPushButton("Sync", this);
    connect(m_button, &QPushButton::clicked, this, &MainWindow::runP2PCommands);
    initVolumeControl();
    // 1) aplay 프로세스 준비 (stdin으로 PCM 받아 재생)
    m_playProc = new QProcess(this);
/*
    QString amixerProg = "./amixer";
    QStringList amixerArgs;
    amixerArgs << "-c" << "0"
               << "cset" << "numid=1" << "80%";
    // 동기 실행(결과 코드가 필요 없으면 execute, 필요하면 반환값 체크)
    QProcess::execute(amixerProg, amixerArgs);
*/
    // WAV 파일 재생
    QString aplayProg = "./aplay";
    QStringList aplayArgs;
    aplayArgs << "-Dhw:0,0"
              << "-f"   << "S16_LE"
              << "-c"   << "2"
              << "-r"   << "44100"
              //<< "-t" << "raw"
              << "/mnt/nfs/test_contents/test.wav";
    // 비동기 실행(앱이 블록되지 않고 바로 리턴)
    m_playProc->start(aplayProg, aplayArgs);


    if (!m_playProc->waitForStarted()) {
        QMessageBox::critical(this, "Error", "aplay 실행 실패");
        return;
    }

    // ——— 10FPS용 계산 ———
    // 1/10초마다 읽을 샘플 수
    m_samplesPerFrame = int(double(m_sampleRate) / 10.0);
    // 링버퍼 초기화
    m_fftBuffer.reserve(m_fftSize);

    connect(m_timer, &QTimer::timeout, this, &MainWindow::onTimer);
    m_timer->start(100);

    // 1) 스레드 생성
    QThread* audioThread = new QThread(this);

    // 2) 오디오 캡처 객체 생성
    m_capturer = new AudioCapture(44100, 2, 4410, nullptr); //4410 reason: 44100/10fps
    m_capturer->moveToThread(audioThread);
    connect(audioThread, &QThread::started, m_capturer, &AudioCapture::start);
    connect(audioThread, &QThread::finished, m_capturer, &QObject::deleteLater);
    connect(this, &MainWindow::destroyed, audioThread, &QThread::quit);
    connect(audioThread, &QThread::finished, audioThread, &QObject::deleteLater);

    // 6) audioThread 시작
    audioThread->start();

    qDebug() << "debug3";
    connect(m_capturer, &AudioCapture::audioDataReady,
               this,        &MainWindow::onAudioData);

    qDebug() << "debug4";
        // US100 초기화
    if (!initializeUS100()) {
        QMessageBox::warning(this, "Error", "Failed to initialize US100 sensor");
    } else {
        // 거리 측정 타이머 설정 (100ms 간격)
        connect(m_distanceTimer, &QTimer::timeout, this, &MainWindow::onDistanceTimer);
        m_distanceTimer->start(100);
    }

    qDebug() << "debug5";

}

MainWindow::~MainWindow()
{
    cleanupVolumeControl();
    m_timer->stop();
    if (m_playProc) {
        m_playProc->closeWriteChannel();
        m_playProc->terminate();
        m_playProc->waitForFinished();
    }
    m_file.close();

    if (m_serialFd >= 0) {
        ::close(m_serialFd);
    }
    delete m_distanceTimer;
    if (m_capturer) {
        QMetaObject::invokeMethod(m_capturer, "stop", Qt::QueuedConnection);
    }
}

void MainWindow::onAudioData(const QByteArray &pcm)
{
    QMutexLocker lk(&m_pcmQueueLock);
    m_pcmQueue.push_back(pcm);
    if (m_pcmQueue.size() > 5) m_pcmQueue.pop_front();
}

// ALSA 믹서 초기화 함수
void MainWindow::initVolumeControl()
{
    const char* card = "default";
    const char* selem_name = "PCM"; // 대부분 "Master" 또는 "PCM" 입니다. 'amixer scontrols'로 확인하세요.

    if (snd_mixer_open(&m_mixerHandle, 0) < 0) {
        qWarning("ALSA: snd_mixer_open failed");
        return;
    }
    if (snd_mixer_attach(m_mixerHandle, card) < 0) {
        qWarning("ALSA: snd_mixer_attach failed");
        snd_mixer_close(m_mixerHandle); m_mixerHandle = nullptr;
        return;
    }
    if (snd_mixer_selem_register(m_mixerHandle, NULL, NULL) < 0) {
        qWarning("ALSA: snd_mixer_selem_register failed");
        snd_mixer_close(m_mixerHandle); m_mixerHandle = nullptr;
        return;
    }
    if (snd_mixer_load(m_mixerHandle) < 0) {
        qWarning("ALSA: snd_mixer_load failed");
        snd_mixer_close(m_mixerHandle); m_mixerHandle = nullptr;
        return;
    }

    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, selem_name);
    m_mixerElem = snd_mixer_find_selem(m_mixerHandle, sid);

    if (!m_mixerElem) {
        qWarning() << "ALSA: Mixer element not found:" << selem_name;
        snd_mixer_close(m_mixerHandle);
        m_mixerHandle = nullptr;
        return;
    }

    // 재생 볼륨의 물리적 범위 가져오기
    snd_mixer_selem_get_playback_volume_range(m_mixerElem, &m_volMin, &m_volMax);

    // 초기 볼륨을 80%로 설정
    long initial_vol = (m_volMax - m_volMin) * 80 / 100 + m_volMin;
    snd_mixer_selem_set_playback_volume_all(m_mixerElem, initial_vol);
    m_currentVolume = 80;
}

// ALSA 리소스 정리 함수
void MainWindow::cleanupVolumeControl()
{
    if (m_mixerHandle) {
        snd_mixer_close(m_mixerHandle);
        m_mixerHandle = nullptr;
    }
}

bool MainWindow::openWav(const QString &path)
{
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) return false;
    readHeader();
    return true;
}


void MainWindow::resizeEvent(QResizeEvent *event)
{
    int w = width(), h = height();
    int topH = h / 4;
    int cellW = w / 3;


    m_button->setGeometry(cellW * 2, 0, cellW, topH);

    QMainWindow::resizeEvent(event);
}

void MainWindow::readHeader()
{
    QDataStream in(&m_file);
    in.setByteOrder(QDataStream::LittleEndian);

    char riff[4];
    in.readRawData(riff,4);            // "RIFF"
    quint32 chunkSize; in >> chunkSize;
    char wave[4]; in.readRawData(wave,4); // "WAVE"

    // fmt subchunk
    char fmt[4]; in.readRawData(fmt,4);    // "fmt "
    quint32 subSize; in >> subSize;        // usually 16
    quint16 audioFormat; in >> audioFormat; // PCM = 1
    in >> m_channels;
    in >> m_sampleRate;
    quint32 byteRate; in >> byteRate;
    quint16 blockAlign; in >> blockAlign;
    in >> m_bitsPerSample;
    // skip any extra fmt bytes
    if (subSize > 16) m_file.skip(subSize - 16);

    // data subchunk
    char dataTag[4];
    in.readRawData(dataTag,4);         // "data"
    in >> m_dataSize;
    m_dataPos = m_file.pos();
}

void MainWindow::onTimer()
{
    qDebug() << "debug10";
    QByteArray buf;
    {
        QMutexLocker lk(&m_pcmQueueLock);
        if (!m_pcmQueue.empty()) {
            buf = m_pcmQueue.front();
            m_pcmQueue.pop_front();
        }
    }
    qDebug() << "debug12";

    if (buf.isEmpty()) {
    // 아직 캡처된 데이터가 없으면 그림만 갱신
        update();
        return;
    }
    qDebug() << "debug11";
    const int bytesPerSample = m_bitsPerSample/8;
    const int chunkBytes    = m_samplesPerFrame * bytesPerSample * m_channels;
    //QByteArray buf = m_file.read(chunkBytes);


    // 1) aplay 프로세스에 똑같은 버퍼 쓰기 → 정확히 이 타이밍의 오디오 출력
    //m_playProc->write(buf);
    qDebug() << "debug13";

    // (2) 읽은 샘플을 FFT 링 버퍼에 추가 (모노 변환)
    for (int i = 0; i < m_samplesPerFrame; ++i) {
        int offset = i * m_channels * bytesPerSample;
        // 16bit PCM 가정
        const char *p = buf.constData() + offset;
        qint16 sample = *reinterpret_cast<const qint16*>(p);
        double norm = double(sample) / 32768.0;
        // push back, 버퍼가 너무 크면 앞에서 pop
        m_fftBuffer.push_back({norm, 0.0});
        if (m_fftBuffer.size() > quint32(m_fftSize))
            m_fftBuffer.pop_front();
    }
    qDebug() << "debug14";
    // (3) 충분히 쌓였으면 FFT 수행
    if (m_fftBuffer.size() == quint32(m_fftSize)) {
        auto spectrum = fft(m_fftBuffer);
        int half = spectrum.size() / 2;
        for (int i = 0; i < half; ++i) {
            m_levels[i] = std::abs(spectrum[i]) / half;
        }
        update();  // paintEvent 트리거
    }
    qDebug() << "debug15";
}

void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    int w = width(), h = height();
    int topH = h / 4;
    int botY = topH;
    int botH = h - topH;
    int cellW = w / 3;
    // ─────── 경계선 그리기 ───────
    p.setPen(Qt::white);
    // 상단/하단 경계 수평선
    p.drawLine(0, topH, w, topH);
    // 상단 영역 세로 분할선 (B/C, C/D, D/E)
    for (int i = 1; i < 3; ++i) {
        p.drawLine(cellW * i, 0, cellW * i, topH);
    }
    // ────────────────────────────

    QFont font = p.font();
    font.setPointSize(14);
    p.setFont(font);

    QString volumeText = QString("volume: %1%").arg(m_currentVolume);
    QString distanceText = QString("distance: %1mm").arg(m_currentDistance);

    struct { int x; QString txt; } labels[] = {
        { 0*cellW, volumeText },
        { 1*cellW, distanceText }
    };
    for (auto &L : labels) {
        QRect area(L.x, 0, cellW, topH);
        p.drawText(area, Qt::AlignCenter, L.txt);
    }

    // ——— 하단 영역 이퀄라이저 그리기 ———
    int barCount = m_levels.size();
    double barW = double(w) / barCount;

    p.setPen(Qt::NoPen);
    for (int i = 0; i < barCount; ++i) {
        double level = qMin(m_levels[i] * 50.0, 1.0);
        // 높이 계산
        double barH = botH * level;
        QRectF bar(
            i * barW,
            botY + (botH - barH),
            barW * 0.8,
            barH
        );
        p.setBrush(QColor::fromHsv((i * 360 / barCount), 255, 200));
        p.drawRect(bar);
    }
    qDebug() << "debug30";
}

bool MainWindow::initializeUS100()
{
    const char *SERIAL_DEV = "/dev/ttyUSB0";
    m_serialFd = ::open(SERIAL_DEV, O_RDWR | O_NOCTTY | O_SYNC);
    if (m_serialFd < 0) {
        return false;
    }

    termios tty = {};
    if (tcgetattr(m_serialFd, &tty) != 0) {
        ::close(m_serialFd);
        m_serialFd = -1;
        return false;
    }

    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag &= ~CRTSCTS;

    tcflush(m_serialFd, TCIOFLUSH);
    if (tcsetattr(m_serialFd, TCSANOW, &tty) != 0) {
        ::close(m_serialFd);
        m_serialFd = -1;
        return false;
    }

    return true;
}

int MainWindow::readUS100Distance()
{
    if (m_serialFd < 0) return -1;

    uint8_t cmd = 0x55;
    if (::write(m_serialFd, &cmd, 1) != 1) return -1;

    uint8_t buf[2];
    int got = 0;
    int wait = 0;
    constexpr int TIMEOUT_MS = 100;

    while (got < 2 && wait < TIMEOUT_MS) {
        int n = ::read(m_serialFd, buf + got, 2 - got);
        if (n > 0) {
            got += n;
        } else {
            usleep(1000);  // 1ms delay
            wait++;
        }
    }

    if (got != 2) return -1;
    return (buf[0] << 8) | buf[1];
}

void MainWindow::updateVolume(int distance)
{
    // ALSA 핸들이 유효하지 않으면 아무것도 하지 않음
    if (!m_mixerElem) return;

    // 거리가 범위를 벗어나면 조정
    if (distance < DIST_MIN_MM) distance = DIST_MIN_MM;
    if (distance > DIST_MAX_MM) distance = DIST_MAX_MM;

    // 거리를 볼륨 퍼센트로 변환 (선형)
    long volume_pct = VOL_MIN_PCT + (long)(VOL_MAX_PCT - VOL_MIN_PCT) * (distance - DIST_MIN_MM) / (DIST_MAX_MM - DIST_MIN_MM);

    // 퍼센트를 실제 ALSA 볼륨 값으로 변환
    long alsa_vol = (m_volMax - m_volMin) * volume_pct / 100 + m_volMin;

    // ALSA API를 통해 모든 채널(좌/우)의 볼륨을 설정
    snd_mixer_selem_set_playback_volume_all(m_mixerElem, alsa_vol);

    m_currentVolume = volume_pct;
    m_currentDistance = distance;
}

void MainWindow::onDistanceTimer()
{
    if (m_serialFd < 0) return;

    int distance = readUS100Distance();
    if (distance <= 0) return;

    // 필터 없이 바로 볼륨 업데이트
    updateVolume(distance);
}


void MainWindow::runP2PCommands() {
            // 1) p2p_find 실행
            QString serverMac;
            QProcess findProc(this);
            findProc.setProgram("wpa_cli");
            findProc.setArguments(QStringList() << "-i" << "p2p-wlan0-0" << "p2p_find");
            findProc.start();
            if (!findProc.waitForFinished(5000)) {
                qWarning() << "p2p_find 명령 타임아웃 또는 실패:" << findProc.errorString();
                return;
            }
            QString findOutput = findProc.readAllStandardOutput();
            qDebug() << "[p2p_find 결과]" << findOutput.trimmed();

            // 2) p2p_connect <server mac> 0000 auth 실행
            QProcess connectProc(this);
            connectProc.setProgram("wpa_cli");
            connectProc.setArguments(QStringList()
                                     << "-i" << "p2p-wlan0-0"
                                     << "p2p_connect"
                                     << serverMac
                                     << "0000"
                                     << "auth");
            connectProc.start();
            if (!connectProc.waitForFinished(5000)) {
                qWarning() << "p2p_connect 명령 타임아웃 또는 실패:" << connectProc.errorString();
                return;
            }
            QString connectOutput = connectProc.readAllStandardOutput();
            qDebug() << "[p2p_connect 결과]" << connectOutput.trimmed();

            if(findOutput.contains("OK") && connectOutput.contains("OK")){
                m_button->setText("Disconnect");
                m_button->setStyleSheet(
                            "QPushButton {"
                                "  background-color: #4CAF50;"  // 배경색
                                "}"
                );
                connect(m_button, &QPushButton::clicked, this, &MainWindow::disconnectP2P);
            }
            else{
                m_button->setText("Reconnect");
                m_button->setStyleSheet(
                            "QPushButton {"
                                "  background-color: #F44336;"  // 배경색
                                "}"
                );
            }
        }

        void MainWindow::disconnectP2P()
        {
            // 1) QProcess 생성
            QProcess proc(this);

            // 2) 프로그램 및 인자 설정
            proc.setProgram("sudo");
            proc.setArguments(QStringList()
                              << "wpa_cli"
                              << "-i" << "p2p-wlan0-0"
                              << "p2p_disconnect");

            // 3) 명령 실행
            proc.start();
            if (!proc.waitForFinished(5000)) {
                qWarning() << "p2p_disconnect 실행 실패:" << proc.errorString();
                return;
            }

            // 4) 표준출력 결과 읽기
            const QByteArray stdoutData = proc.readAllStandardOutput();
            const QString result = QString::fromLocal8Bit(stdoutData).trimmed();

            // 5) 결과 처리
            if (result == "OK") {
                qDebug() << "P2P 그룹에서 정상적으로 연결 해제됨.";
                m_button->setText("Connect");
                m_button->setStyleSheet(
                            "QPushButton {"
                                "  background-color: #FFFFFF;"  // 배경색
                                "}"
                );

            } else {
                qDebug() << "P2P 그룹 해제 시도 결과:" << result;
            }
        }
