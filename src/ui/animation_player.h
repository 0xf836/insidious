#pragma once

#include "core/animation_decode_session.h"
#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <optional>

namespace insidious::ui {
class AnimationPlayer : public QObject {
    Q_OBJECT
public:
    explicit AnimationPlayer(QObject *parent = nullptr);
    ~AnimationPlayer() override;
    void open(const core::ImageEntry &entry, const core::DecodeRequest &request, const QString &password);
    void stop();
    void setView(const core::DecodeRequest &request);
    void setSuspended(bool suspended);
    void toggle();
    [[nodiscard]] bool available() const { return m_info.animated() && !m_failed; }
    [[nodiscard]] bool owns(const core::ImageEntry &entry) const { return m_entry.identityKey() == entry.identityKey() && available(); }
    [[nodiscard]] QString sourceKey() const { return m_entry.isValid() ? m_entry.identityKey() : QString(); }
    [[nodiscard]] bool playing() const { return available() && !m_paused && !m_finished; }
    [[nodiscard]] int frameIndex() const { return m_index; }
signals:
    void frameReady(const insidious::core::DecodedImage &frame);
    void failed(const QString &message);
    void stateChanged();
private:
    friend class AnimationTests;
    enum class Work { Close, Open, Render, Next, Restart };
    struct Worker : QObject {
        std::unique_ptr<core::AnimationDecodeSession> session;
    };
    void pump();
    void freeze();
    void resume();
    void present();
    QThread m_thread;
    Worker *m_worker;
    std::shared_ptr<std::atomic<quint64>> m_epoch;
    quint64 m_viewEpoch = 0;
    core::ImageEntry m_entry;
    core::DecodeRequest m_request;
    core::DecodeRequest m_restoreRequest;
    QString m_password;
    core::AnimationInfo m_info;
    QTimer m_timer;
    QElapsedTimer m_clock;
    std::optional<core::DecodedImage> m_next;
    qint64 m_displayBytes = 0;
    int m_index = 0;
    int m_plays = 0;
    int m_remaining = 0;
    bool m_busy = false;
    bool m_closePending = false;
    bool m_openPending = false;
    bool m_renderPending = false;
    bool m_restartPending = false;
    bool m_restarting = false;
    bool m_nextRestarts = false;
    bool m_paused = false;
    bool m_suspended = false;
    bool m_finished = false;
    bool m_failed = false;
};
}
