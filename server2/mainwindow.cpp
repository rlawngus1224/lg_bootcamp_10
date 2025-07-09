// mainwindow.cpp
#include "mainwindow.h"
#include <QFileInfoList>
#include <QApplication>
#include <QProcess>
#include <QFileInfo>
#include <QDataStream>
#include <QDebug>
#include <unistd.h>
#include <QRegularExpression>

static quint32 readUInt32(QDataStream &in) {
    quint32 val;
    in >> val;
    return val;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      listWidget(new QListWidget(this)),
      stacked(new QStackedWidget(this)),
      titleLabel(new QLabel(this)),
      progressSlider(new ClickableSlider(Qt::Horizontal, this)),
      timeLabel(new QLabel("00:00 / 00:00", this)),
      backButton(new QPushButton("Back", this)),
      timer(new QTimer(this)),
      procSnapLow(nullptr), procSnapHigh(nullptr), procSnapOrigin(nullptr),
      procFFmpeg(nullptr),
      currentPosition(0), totalDuration(0)
{
    qDebug() << "[MainWindow] Constructor start";
    // List page setup
    listPage = new QWidget(this);
    QVBoxLayout *listLayout = new QVBoxLayout(listPage);
    QLabel *waveLabel = new QLabel("WAV Files", this);
    QFont waveFont = waveLabel->font();
    waveFont.setPointSize(waveFont.pointSize() + 8);
    waveLabel->setFont(waveFont);
    listWidget->setFont(waveFont);
    listWidget->setStyleSheet("font-size: 20pt;");
    listLayout->addWidget(waveLabel);
    listLayout->addWidget(listWidget);
    loadWavList();
    connect(listWidget, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onFileDoubleClicked);

    // Play page setup
    playPage = new QWidget(this);
    QVBoxLayout *playLayout = new QVBoxLayout(playPage);
    playLayout->addWidget(titleLabel);
    playLayout->addWidget(progressSlider);
    playLayout->addWidget(timeLabel);
    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(backButton);
    btnLayout->addStretch();
    playLayout->addLayout(btnLayout);
    progressSlider->setRange(0, 100);
    connect(timer, &QTimer::timeout, this, &MainWindow::updateProgress);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::onBackClicked);
    connect(progressSlider, &QSlider::sliderMoved,
            this, &MainWindow::onSliderMoved);

    // Stacked widget
    stacked->addWidget(listPage);
    stacked->addWidget(playPage);

    // — A구역을 감쌀 프레임 —
    QFrame *frameA = new QFrame;
    frameA->setFrameShape(QFrame::Box);
    QHBoxLayout *aLayout = new QHBoxLayout(frameA);
    aLayout->addWidget(stacked);

    // — B/C/D 구역 상태 라벨(클래스 멤버) ---
    lblClient1 = new QLabel("Client1 Disconnected", this);
    lblClient2 = new QLabel("Client2 Disconnected", this);
    lblClient3 = new QLabel("Client3 Disconnected", this);

    QFrame *frameB = new QFrame; frameB->setFrameShape(QFrame::Box);
    QVBoxLayout *layB = new QVBoxLayout(frameB);
    layB->addWidget(lblClient1, 0, Qt::AlignCenter);

    QFrame *frameC = new QFrame; frameC->setFrameShape(QFrame::Box);
    QVBoxLayout *layC = new QVBoxLayout(frameC);
    layC->addWidget(lblClient2, 0, Qt::AlignCenter);

    QFrame *frameD = new QFrame; frameD->setFrameShape(QFrame::Box);
    QVBoxLayout *layD = new QVBoxLayout(frameD);
    layD->addWidget(lblClient3, 0, Qt::AlignCenter);

    QWidget *rightWidget = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setSpacing(0);
    rightLayout->setMargin(0);
    rightLayout->addWidget(frameB, 1);
    rightLayout->addWidget(frameC, 1);
    rightLayout->addWidget(frameD, 1);

    // — 전체를 가로로 2:1 비율로 배치 —
    QWidget *central = new QWidget;
    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    mainLayout->setSpacing(0);
    mainLayout->setMargin(0);
    mainLayout->addWidget(frameA, 2);       // A구역: 2
    mainLayout->addWidget(rightWidget, 1);  // B/C/D 구역 합계: 1

    setCentralWidget(central);
    setWindowTitle("WAV Streamer");

    // 서버 시작
    startSnapServers();
    qDebug() << "[MainWindow] Constructor end";
}

