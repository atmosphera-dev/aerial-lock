#include <QGuiApplication>
#include <QQuickWindow>
#include <QKeyEvent>
#include <QTimer>
#include <cstdio>

class DesktopProbe final : public QQuickWindow {
public:
    bool witness = false;
protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (witness && event->key() == Qt::Key_F11) {
            std::puts("DESKTOP_LOCK_INPUT_LEAK");
            std::fflush(stdout);
            QCoreApplication::exit(2);
        }
        if (event->key() == Qt::Key_F12) {
            std::puts("DESKTOP_INPUT_RECEIVED");
            std::fflush(stdout);
            QCoreApplication::exit(0);
        }
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    DesktopProbe window;
    window.witness = app.arguments().contains("--witness");
    window.setTitle("aerial remediation desktop input probe");
    window.setColor(window.witness ? QColor(3, 113, 47) : QColor(Qt::darkGreen));
    window.resize(320, 240);
    window.show();
    QTimer readiness;
    QObject::connect(&readiness, &QTimer::timeout, [&window, &readiness] {
        if (!window.isActive() || !window.isExposed()) return;
        readiness.stop();
        std::puts("DESKTOP_INPUT_READY");
        std::fflush(stdout);
    });
    readiness.start(10);
    QTimer::singleShot(window.witness ? 120000 : 5000, [] { QCoreApplication::exit(1); });
    return app.exec();
}
