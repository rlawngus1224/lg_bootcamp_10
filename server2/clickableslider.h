#ifndef CLICKABLESLIDER_H
#define CLICKABLESLIDER_H

#include <QSlider>
#include <QMouseEvent>

class ClickableSlider : public QSlider {
    Q_OBJECT
public:
    explicit ClickableSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent) {}

protected:
    void mousePressEvent(QMouseEvent *ev) override {
        if (orientation() == Qt::Horizontal) {
            // 클릭 위치 기준으로 값 계산
            int x = ev->x();
            int w = width();
            double ratio = static_cast<double>(x) / w;
            int newVal = minimum() + static_cast<int>(ratio * (maximum() - minimum()));
            setValue(newVal);
            emit sliderMoved(newVal);         // 드래그 시그널처럼
            emit sliderPressed();             // 옵션
        }
        QSlider::mousePressEvent(ev);
    }
};

#endif // CLICKABLESLIDER_H
