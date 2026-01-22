#include <QApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>
#include <QShortcut>
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QThread>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


#include <atomic>
#include <thread>

extern "C" {
#include "whisper.h"
}

#include "SpeechWorker.h"




#ifdef Q_OS_WIN
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
	explicit OverlayWindow(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
		setAttribute(Qt::WA_TranslucentBackground, true);
		setAttribute(Qt::WA_TransparentForMouseEvents, true);

		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(24, 24, 24, 24);

		m_label = new QLabel("Initializing...", this);
		m_label->setWordWrap(true);
		m_label->setStyleSheet(R"(
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
		layout->addWidget(m_label);

		const QScreen* s = QGuiApplication::primaryScreen();
		const QRect g = s ? s->availableGeometry() : QRect(0, 0, 1920, 1080);

		const int w = qMin(1000, g.width() - 80);
		const int h = 160;
		resize(w, h);

		move(g.x() + (g.width() - width()) / 2, g.y() + 40);
	}

	void setOverlayText(const QString& t) {
		m_label->setText(t);
	}

protected:
	void showEvent(QShowEvent* e) override {
		QWidget::showEvent(e);
#ifdef Q_OS_WIN
		HWND hwnd = reinterpret_cast<HWND>(winId());
		if (!hwnd) return;
		LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
		ex |= WS_EX_TRANSPARENT | WS_EX_LAYERED;
		SetWindowLong(hwnd, GWL_EXSTYLE, ex);
#endif
	}

private:
	QLabel* m_label = nullptr;
};




int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

#ifdef Q_OS_WIN
	GlobalHotkeyFilter filter;
	app.installNativeEventFilter(&filter);

	constexpr int HOTKEY_ID = 1;
	if (!RegisterHotKey(nullptr, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT, 'Q')) {
		RegisterHotKey(nullptr, HOTKEY_ID, MOD_CONTROL | MOD_ALT, 'Q');
	}
#endif

	qDebug() << "CWD =" << QDir::currentPath();
	qDebug() << "EXE =" << QCoreApplication::applicationDirPath();

	const QString modelPath =
		QDir(QCoreApplication::applicationDirPath())
		.filePath("models/ggml-base.en.bin");

	qDebug() << "Model =" << modelPath << "exists=" << QFileInfo::exists(modelPath);

	OverlayWindow w;
	w.setOverlayText("Starting... Ctrl+Shift+Q to quit");
	w.show();

	// Worker thread
	auto* thread = new QThread(&app);
	auto* worker = new SpeechWorker(modelPath);   // <-- USE IT
	worker->moveToThread(thread);

	QObject::connect(thread, &QThread::started, worker, &SpeechWorker::start);

	QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
		// Safest if stop needs to run on the worker thread:
		QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
		thread->quit();
		thread->wait();
		});



	QObject::connect(worker, &SpeechWorker::textReady, &w, [&](const QString& t) {
		w.setOverlayText(t);
		}, Qt::QueuedConnection);

	QObject::connect(worker, &SpeechWorker::finished, thread, &QThread::quit);
	QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
	QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);

	thread->start();

	const int rc = app.exec();

#ifdef Q_OS_WIN
	UnregisterHotKey(nullptr, HOTKEY_ID);
#endif
	return rc;
}

