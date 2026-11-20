/*
   SPDX-FileCopyrightText: 2017 (c) Matthieu Gallien <matthieu_gallien@yahoo.fr>

   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "audiowrapper.h"
#include "powermanagementinterface.h"

#include "qtMultimediaLogging.h"

#include "elisa_settings.h"

#include <QAudio>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "config-upnp-qt.h"

/*
 * Crossfade
 *
 * Two players take turns. A few seconds before the current song ends, the other
 * player starts the next song at volume 0 and becomes the active one, while the
 * previous one keeps playing and fades out. Everything outside this class only
 * ever sees the active player: when the crossfade starts, it is told that the
 * previous song ended (so the playlist moves on as usual), and when the playlist
 * then asks for the next song, it is already playing.
 */

class AudioWrapperPrivate
{

public:

    PowerManagementInterface mPowerInterface;

    QMediaPlayer mPlayerA;

    QMediaPlayer mPlayerB;

    QAudioOutput mOutputA;

    QAudioOutput mOutputB;

    // the player everything else sees and controls
    QMediaPlayer *mPlayer = &mPlayerA;

    QAudioOutput *mOutput = &mOutputA;

    // the previous song while it fades out, or nullptr
    QMediaPlayer *mFadingPlayer = nullptr;

    QAudioOutput *mFadingOutput = nullptr;

    // volume chosen by the user (linear scale) and mute state, for both outputs
    qreal mUserVolume = 1.0;

    bool mMuted = false;

    QUrl mNextSource;

    // the crossfade has started and the playlist has not yet asked for the next song
    bool mHandoffPending = false;

    QTimer mFadeTimer;

    QElapsedTimer mFadeClock;

    qint64 mFadeLength = 0;

    qint64 mSavedPosition = 0.0;

    qint64 mUndoSavedPosition = 0.0;

    bool mHasSavedPosition = false;

    QMediaPlayer::PlaybackState mCurrentPlaybackState = QMediaPlayer::StoppedState;

    QMediaPlayer::MediaStatus mCurrentMediaStatus = QMediaPlayer::NoMedia;

    bool mQueuedStatusUpdate = false;

    QMediaDevices mMediaDevices;

    QMediaPlayer *otherPlayer() const
    {
        return mPlayer == &mPlayerA ? const_cast<QMediaPlayer *>(&mPlayerB) : const_cast<QMediaPlayer *>(&mPlayerA);
    }

    QAudioOutput *otherOutput() const
    {
        return mOutput == &mOutputA ? const_cast<QAudioOutput *>(&mOutputB) : const_cast<QAudioOutput *>(&mOutputA);
    }
};

AudioWrapper::AudioWrapper(QObject *parent) : QObject(parent), d(std::make_unique<AudioWrapperPrivate>())
{
    d->mPlayerA.setAudioOutput(&d->mOutputA);
    d->mPlayerB.setAudioOutput(&d->mOutputB);
    d->mUserVolume = d->mOutputA.volume();
    d->mMuted = d->mOutputA.isMuted();
    d->mCurrentPlaybackState = d->mPlayer->playbackState();
    d->mCurrentMediaStatus = d->mPlayer->mediaStatus();

    for (QMediaPlayer *player : {&d->mPlayerA, &d->mPlayerB}) {
        // only the active player is reported; the one fading out is not
        connect(player, &QMediaPlayer::sourceChanged, this, [this, player]() {
            if (player == d->mPlayer) {
                Q_EMIT sourceChanged();
            }
        });
        connect(player, &QMediaPlayer::playbackStateChanged, this, [this, player]() {
            if (player == d->mPlayer) {
                queueStatusChanged();
            }
        });
        connect(player, &QMediaPlayer::errorOccurred, this, [this, player](QMediaPlayer::Error error, const QString &) {
            if (player == d->mPlayer) {
                Q_EMIT errorChanged(error);
            }
        });
        connect(player, &QMediaPlayer::mediaStatusChanged, this, [this, player](QMediaPlayer::MediaStatus status) {
            if (player == d->mPlayer) {
                queueStatusChanged();
                mediaStatusChanged();
            } else if (player == d->mFadingPlayer && status == QMediaPlayer::EndOfMedia) {
                // the previous song has ended before the fade did
                finishCrossfade();
            }
        });
        connect(player, &QMediaPlayer::durationChanged, this, [this, player](qint64 duration) {
            if (player == d->mPlayer) {
                Q_EMIT durationChanged(duration);
            }
        });
        connect(player, &QMediaPlayer::positionChanged, this, [this, player](qint64 position) {
            if (player == d->mPlayer) {
                Q_EMIT positionChanged(position);
                checkCrossfade(position);
            }
        });
        connect(player, &QMediaPlayer::seekableChanged, this, [this, player](bool seekable) {
            if (player == d->mPlayer) {
                Q_EMIT seekableChanged(seekable);
            }
        });
    }

    d->mFadeTimer.setInterval(40);
    connect(&d->mFadeTimer, &QTimer::timeout, this, &AudioWrapper::crossfadeTick);

    // Signal is emitted whenever the global output device is changed and we must manually move ourselves to that device.
    connect(&d->mMediaDevices, &QMediaDevices::audioOutputsChanged, this, [this] {
        d->mOutputA.setDevice(QAudioDevice{});
        d->mOutputB.setDevice(QAudioDevice{});
    });
}

