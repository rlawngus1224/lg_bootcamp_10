#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QDataStream>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QProcess>
#include "clickableslider.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onFileDoubleClicked(QListWidgetItem *item);
    void updateProgress();
    void onBackClicked();
    void onSliderMoved(int percent);
    void onSnapOutputLow();
    void onSnapOutputHigh();
    void onSnapOutputOrigin();

private:
    ClickableSlider *progressSlider;
    void loadWavList();
    int getWavDuration(const QString &filePath);
    void startSnapServers();
    void startFFmpeg(const QString &filePath);
    void stopFFmpeg();

    QListWidget *listWidget;
    QStackedWidget *stacked;
    QWidget *listPage;
    QWidget *playPage;

    // playback UI
    QLabel *titleLabel;
    QLabel *timeLabel;
    QPushButton *backButton;
    QTimer *timer;
    int currentPosition;
    int totalDuration; // seconds

    // 서버 프로세스
    QProcess *procSnapLow;
    QProcess *procSnapHigh;
    QProcess *procSnapOrigin;

    // ffmpeg 프로세스
    QProcess *procFFmpeg;

    // 클라이언트 상태 라벨
    QLabel *lblClient1;
    QLabel *lblClient2;
    QLabel *lblClient3;
};

#endif // MAINWINDOW_H
