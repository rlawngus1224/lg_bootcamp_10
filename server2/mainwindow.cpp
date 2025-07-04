// mainwindow.cpp
#include "mainwindow.h"
#include <QFileInfoList>
#include <QApplication>
#include <QFile>

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
      currentPosition(0),
      totalDuration(0)
{
    // List page setup
    listPage = new QWidget(this);
    QVBoxLayout *listLayout = new QVBoxLayout(listPage);
    listLayout->addWidget(new QLabel("WAV Files", this));
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

    // — B/C/D 구역을 세로로 쌓을 컨테이너 —
    QFrame *frameB = new QFrame; frameB->setFrameShape(QFrame::Box);
    QLabel *lblB = new QLabel("Client1 Connected/Disconnected", frameB);
    QVBoxLayout *layB = new QVBoxLayout(frameB);
    layB->addWidget(lblB, 0, Qt::AlignCenter);

    QFrame *frameC = new QFrame; frameC->setFrameShape(QFrame::Box);
    QLabel *lblC = new QLabel("Client2 Connected/Disconnected", frameC);
    QVBoxLayout *layC = new QVBoxLayout(frameC);
    layC->addWidget(lblC, 0, Qt::AlignCenter);

    QFrame *frameD = new QFrame; frameD->setFrameShape(QFrame::Box);
    QLabel *lblD = new QLabel("Client3 Connected/Disconnected", frameD);
    QVBoxLayout *layD = new QVBoxLayout(frameD);
    layD->addWidget(lblD, 0, Qt::AlignCenter);

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
}

MainWindow::~MainWindow() {}

void MainWindow::loadWavList() {
    // QDir::homePath() returns the user's home directory
    QString path = "/mnt/nfs";
    QDir dir(path);
    QStringList filters {"*.wav"};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    for (auto &fi : files) {
        listWidget->addItem(fi.absoluteFilePath());
    }
}

int MainWindow::getWavDuration(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    // 1) RIFF 헤더 지나치기
    in.skipRawData(12);  // "RIFF"+size+"WAVE"

    // 2) fmt 청크 찾기
    char chunkId[4];
    quint32 chunkSize;
    quint16 audioFormat, numChannels, blockAlign, bitsPerSample;
    quint32 sampleRate, byteRate;
    while (!file.atEnd()) {
        in.readRawData(chunkId, 4);
        chunkSize = readUInt32(in);
        QString id = QString::fromLatin1(chunkId, 4);
        if (id == "fmt ") {
            in >> audioFormat    // 포맷 코드
               >> numChannels   // 채널 수
               >> sampleRate    // 샘플레이트
               >> byteRate ;
            // 다음 두 값 읽기 (blockAlign, bitsPerSample)
            in >> blockAlign
               >> bitsPerSample;
            // fmt 청크의 남은 바이트(예: fmtSize>16) 스킵
            int remain = int(chunkSize) - 16;
            if (remain > 0)
                in.skipRawData(remain);
            break;
        } else {
            // 다른 청크면 건너뛰기
            in.skipRawData(chunkSize);
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
        in.skipRawData(chunkSize);
    }
    file.close();

    if (sampleRate == 0 || blockAlign == 0)
        return 0;

    // 4) 재생 시간 계산
    double sampleCount = double(dataSize) / blockAlign;
    double durationSec = sampleCount / sampleRate;
    return int(durationSec + 0.5);  // 반올림 후 정수 반환
}

void MainWindow::onFileDoubleClicked(QListWidgetItem *item) {
    QString filePath = item->text();
    totalDuration = getWavDuration(filePath);
    currentPosition = 0;
    titleLabel->setText(QFileInfo(filePath).fileName());
    progressSlider->setValue(0);
    int m = totalDuration / 60;
    int s = totalDuration % 60;
    timeLabel->setText(QString("00:00 / %1:%2")
                       .arg(m, 2, 10, QChar('0'))
                       .arg(s, 2, 10, QChar('0')));
    stacked->setCurrentWidget(playPage);
    if (totalDuration > 0)
        timer->start(1000);
}

void MainWindow::updateProgress() {
    if (currentPosition < totalDuration) {
        ++currentPosition;
        int pct = (currentPosition * 100) / totalDuration;
        progressSlider->setValue(pct);
        int cm = currentPosition / 60;
        int cs = currentPosition % 60;
        int tm = totalDuration / 60;
        int ts = totalDuration % 60;
        timeLabel->setText(
            QString("%1:%2 / %3:%4")
            .arg(cm, 2, 10, QChar('0'))
            .arg(cs, 2, 10, QChar('0'))
            .arg(tm, 2, 10, QChar('0'))
            .arg(ts, 2, 10, QChar('0')));
    } else {
        timer->stop();
    }
}

void MainWindow::onBackClicked() {
    timer->stop();
    stacked->setCurrentWidget(listPage);
}

// **슬라이더 이동 시 currentPosition 갱신 및 화면 업데이트**
void MainWindow::onSliderMoved(int percent) {
    if (totalDuration <= 0) return;
    // percent(0–100) → 초 단위 위치
    currentPosition = percent * totalDuration / 100;
    // 시간 문자열 갱신
    int cm = currentPosition / 60;
    int cs = currentPosition % 60;
    int tm = totalDuration / 60;
    int ts = totalDuration % 60;
    timeLabel->setText(
        QString("%1:%2 / %3:%4")
        .arg(cm, 2, 10, QChar('0'))
        .arg(cs, 2, 10, QChar('0'))
        .arg(tm, 2, 10, QChar('0'))
        .arg(ts, 2, 10, QChar('0')));
}