MainWindow::~MainWindow() {
    qDebug() << "[MainWindow] Destructor start";
    // 종료 시 프로세스 정리
    stopFFmpeg();
    for (auto p : {procSnapLow, procSnapHigh, procSnapOrigin}) {
        if (p && p->state() == QProcess::Running) {
            p->kill();
            p->waitForFinished();
        }
    }
}

void MainWindow::loadWavList() {
    QString path = "/root";
    QDir dir(path);
    QStringList filters {"*.wav"};
    auto files = dir.entryInfoList(filters, QDir::Files);
    for (auto &fi : files) {
        listWidget->addItem(fi.absoluteFilePath());
    }
}

int MainWindow::getWavDuration(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    // 1) RIFF 헤더 스킵
    in.skipRawData(12);

    // 2) fmt 청크 찾기
    char   chunkId[4];
    quint32 chunkSize;
    quint16 blockAlign = 0;
    quint32 sampleRate = 0;

    while (!file.atEnd()) {
        in.readRawData(chunkId, 4);
        chunkSize = readUInt32(in);
        QString id = QString::fromLatin1(chunkId,4);
        if (id == "fmt ") {
            // fmt 청크 본문 읽기 (16바이트)
            quint16 audioFormat, numChannels, bitsPerSample;
            quint32 byteRate;
            in >> audioFormat    // 2
               >> numChannels    // 2
               >> sampleRate     // 4
               >> byteRate       // 4
               >> blockAlign     // 2
               >> bitsPerSample; // 2  ← 이 부분이 빠져 있었음

            // fmt 청크에 확장 데이터(16바이트 초과)가 있으면 스킵
            if (chunkSize > 16)
                in.skipRawData(int(chunkSize - 16));
            break;
        } else {
            // 다른 청크면 본문 크기만큼 스킵
            in.skipRawData(int(chunkSize));
        }
    }

    // 3) data 청크 찾기
    quint32 dataSize = 0;
    while (!file.atEnd()) {
        in.readRawData(chunkId, 4);
        chunkSize = readUInt32(in);
        if (QString::fromLatin1(chunkId,4) == "data") {
            dataSize = chunkSize;
            break;
        }
        in.skipRawData(int(chunkSize));
    }
    file.close();

    if (sampleRate == 0 || blockAlign == 0 || dataSize == 0) {
        qDebug() << "[getWavDuration] 읽기 실패:"
                 << "sampleRate=" << sampleRate
                 << "blockAlign="  << blockAlign
                 << "dataSize="    << dataSize;
        return 0;
    }

    // 4) 재생 시간 계산
    double sampleCount = double(dataSize) / blockAlign;
    double durationSec = sampleCount / sampleRate;
    return int(durationSec + 0.5);
}


