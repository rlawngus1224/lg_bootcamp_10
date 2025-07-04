#include <QApplication>
#include <QMainWindow>
#include <QFileDialog>
#include <QFile>
#include <QDataStream>
#include <QVector>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include "qcustomplot.h"

class SoundWaveformViewer : public QMainWindow {
    Q_OBJECT
public:
    SoundWaveformViewer(QWidget *parent=nullptr)
      : QMainWindow(parent), plot(new QCustomPlot(this)), timer(new QTimer(this))
    {
        // UI 셋업
        auto *btn = new QPushButton("Load WAV & Start", this);
        connect(btn, &QPushButton::clicked, this, &SoundWaveformViewer::onLoad);

        plot->addGraph();
        plot->xAxis->setLabel("Sample Index");
        plot->yAxis->setLabel("Amplitude");
        plot->yAxis->setRange(-1,1);

        auto *lay = new QVBoxLayout;
        lay->addWidget(btn);
        lay->addWidget(plot);
        auto *ctr = new QWidget(this);
        ctr->setLayout(lay);
        setCentralWidget(ctr);

        // 타이머 슬롯: 매 intervalMs 마다 nextChunk() 호출
        connect(timer, &QTimer::timeout, this, &SoundWaveformViewer::nextChunk);
    }

private slots:
    void onLoad() {
        // 파일 선택
        QString path = QFileDialog::getOpenFileName(this,"Open WAV","","WAV Files (*.wav)");
        if (path.isEmpty()) return;

        // 기존 열려 있던 파일/데이터 초기화
        if (file.isOpen()) file.close();
        xdata.clear(); ydata.clear(); sampleIndex = 0;
        plot->graph(0)->setData(xdata, ydata);
        plot->replot();

        // 파일 열기
        file.setFileName(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this,"Error","Cannot open file");
            return;
        }
        QDataStream in(&file);
        in.setByteOrder(QDataStream::LittleEndian);

        // WAV 헤더 스킵 (RIFF/WAVE + fmt + data 헤더만)
        char hdr[4];
        quint32 chunkSize;
        // RIFF
        in.readRawData(hdr,4); in >> chunkSize; in.readRawData(hdr,4);
        // 청크 순회: fmt/data 둘 다 찾으면 탈출
        bool fmtFound=false, dataFound=false;
        quint16 numChannels=0, bitsPerSample=0;
        while(!in.atEnd() && !(fmtFound&&dataFound)) {
            in.readRawData(hdr,4);
            in >> chunkSize;
            if (strncmp(hdr,"fmt ",4)==0) {
                quint16 audioFormat; quint32 sampleRate, byteRate; quint16 blockAlign;
                in>>audioFormat>>numChannels>>sampleRate>>byteRate>>blockAlign>>bitsPerSample;
                if (chunkSize>16) file.seek(file.pos() + (chunkSize-16));
                fmtFound=true;
            }
            else if (strncmp(hdr,"data",4)==0) {
                dataSize = chunkSize;
                dataPos  = file.pos();
                file.seek(dataPos + chunkSize);
                dataFound=true;
            }
            else {
                file.seek(file.pos() + chunkSize);
            }
        }
        if (!fmtFound || !dataFound || bitsPerSample!=16) {
            QMessageBox::warning(this,"Error","Unsupported WAV format (need 16-bit PCM)");
            file.close();
            return;
        }

        // 재위치: 데이터 시작
        file.seek(dataPos);

        // 청크 크기 및 샘플 총 개수 산정
        bytesPerSample = sizeof(qint16) * numChannels;
        totalSamples    = dataSize / bytesPerSample;

        // 타이머 시작 (예: 30fps -> 33ms 마다 호출)
        intervalMs = 33;
        samplesPerInterval = qMax<qint64>(1, totalSamples / (5000 * (1000/intervalMs)));
        timer->start(intervalMs);
    }

    void nextChunk() {
        if (!file.isOpen()) {
            timer->stop();
            return;
        }
        QDataStream in(&file);
        in.setByteOrder(QDataStream::LittleEndian);

        QByteArray chunk = file.read(bytesPerSample * samplesPerInterval);
        if (chunk.isEmpty()) {
            timer->stop();
            file.close();
            return;
        }
        const qint16 *sp = reinterpret_cast<const qint16*>(chunk.constData());
        int nSamples = chunk.size() / sizeof(qint16);
        // 읽은 샘플 중 배율 처리
        for (int i = 0; i < nSamples; ++i) {
            double val = double(sp[i]) / 32768.0;
            xdata.append(sampleIndex++);
            ydata.append(val);
        }
        // 최대 5000점만 유지 (스크롤 효과)
        if (xdata.size() > 5000) {
            int removeCnt = xdata.size() - 5000;
            xdata.remove(0, removeCnt);
            ydata.remove(0, removeCnt);
        }
        // 플롯 갱신
        plot->graph(0)->setData(xdata, ydata);
        plot->xAxis->setRange(sampleIndex - 5000, sampleIndex);
        plot->replot();
    }

private:
    QCustomPlot *plot;
    QTimer      *timer;
    QFile        file;
    qint64       dataSize=0, dataPos=0;
    qint64       totalSamples=0, bytesPerSample=0;
    qint64       samplesPerInterval=1, intervalMs=33;
    QVector<double> xdata, ydata;
    qint64       sampleIndex=0;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    SoundWaveformViewer w;
    w.resize(820,520);
    w.show();
    return a.exec();
}
