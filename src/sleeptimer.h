/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef SLEEPTIMER_H
#define SLEEPTIMER_H

#include "elisaLib_export.h"

#include <QElapsedTimer>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

/**
 * Pauses playback after a number of minutes, or at the end of the current track.
 *
 * The class only decides when to pause: it emits timeout() and the owner pauses the player.
 */
class ELISALIB_EXPORT SleepTimer : public QObject
{
    Q_OBJECT

    QML_ELEMENT

    QML_UNCREATABLE("")

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

    Q_PROPERTY(int remainingMinutes READ remainingMinutes NOTIFY remainingMinutesChanged)

    Q_PROPERTY(bool stopAfterCurrentTrack READ stopAfterCurrentTrack WRITE setStopAfterCurrentTrack NOTIFY stopAfterCurrentTrackChanged)

public:
    explicit SleepTimer(QObject *parent = nullptr);

    [[nodiscard]] bool active() const;

    /// Minutes left, rounded up (0 when the timer is off)
    [[nodiscard]] int remainingMinutes() const;

    [[nodiscard]] bool stopAfterCurrentTrack() const;

    /// Start (or restart) the timer. A value of 0 or less switches it off.
    Q_INVOKABLE void start(int minutes);

    Q_INVOKABLE void cancel();

    /// Length of a "minute" in milliseconds. Only changed by tests.
    void setMillisecondsPerMinute(int value);

public Q_SLOTS:

    void setStopAfterCurrentTrack(bool value);

    /// Call with the player position and track duration (ms) while playing
    void trackPositionChanged(qint64 position, qint64 duration);

    /// Call when the player moves on to another track
    void trackChanged();

Q_SIGNALS:

    void activeChanged();

    void remainingMinutesChanged();

    void stopAfterCurrentTrackChanged();

    /// Playback should be paused now
    void timeout();

private:
    void tick();

    void setRemainingMinutes(int value);

    QTimer mTick;

    QElapsedTimer mElapsed;

    qint64 mTotalMilliseconds = 0;

    int mMillisecondsPerMinute = 60000;

    int mRemainingMinutes = 0;

    bool mStopAfterCurrentTrack = false;
};

#endif // SLEEPTIMER_H
