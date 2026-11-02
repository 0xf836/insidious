#include "ui/animation_player.h"
#include <QMetaObject>
#include <algorithm>

namespace insidious::ui {
AnimationPlayer::AnimationPlayer(QObject *parent)
    : QObject(parent), m_worker(new Worker), m_epoch(std::make_shared<std::atomic<quint64>>(0))
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_restarting) { return; }
        m_remaining = 0;
        if (m_index + 1 == m_info.delaysMs.size() && m_info.totalPlays > 0 && m_plays + 1 >= m_info.totalPlays) {
            m_finished = true;
            emit stateChanged();
        } else if (m_next) {
            present();
        } else { pump(); }
    });
}
AnimationPlayer::~AnimationPlayer()
{
    ++*m_epoch;
    m_thread.quit();
    m_thread.wait();
}
void AnimationPlayer::stop()
{
    ++*m_epoch;
    ++m_viewEpoch;
    m_timer.stop();
    m_entry = {};
    m_password.clear();
    m_info = {};
    m_next.reset();
    m_openPending = m_renderPending = m_restartPending = false;
    m_paused = m_finished = m_failed = m_restarting = false;
    m_displayBytes = 0;
    m_closePending = true;
    pump();
    emit stateChanged();
}
void AnimationPlayer::open(const core::ImageEntry &entry, const core::DecodeRequest &request, const QString &password)
{
    if (m_entry.identityKey() == entry.identityKey()) {
        if (request.cacheKeySuffix() != m_request.cacheKeySuffix()) { setView(request); }
        return;
    }
    stop();
    if (!core::AnimationDecodeSession::candidate(entry)) { return; }
    m_entry = entry;
    m_request = request;
    m_password = password;
    m_index = m_plays = 0;
    m_openPending = true;
    pump();
}
void AnimationPlayer::freeze()
{
    if (m_timer.isActive()) {
        m_remaining = std::max(0, m_remaining - static_cast<int>(m_clock.elapsed()));
        m_timer.stop();
    }
}
void AnimationPlayer::resume()
{
    if (!playing() || m_suspended || m_renderPending || m_openPending || m_restarting) { return; }
    if (m_remaining == 0 && m_next) { present(); return; }
    if (!m_timer.isActive()) {
        m_clock.start();
        m_timer.start(m_remaining);
    }
    pump();
}
void AnimationPlayer::setSuspended(bool suspended)
{
    if (m_suspended == suspended) { return; }
    freeze();
    m_suspended = suspended;
    if (!suspended) { resume(); }
}
void AnimationPlayer::toggle()
{
    if (!available()) { return; }
    freeze();
    if (m_finished) {
        m_finished = false;
        m_paused = false;
        m_plays = 0;
        m_restartPending = true;
        m_restarting = true;
        m_next.reset();
    } else { m_paused = !m_paused; }
    emit stateChanged();
    resume();
    pump();
}
void AnimationPlayer::setView(const core::DecodeRequest &request)
{
    freeze();
    if (!m_renderPending) { m_restoreRequest = m_request; }
    m_request = request;
    ++m_viewEpoch;
    m_next.reset();
    m_renderPending = true;
    pump();
}
void AnimationPlayer::present()
{
    auto frame = std::move(*m_next);
    m_next.reset();
    if (m_nextRestarts) { m_index = 0; ++m_plays; }
    else { ++m_index; }
    m_remaining = m_info.delaysMs[m_index];
    m_displayBytes = frame.image.sizeInBytes();
    emit frameReady(frame);
    resume();
}
void AnimationPlayer::pump()
{
    if (m_busy) { return; }
    const bool terminal = m_index + 1 == m_info.delaysMs.size() && m_info.totalPlays > 0 && m_plays + 1 >= m_info.totalPlays;
    Work work;
    if (m_openPending) { work = Work::Open; m_openPending = false; m_closePending = false; }
    else if (m_closePending) { work = Work::Close; m_closePending = false; }
    else if (m_restartPending) { work = Work::Restart; m_restartPending = false; }
    else if (m_renderPending && available()) { work = Work::Render; }
    else if (playing() && !m_suspended && !m_next && !terminal) { work = Work::Next; }
    else { return; }
    m_busy = true;
    const quint64 epoch = m_epoch->load();
    const quint64 viewEpoch = m_viewEpoch;
    const auto entry = m_entry;
    const auto request = m_request;
    const auto password = m_password;
    const int index = m_index;
    const qint64 displayBytes = m_displayBytes;
    const bool restart = work == Work::Next && index + 1 == m_info.delaysMs.size();
    QMetaObject::invokeMethod(m_worker, [this, worker = m_worker, token = m_epoch, epoch, viewEpoch,
                                      entry, request, password, index, displayBytes, work, restart] {
        core::DecodedImage frame;
        core::AnimationInfo info;
        QString error;
        if (epoch == token->load()) {
            if (work == Work::Close) { worker->session.reset(); }
            if (work == Work::Open) {
                worker->session = std::make_unique<core::AnimationDecodeSession>();
                if (!worker->session->open(entry, password)) { error = worker->session->errorMessage(); }
            }
            if (worker->session && error.isEmpty() && epoch == token->load()) {
                auto &session = *worker->session;
                info = session.info();
                if (info.animated()) {
                    if (work == Work::Restart || (restart && session.frameIndex() != 0)) {
                        if (!session.restart() || !session.advance()) { error = session.errorMessage(); }
                    } else if (work == Work::Next && session.frameIndex() == index) {
                        if (!session.advance()) { error = session.errorMessage(); }
                    }
                    if (error.isEmpty() && epoch == token->load()) {
                        frame = session.render(request, displayBytes, work == Work::Render ? index : -1);
                        if (!frame.ok()) { error = frame.errorMessage; }
                    }
                } else {
                    worker->session.reset();
                }
            }
        }
        QMetaObject::invokeMethod(this, [this, epoch, viewEpoch, work, restart, info, frame, error] {
            m_busy = false;
            if (epoch != m_epoch->load() || work == Work::Close) { pump(); return; }
            if (work == Work::Open) { m_info = info; emit stateChanged(); }
            if (work == Work::Restart && error.isEmpty()) {
                m_restarting = false;
                m_index = 0;
                m_remaining = info.delaysMs[0];
            }
            if (viewEpoch != m_viewEpoch) { m_renderPending = true; pump(); return; }
            if (!error.isEmpty() && work == Work::Render) {
                m_request = m_restoreRequest;
                m_renderPending = false;
                emit failed(error);
                resume();
                return;
            }
            if (!error.isEmpty()) {
                m_failed = true;
                m_timer.stop();
                m_next.reset();
                emit failed(error);
                emit stateChanged();
                return;
            }
            if (!info.animated()) { return; }
            if (work == Work::Next) {
                m_next = frame;
                m_nextRestarts = restart;
            } else {
                m_renderPending = false;
                if (work != Work::Render) {
                    m_index = 0;
                    m_remaining = info.delaysMs[0];
                }
                m_displayBytes = frame.image.sizeInBytes();
                emit frameReady(frame);
            }
            resume();
            pump();
        });
    });
}
}
