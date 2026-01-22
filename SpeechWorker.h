#pragma once
#include <QObject>
#include <QString>
#include <atomic>

struct whisper_context;
class QTimer;

class SpeechWorker : public QObject {
    Q_OBJECT
public:
    explicit SpeechWorker(QString modelPath, QObject* parent=nullptr);
    ~SpeechWorker() override;

public slots:
    void start();
    void stop();

signals:
    void textReady(const QString& text);
    void finished();

private slots:
    void onTick();

private:
    QString m_modelPath;
    whisper_context* m_ctx = nullptr;

    QTimer* m_timer = nullptr;
    std::atomic_bool m_running{false};
};