void MainWindow::startSnapServers() {
    qDebug() << "[MainWindow] Starting Snapcast servers...";
    ::system("rm -f /tmp/snap_low /tmp/snap_high /tmp/snap_origin");
    ::system("mkfifo /tmp/snap_low /tmp/snap_high /tmp/snap_origin");
    ::system("chmod 666 /tmp/snap_low /tmp/snap_high /tmp/snap_origin");

    // 2) snapserver 프로세스 실행
    auto startProc = [&](QProcess *&proc, const QString &pipe, int httpPort, int tcpPort, int streamPort, 
                         void (MainWindow::*slot)(), QLabel *lbl){
        proc = new QProcess(this);
        QString prog = "/root/snapserver";
        QStringList args = {
            "--http.bind_to_address=192.168.4.1", QString("--http.port=%1").arg(httpPort),
            "--tcp.bind_to_address=192.168.4.1", QString("--tcp.port=%1").arg(tcpPort),
            "--stream.bind_to_address=192.168.4.1", QString("--stream.port=%1").arg(streamPort),
            QString("--stream.source=pipe://%1?name=%2&format=wav").arg(pipe, pipe.split('_').last()),
            "--logging.filter=*:info"
        };
        connect(proc, &QProcess::readyReadStandardOutput, this, slot);
        connect(proc, &QProcess::readyReadStandardError, this, [this, proc, pipe]() {
            auto errOut = proc->readAllStandardError();
            if (!errOut.isEmpty()) qDebug() << "[SnapServer:" << pipe << "stderr]" << errOut;
        });
        connect(proc, &QProcess::stateChanged, this, [pipe](QProcess::ProcessState state) {
            qDebug() << "[SnapServer:" << pipe << "] State changed to" << state;
        });
        connect(proc, &QProcess::errorOccurred, this, [pipe](QProcess::ProcessError err) {
            qDebug() << "[SnapServer:" << pipe << "] Error occurred:" << err;
        });

        proc->start(prog, args);
        if (!proc->waitForStarted(3000)) {
            qDebug() << "[SnapServer] Failed to start" << prog << args;
        } else {
            qDebug() << "[SnapServer] Started" << prog << "with args" << args;
        }
        lbl->setText(lbl->text());
    };

    startProc(procSnapLow, "/tmp/snap_low", 1780, 1707, 1704,
              &MainWindow::onSnapOutputLow, lblClient1);
    startProc(procSnapHigh, "/tmp/snap_high", 1781, 1708, 1705,
              &MainWindow::onSnapOutputHigh, lblClient2);
    startProc(procSnapOrigin, "/tmp/snap_origin", 1782, 1709, 1706,
              &MainWindow::onSnapOutputOrigin, lblClient3);
}

void MainWindow::onSnapOutputLow() {
    if (!procSnapLow) return;

    // 1) 프로세스 stdout 읽기
    QByteArray data = procSnapLow->readAllStandardOutput();
    QString out = QString::fromUtf8(data);
    qDebug() << "[SnapLow stdout]" << out;
    // 2) MAC 주소 파싱: "Hello from b8:27:eb:9d:e5:c8,"
    static const QRegularExpression macRx(R"(Hello from\s+([0-9A-Fa-f:]+),)");
    QRegularExpressionMatch match = macRx.match(out);
    if (match.hasMatch()) {
        QString mac = match.captured(1);
        qDebug() << "[SnapLow] Detected MAC:" << mac;
        // 지정된 MAC과 일치할 때만 상태 변경
        if (mac.compare("b8:27:eb:9d:e5:c8", Qt::CaseInsensitive) == 0) {
            lblClient1->setText("Client1 Connected (Low)");
        }
    }
}

void MainWindow::onSnapOutputHigh() {
    if (!procSnapLow) return;

    // 1) 프로세스 stdout 읽기
    QByteArray data = procSnapLow->readAllStandardOutput();
    QString out = QString::fromUtf8(data);
    qDebug() << "[SnapHigh stdout]" << out;
    // 2) MAC 주소 파싱: "Hello from B8:27:EB:FF:7D:4A,"
    static const QRegularExpression macRx(R"(Hello from\s+([0-9A-Fa-f:]+),)");
    QRegularExpressionMatch match = macRx.match(out);
    if (match.hasMatch()) {
        QString mac = match.captured(1);
        qDebug() << "[SnapHigh] Detected MAC:" << mac;
        // 지정된 MAC과 일치할 때만 상태 변경
        if (mac.compare("b8:27:eb:ff:7d:4a", Qt::CaseInsensitive) == 0) {
            lblClient2->setText("Client2 Connected (High)");
        }
    }
}

void MainWindow::onSnapOutputOrigin() {
    if (!procSnapLow) return;

    // 1) 프로세스 stdout 읽기
    QByteArray data = procSnapLow->readAllStandardOutput();
    QString out = QString::fromUtf8(data);
    qDebug() << "[SnapOrigin stdout]" << out;

    // 2) MAC 주소 파싱: "Hello from B8:27:EB:CC:FE:F3,"
    static const QRegularExpression macRx(R"(Hello from\s+([0-9A-Fa-f:]+),)");
    QRegularExpressionMatch match = macRx.match(out);
    if (match.hasMatch()) {
        QString mac = match.captured(1);
        qDebug() << "[SnapLow] Detected MAC:" << mac;
        // 지정된 MAC과 일치할 때만 상태 변경
        if (mac.compare("b8:27:eb:cc:fe:f3", Qt::CaseInsensitive) == 0) {
            lblClient3->setText("Client3 Connected (Origin)");
        }
    }
}

