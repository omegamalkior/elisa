/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "sleeptimer.h"

#include <algorithm>

namespace
{
// pause this close to the end of a track when "stop after current track" is on
constexpr qint64 endOfTrackMargin = 600;
}

SleepTimer::SleepTimer(QObject *parent)
    : QObject(parent)
{
    mTick.setInterval(1000);
    connect(&mTick, &QTimer::timeout, this, &SleepTimer::tick);
}

bool SleepTimer::active() const
{
    return mTick.isActive();
}

int SleepTimer::remainingMinutes() const
{
    return mRemainingMinutes;
}

bool SleepTimer::stopAfterCurrentTrack() const
{
    return mStopAfterCurrentTrack;
}

void SleepTimer::start(int minutes)
{
    if (minutes <= 0) {
        cancel();
        return;
    }

    mTotalMilliseconds = qint64(minutes) * mMillisecondsPerMinute;
    mElapsed.start();
    setRemainingMinutes(minutes);

    if (!mTick.isActive()) {
        mTick.start();
        Q_EMIT activeChanged();
    }
}

void SleepTimer::cancel()
{
    if (mTick.isActive()) {
        mTick.stop();
        Q_EMIT activeChanged();
    }

    setRemainingMinutes(0);
}

void SleepTimer::setMillisecondsPerMinute(int value)
{
    mMillisecondsPerMinute = std::max(1, value);
    mTick.setInterval(std::clamp(mMillisecondsPerMinute / 4, 1, 1000));
}

void SleepTimer::setStopAfterCurrentTrack(bool value)
{
    if (mStopAfterCurrentTrack == value) {
        return;
    }

    mStopAfterCurrentTrack = value;
    Q_EMIT stopAfterCurrentTrackChanged();
}

void SleepTimer::trackPositionChanged(qint64 position, qint64 duration)
{
    if (!mStopAfterCurrentTrack || duration <= 0) {
        return;
    }

    if (duration - position <= endOfTrackMargin) {
        setStopAfterCurrentTrack(false);
        Q_EMIT timeout();
    }
}

void SleepTimer::trackChanged()
{
    if (!mStopAfterCurrentTrack) {
        return;
    }

    // the end of the track was not seen in time: pause the track that just started
    setStopAfterCurrentTrack(false);
    QTimer::singleShot(250, this, &SleepTimer::timeout);
}

void SleepTimer::tick()
{
    const auto remaining = mTotalMilliseconds - mElapsed.elapsed();

    if (remaining <= 0) {
        cancel();
        Q_EMIT timeout();
        return;
    }

    setRemainingMinutes(int((remaining + mMillisecondsPerMinute - 1) / mMillisecondsPerMinute));
}

void SleepTimer::setRemainingMinutes(int value)
{
    if (mRemainingMinutes == value) {
        return;
    }

    mRemainingMinutes = value;
    Q_EMIT remainingMinutesChanged();
}

#include "moc_sleeptimer.cpp"
