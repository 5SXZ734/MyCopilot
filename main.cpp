#include <QApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>
#include <QShortcut>
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>


#ifdef Q_OS_WIN
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>

class GlobalHotkeyFilter : public QAbstractNativeEventFilter
{
public:
    bool nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* /*result*/) override
    {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY) {
            QCoreApplication::quit();
            return true;
        }
        return false;
    }
};

#endif

class OverlayWindow : public QWidget
{
public:
    explicit OverlayWindow(const QString& text, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setWindowFlags(Qt::Tool |
                       Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint);

        // Transparency + click-through from Qt side
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 24, 24, 24);

        auto* label = new QLabel(text, this);
        label->setWordWrap(true);
        label->setStyleSheet(R"(
            QLabel {
                color: white;
                font-size: 28px;
                font-weight: 600;
                padding: 16px 20px;
                background: rgba(0, 0, 0, 160);
                border: 1px solid rgba(255, 255, 255, 60);
                border-radius: 14px;
            }
        )");
        layout->addWidget(label);

        // Position: top-center of primary screen
        const QScreen* s = QGuiApplication::primaryScreen();
        const QRect g = s ? s->availableGeometry() : QRect(0, 0, 1920, 1080);

        const int w = qMin(900, g.width() - 80);
        const int h = 140;
        resize(w, h);

        const int x = g.x() + (g.width() - width()) / 2;
        const int y = g.y() + 40;
        move(x, y);
    }

protected:
    void showEvent(QShowEvent* e) override
    {
        QWidget::showEvent(e);
#ifdef Q_OS_WIN
        // Make sure native handle exists
        HWND hwnd = reinterpret_cast<HWND>(winId());
        if (!hwnd) return;

        LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
        // WS_EX_TRANSPARENT: mouse events fall through to windows below
        // WS_EX_LAYERED: required for transparency and to behave correctly
        ex |= WS_EX_TRANSPARENT | WS_EX_LAYERED;
        SetWindowLong(hwnd, GWL_EXSTYLE, ex);

        // Optional: keep it out of Alt-Tab
        // (Qt::Tool usually already does)
#endif
    }
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    OverlayWindow w("Click-through overlay.\nYou should be able to click things UNDER this window.");
    w.show();

#ifdef Q_OS_WIN
    GlobalHotkeyFilter filter;
    app.installNativeEventFilter(&filter);

    // Ctrl+Shift+Q
    constexpr int HOTKEY_ID = 1;
    if (!RegisterHotKey(nullptr, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT, 'Q')) {
        // Optional: fallback if already taken by something else
        // e.g. try Ctrl+Alt+Q
        RegisterHotKey(nullptr, HOTKEY_ID, MOD_CONTROL | MOD_ALT, 'Q');
    }

    const int rc = app.exec();

    UnregisterHotKey(nullptr, HOTKEY_ID);
    return rc;
#else
    return app.exec();
#endif
}
