#include <QApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

class OverlayWindow : public QWidget
{
public:
    explicit OverlayWindow(const QString& text, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        // Frameless + always-on-top + tool window (doesn't appear in taskbar typically)
        setWindowFlags(Qt::Tool |
                       Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint);

        // Enable per-pixel transparency
        setAttribute(Qt::WA_TranslucentBackground, true);

        // If you want mouse clicks to pass through the overlay, uncomment:
        // setAttribute(Qt::WA_TransparentForMouseEvents, true);

        // Layout + label
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 24, 24, 24);

        auto* label = new QLabel(text, this);
        label->setWordWrap(true);

        // Style: readable overlay text + translucent dark background
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

        // Size & position: top-center of primary screen
        const QScreen* s = QGuiApplication::primaryScreen();
        const QRect g = s ? s->availableGeometry() : QRect(0, 0, 1920, 1080);

        const int w = qMin(900, g.width() - 80);
        const int h = 140;
        resize(w, h);

        const int x = g.x() + (g.width() - width()) / 2;
        const int y = g.y() + 40;
        move(x, y);
    }
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    OverlayWindow w("Overlay text: Hello from Qt6.\nEdit this string and rebuild.");
    w.show();

    return app.exec();
}