AudioWrapper::~AudioWrapper()
{
    d->mPowerInterface.setPreventSleep(false);
}

bool AudioWrapper::muted() const
{
    return d->mMuted;
}

qreal AudioWrapper::volume() const
{
    const auto userVolume = static_cast<qreal>(QAudio::convertVolume(d->mUserVolume, QAudio::LinearVolumeScale, QAudio::LogarithmicVolumeScale));

    return userVolume * 100.0;
}

QUrl AudioWrapper::source() const
{
    return d->mPlayer->source();
}

QMediaPlayer::Error AudioWrapper::error() const
{
    if (d->mPlayer->error() != QMediaPlayer::NoError) {
        qDebug() << "AudioWrapper::error" << d->mPlayer->errorString();
    }

    return d->mPlayer->error();
}

qint64 AudioWrapper::duration() const
{
    return d->mPlayer->duration();
}

qint64 AudioWrapper::position() const
{
    return d->mPlayer->position();
}

bool AudioWrapper::seekable() const
{
    return d->mPlayer->isSeekable();
}

QMediaPlayer::PlaybackState AudioWrapper::playbackState() const
{
    return d->mCurrentPlaybackState;
}

QMediaPlayer::MediaStatus AudioWrapper::status() const
{
    return d->mCurrentMediaStatus;
}

void AudioWrapper::setMuted(bool muted)
{
    if (d->mMuted == muted) {
        return;
    }

    d->mMuted = muted;
    d->mOutputA.setMuted(muted);
    d->mOutputB.setMuted(muted);
    playerMutedChanged();
}

void AudioWrapper::setVolume(qreal volume)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::setVolume" << volume;

    const auto newVolume = static_cast<qreal>(QAudio::convertVolume(volume / 100.0, QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale));
    if (qFuzzyCompare(1.0 + newVolume, 1.0 + d->mUserVolume)) {
        return;
    }
    d->mUserVolume = newVolume;
    if (d->mFadingPlayer) {
        crossfadeTick();
    } else {
        d->mOutput->setVolume(d->mUserVolume);
    }
    playerVolumeChanged();
}

void AudioWrapper::setSource(const QUrl &source)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::setSource" << source;

    if (d->mHandoffPending) {
        d->mHandoffPending = false;
        if (source == d->mPlayer->source()) {
            // the playlist moved on to the song that is already fading in
            queueStatusChanged();
            return;
        }
        // something else was chosen: drop the song that was fading in
        d->mPlayer->stop();
    }
    finishCrossfade();

    // HACK workaround for https://bugreports.qt.io/browse/QTBUG-121355
    // Playing the same source when at EndOfMedia causes the player to instantly jump the end
    if (d->mPlayer->mediaStatus() == QMediaPlayer::EndOfMedia && d->mPlayer->source() == source) {
        d->mPlayer->setPosition(0);
    } else {
        d->mPlayer->setSource(source);
    }
    queueStatusChanged();
}

void AudioWrapper::setNextSource(const QUrl &nextSource)
{
    d->mNextSource = nextSource;
}

void AudioWrapper::setPosition(qint64 position)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::setPosition" << position;

    if (d->mPlayer->duration() <= 0) {
        savePosition(position);
        return;
    }

    finishCrossfade();
    d->mPlayer->setPosition(position);
}

void AudioWrapper::play()
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::play";

    d->mPlayer->play();

    if (d->mHasSavedPosition) {
        qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::playerDurationSignalChanges" << "restore old position" << d->mSavedPosition;

        setPosition(d->mSavedPosition);
        d->mHasSavedPosition = false;
    }
}

