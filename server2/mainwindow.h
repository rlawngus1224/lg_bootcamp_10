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

private:
    ClickableSlider *progressSlider;
    void loadWavList();
    int getWavDuration(const QString &filePath);

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
};

#endif // MAINWINDOW_H