void MainWindow::onFileDoubleClicked(QListWidgetItem *item) {
    QString filePath = item->text();
    qDebug() << "[MainWindow] File selected for playback:" << filePath;
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 10);        // 원래보다 6pt 크게
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);

    // 재생 정보
    QFont timeFont = timeLabel->font();
    timeFont.setPointSize(timeFont.pointSize() + 8);         // 원래보다 4pt 크게
    timeLabel->setFont(timeFont);
    timeLabel->setAlignment(Qt::AlignCenter);
    totalDuration = getWavDuration(filePath);
    currentPosition = 0;
    titleLabel->setText(QFileInfo(filePath).fileName());
    progressSlider->setValue(0);
    int m = totalDuration / 60;
    int s = totalDuration % 60;
    timeLabel->setText(QString("00:00 / %1:%2")
                       .arg(m,2,10,QChar('0'))
                       .arg(s,2,10,QChar('0')));
    stacked->setCurrentWidget(playPage);
    if (totalDuration > 0) {
        timer->start(1000);
    } else {
        qDebug() << "[MainWindow] Warning: WAV duration is zero, progress bar disabled.";
    }

    // ffmpeg 실행
    startFFmpeg(filePath);
}

void MainWindow::startFFmpeg(const QString &filePath) {
    stopFFmpeg(); // 이미 실행 중이면 종료
    procFFmpeg = new QProcess(this);
    QString prog = "ffmpeg";
    QStringList args = {
        "-re", "-i", filePath,
        "-filter_complex",
        "[0:a]asplit=3[origin][low][high];"
        "[low]lowpass=f=250[lowout];"
        "[high]highpass=f=250[highout]",
        "-map", "[origin]", "-ac", "2", "-ar", "48000", "-f", "wav", "/tmp/snap_origin",
        "-map", "[lowout]",  "-ac", "2", "-ar", "48000", "-f", "wav", "/tmp/snap_low",
        "-map", "[highout]", "-ac", "2", "-ar", "48000", "-f", "wav", "/tmp/snap_high",
        "-y"
    };
    qDebug() << "[MainWindow] Starting FFmpeg with args:" << args;
    connect(procFFmpeg, &QProcess::readyReadStandardError, this, [this]() {
        auto err = procFFmpeg->readAllStandardError();
        if (!err.isEmpty()) qDebug() << "[FFmpeg stderr]" << err;
    });
    connect(procFFmpeg, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        qDebug() << "[FFmpeg] Finished with code" << code << "status" << status;
    });
    procFFmpeg->start(prog, args);
}

void MainWindow::stopFFmpeg() {
    if (procFFmpeg && procFFmpeg->state() == QProcess::Running) {
        procFFmpeg->kill();
        procFFmpeg->waitForFinished();
    }
}

void MainWindow::updateProgress() {
    if (currentPosition < totalDuration) {
        ++currentPosition;
        int pct = (currentPosition * 100) / totalDuration;
        progressSlider->setValue(pct);
        int cm = currentPosition / 60, cs = currentPosition % 60;
        int tm = totalDuration / 60, ts = totalDuration % 60;
        timeLabel->setText(
            QString("%1:%2 / %3:%4")
            .arg(cm,2,10,QChar('0')).arg(cs,2,10,QChar('0'))
            .arg(tm,2,10,QChar('0')).arg(ts,2,10,QChar('0')));
    } else {
        timer->stop();
    }
}

void MainWindow::onBackClicked() {
    timer->stop();
    stopFFmpeg();
    stacked->setCurrentWidget(listPage);
}

// **슬라이더 이동 시 currentPosition 갱신 및 화면 업데이트**
void MainWindow::onSliderMoved(int percent) {
    if (totalDuration <= 0) return;
    // percent(0–100) → 초 단위 위치
    currentPosition = percent * totalDuration / 100;
    int cm = currentPosition / 60, cs = currentPosition % 60;
    int tm = totalDuration / 60, ts = totalDuration % 60;
    timeLabel->setText(
        QString("%1:%2 / %3:%4")
        .arg(cm,2,10,QChar('0')).arg(cs,2,10,QChar('0'))
        .arg(tm,2,10,QChar('0')).arg(ts,2,10,QChar('0')));
}