void AudioWrapper::pause()
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::pause";

    finishCrossfade();
    d->mPlayer->pause();
}

void AudioWrapper::stop()
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::stop";

    finishCrossfade();
    d->mPlayer->stop();
}

void AudioWrapper::seek(qint64 position)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::seek" << position;

    finishCrossfade();
    d->mPlayer->setPosition(position);
}

void AudioWrapper::checkCrossfade(qint64 position)
{
    if (d->mFadingPlayer || d->mHandoffPending) {
        return;
    }

    const int seconds = Elisa::ElisaConfiguration::self()->crossfadeSeconds();
    if (seconds <= 0) {
        return;
    }

    // only between two local songs (not radios), while playing
    if (!d->mNextSource.isValid() || !d->mNextSource.isLocalFile() || !d->mPlayer->source().isLocalFile()) {
        return;
    }
    if (d->mPlayer->playbackState() != QMediaPlayer::PlayingState) {
        return;
    }

    const qint64 fadeLength = seconds * 1000;
    const qint64 duration = d->mPlayer->duration();
    // very short songs are not crossfaded
    if (duration < fadeLength * 3) {
        return;
    }
    if (position < duration - fadeLength) {
        return;
    }

    startCrossfade(std::max<qint64>(duration - position, 500));
}

void AudioWrapper::startCrossfade(qint64 fadeLength)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::startCrossfade" << d->mNextSource << fadeLength;

    QMediaPlayer *nextPlayer = d->otherPlayer();
    QAudioOutput *nextOutput = d->otherOutput();

    nextOutput->setMuted(d->mMuted);
    nextOutput->setVolume(0);
    nextPlayer->setSource(d->mNextSource);
    nextPlayer->play();

    d->mFadingPlayer = d->mPlayer;
    d->mFadingOutput = d->mOutput;
    d->mPlayer = nextPlayer;
    d->mOutput = nextOutput;

    d->mFadeLength = fadeLength;
    d->mFadeClock.start();
    d->mFadeTimer.start();

    // Tell the rest of Elisa that the previous song has ended, so the playlist
    // moves on to the next song (which is already starting).
    d->mHandoffPending = true;
    d->mCurrentMediaStatus = QMediaPlayer::EndOfMedia;
    Q_EMIT statusChanged(d->mCurrentMediaStatus);
    d->mCurrentPlaybackState = QMediaPlayer::StoppedState;
    Q_EMIT playbackStateChanged(d->mCurrentPlaybackState);

    // if the playlist does not ask for the next song (it ended, or it changed), stop waiting
    QTimer::singleShot(3000, this, [this]() {
        if (d->mHandoffPending) {
            d->mHandoffPending = false;
            queueStatusChanged();
        }
    });
}

void AudioWrapper::crossfadeTick()
{
    if (!d->mFadingPlayer) {
        d->mFadeTimer.stop();
        return;
    }

    const qreal progress = d->mFadeLength > 0 ? std::clamp(static_cast<qreal>(d->mFadeClock.elapsed()) / d->mFadeLength, 0.0, 1.0) : 1.0;

    // equal-power curves: the overall loudness stays even during the fade
    d->mOutput->setVolume(d->mUserVolume * std::sin(progress * M_PI_2));
    d->mFadingOutput->setVolume(d->mUserVolume * std::cos(progress * M_PI_2));

    if (progress >= 1.0) {
        finishCrossfade();
    }
}

void AudioWrapper::finishCrossfade()
{
    d->mFadeTimer.stop();

    if (d->mFadingPlayer) {
        qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::finishCrossfade";

        QMediaPlayer *fadingPlayer = d->mFadingPlayer;
        d->mFadingPlayer = nullptr;
        d->mFadingOutput = nullptr;
        fadingPlayer->stop();
        fadingPlayer->setSource(QUrl{});
    }

    d->mOutput->setVolume(d->mUserVolume);
}

void AudioWrapper::mediaStatusChanged()
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::mediaStatusChanged" << d->mPlayer->mediaStatus();
}

