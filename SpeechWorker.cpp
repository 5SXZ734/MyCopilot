#include "SpeechWorker.h"
#include <QTimer>

extern "C" {
#include "whisper.h"
}

SpeechWorker::SpeechWorker(QString modelPath, QObject* parent)
    : QObject(parent), m_modelPath(std::move(modelPath)) {}

SpeechWorker::~SpeechWorker() {
    stop(); // safe even if already stopped
}

void SpeechWorker::start() {
    if (m_running.exchange(true)) return; // already running

    m_ctx = whisper_init_from_file(m_modelPath.toUtf8().constData());
    if (!m_ctx) {
        m_running = false;
        emit textReady("Failed to load model.");
        emit finished();
        return;
    }

    emit textReady("Model loaded.");

    // Create timer in this thread (important)
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &SpeechWorker::onTick);
    }
    m_timer->start(3000); // every 3s (example)
}

void SpeechWorker::onTick() {
    if (!m_ctx) return;

    // TODO: capture audio, run whisper_full(), emit textReady(...)
    // emit textReady("...recognized text...");
}

void SpeechWorker::stop() {
    if (!m_running.exchange(false)) return; // already stopped

    if (m_timer) m_timer->stop();

    if (m_ctx) {
        whisper_free(m_ctx);
        m_ctx = nullptr;
    }

    emit finished();
}
