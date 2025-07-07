#include "mainwindow.h"
#include <QPainter>
#include <QtMath>
#include <complex>
#include <QProcess>
#include <QMessageBox>
#include <QResizeEvent>
#include <QDebug> // 디버깅용 로그 출력을 위해 추가
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
      m_fftSize(1024)
{
    setMinimumSize(600, 300);
    m_levels.resize( m_fftSize/2 );
    if (!openWav("/mnt/nfs/test_contents/test.wav")) {
        qFatal("WAV open failed");
    }
    m_button = new QPushButton("Sync", this);
    connect(m_button, &QPushButton::clicked, this, &MainWindow::runP2PCommands);

    // 1) aplay 프로세스 준비 (stdin으로 PCM 받아 재생)
    m_playProc = new QProcess(this);

    QString amixerProg = "./amixer";
    QStringList amixerArgs;
    amixerArgs << "-c" << "0"
               << "cset" << "numid=1" << "80%";
    // 동기 실행(결과 코드가 필요 없으면 execute, 필요하면 반환값 체크)
    QProcess::execute(amixerProg, amixerArgs);

    // WAV 파일 재생
    QString aplayProg = "./aplay";
    QStringList aplayArgs;
    aplayArgs << "-Dhw:0,0"
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
}

MainWindow::~MainWindow()
{
    m_timer->stop();
    if (m_playProc) {
        m_playProc->closeWriteChannel();
        m_playProc->terminate();
        m_playProc->waitForFinished();
    }
    m_file.close();
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
    int cellW = w / 4;


    m_button->setGeometry(cellW * 3, 0, cellW, topH);

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
    const int bytesPerSample = m_bitsPerSample/8;
    const int chunkBytes    = m_samplesPerFrame * bytesPerSample * m_channels;
    QByteArray buf = m_file.read(chunkBytes);

    if (buf.size() < chunkBytes) {
        // 파일 끝: 더 이상 처리하지 않고 종료
        m_timer->stop();
        if (m_playProc) {
            m_playProc->closeWriteChannel();
            m_playProc->waitForFinished();
        }
        m_file.close();
        return;
    }

    // 1) aplay 프로세스에 똑같은 버퍼 쓰기 → 정확히 이 타이밍의 오디오 출력
    m_playProc->write(buf);

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

    // (3) 충분히 쌓였으면 FFT 수행
    if (m_fftBuffer.size() == quint32(m_fftSize)) {
        auto spectrum = fft(m_fftBuffer);
        int half = spectrum.size() / 2;
        for (int i = 0; i < half; ++i) {
            m_levels[i] = std::abs(spectrum[i]) / half;
        }
        update();  // paintEvent 트리거
    }
}

void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    int w = width(), h = height();
    int topH = h / 4;
    int botY = topH;
    int botH = h - topH;
    int cellW = w / 4;

    // ─────── 경계선 그리기 ───────
    p.setPen(Qt::white);
    // 상단/하단 경계 수평선
    p.drawLine(0, topH, w, topH);
    // 상단 영역 세로 분할선 (B/C, C/D, D/E)
    for (int i = 1; i < 4; ++i) {
        p.drawLine(cellW * i, 0, cellW * i, topH);
    }
    // ────────────────────────────

    QFont font = p.font();
    font.setPointSize(14);
    p.setFont(font);

    struct { int x; const char* txt; } labels[] = {
        { 0*cellW, "volume[implementing]" },
        { 1*cellW, "distance: ???m" },
        { 2*cellW, "music name: test_wav" }
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