void AudioWrapper::playerStateChanged()
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::playerStateChanged" << d->mPlayer->playbackState();

    switch(d->mPlayer->playbackState())
    {
    case QMediaPlayer::PlaybackState::StoppedState:
        Q_EMIT stopped();
        d->mPowerInterface.setPreventSleep(false);
        break;
    case QMediaPlayer::PlaybackState::PlayingState:
        Q_EMIT playing();
        d->mPowerInterface.setPreventSleep(true);
        break;
    case QMediaPlayer::PlaybackState::PausedState:
        Q_EMIT paused();
        d->mPowerInterface.setPreventSleep(false);
        break;
    }
}

void AudioWrapper::playerVolumeChanged()
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::playerVolumeChanged" << d->mUserVolume;

    QTimer::singleShot(0, this, [this]() {Q_EMIT volumeChanged();});
}

void AudioWrapper::playerMutedChanged()
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::playerMutedChanged";

    QTimer::singleShot(0, this, [this]() {Q_EMIT mutedChanged(muted());});
}

void AudioWrapper::playerStateSignalChanges(QMediaPlayer::PlaybackState newState)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::playerStateSignalChanges" << newState;

    QMetaObject::invokeMethod(this, [this, newState]() {Q_EMIT playbackStateChanged(newState);}, Qt::QueuedConnection);
}

void AudioWrapper::mediaStatusSignalChanges(QMediaPlayer::MediaStatus newStatus)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::mediaStatusSignalChanges" << newStatus;

    QMetaObject::invokeMethod(this, [this, newStatus]() {Q_EMIT statusChanged(newStatus);}, Qt::QueuedConnection);
}

void AudioWrapper::playerDurationSignalChanges(qint64 newDuration)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::playerDurationSignalChanges" << newDuration;

    QMetaObject::invokeMethod(this, [this, newDuration]() {Q_EMIT durationChanged(newDuration);}, Qt::QueuedConnection);
}

void AudioWrapper::playerPositionSignalChanges(qint64 newPosition)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::playerPositionSignalChanges" << newPosition;

    QMetaObject::invokeMethod(this, [this, newPosition]() {Q_EMIT positionChanged(newPosition);}, Qt::QueuedConnection);
}

void AudioWrapper::playerVolumeSignalChanges()
{
    QMetaObject::invokeMethod(this, [this]() {Q_EMIT volumeChanged();}, Qt::QueuedConnection);
}

void AudioWrapper::playerMutedSignalChanges(bool isMuted)
{
    QMetaObject::invokeMethod(this, [this, isMuted]() {Q_EMIT mutedChanged(isMuted);}, Qt::QueuedConnection);
}

void AudioWrapper::playerSeekableSignalChanges(bool isSeekable)
{
    QMetaObject::invokeMethod(this, [this, isSeekable]() {Q_EMIT seekableChanged(isSeekable);}, Qt::QueuedConnection);
}

void AudioWrapper::saveUndoPosition(qint64 position)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::saveUndoPosition" << position;

    d->mUndoSavedPosition = position;
}

void AudioWrapper::restoreUndoPosition()
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::restoreUndoPosition";

    d->mHasSavedPosition = true;
    d->mSavedPosition = d->mUndoSavedPosition;
}

void AudioWrapper::savePosition(qint64 position)
{
    qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::savePosition" << position;

    if (!d->mHasSavedPosition) {
        d->mHasSavedPosition = true;
        d->mSavedPosition = position;
        qCDebug(orgKdeElisaPlayerQtMultimedia) << "AudioWrapper::savePosition" << "restore old position" << d->mSavedPosition;
    }
}

void AudioWrapper::notifyStatusChanges()
{
    d->mQueuedStatusUpdate = false;

    // while the playlist moves on to the song that is fading in, keep reporting the end of the previous one
    if (d->mHandoffPending) {
        return;
    }

    if (d->mPlayer->mediaStatus() != d->mCurrentMediaStatus) {
        d->mCurrentMediaStatus = d->mPlayer->mediaStatus();
        Q_EMIT statusChanged(d->mCurrentMediaStatus);
    }
    if (d->mPlayer->playbackState() != d->mCurrentPlaybackState) {
        d->mCurrentPlaybackState = d->mPlayer->playbackState();
        Q_EMIT playbackStateChanged(d->mCurrentPlaybackState);
        playerStateChanged();
    }
}

void AudioWrapper::queueStatusChanged()
{
    if (!d->mQueuedStatusUpdate) {
        QTimer::singleShot(0, this, &AudioWrapper::notifyStatusChanges);
        d->mQueuedStatusUpdate = true;
    }
}


#include "moc_audiowrapper.cpp"
