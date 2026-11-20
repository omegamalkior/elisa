/*
   SPDX-FileCopyrightText: 2026 Elisa album-artist fork

   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "albumlookup.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrentRun>

using namespace Qt::Literals::StringLiterals;

namespace
{
QMutex cacheMutex;
QHash<QString, QString> cache; // "artist\ntitle" (lower case) -> album, "" when not found
bool cacheLoaded = false;
QSet<QString> pending;

QMutex requestMutex; // one request at a time, a second apart
qint64 lastRequest = 0;

QString cacheFile()
{
    const auto folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir{}.mkpath(folder);
    return folder + u"/album-lookups.json"_s;
}

QString keyOf(const QString &artist, const QString &title)
{
    return artist.trimmed().toLower() + u'\n' + title.trimmed().toLower();
}

void loadCache()
{
    if (cacheLoaded) {
        return;
    }
    cacheLoaded = true;
    QFile file{cacheFile()};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        cache.insert(it.key(), it.value().toString());
    }
}

void saveCache()
{
    QJsonObject object;
    for (auto it = cache.cbegin(); it != cache.cend(); ++it) {
        object.insert(it.key(), it.value());
    }
    QFile file{cacheFile()};
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument{object}.toJson(QJsonDocument::Compact));
    }
}

// the band's own studio album first; compilations, live records, promos and singles last;
// the oldest release wins among equals
QString bestAlbum(const QJsonDocument &answer, const QString &artist, const QString &title)
{
    QString best;
    int bestRank = -1;
    QString bestDate;
    const auto recordings = answer.object().value(u"recordings"_s).toArray();
    for (const auto &recordingValue : recordings) {
        const auto recording = recordingValue.toObject();
        if (recording.value(u"score"_s).toInt() < 90
            || recording.value(u"title"_s).toString().compare(title, Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool sameArtist = false;
        const auto credits = recording.value(u"artist-credit"_s).toArray();
        for (const auto &credit : credits) {
            if (credit.toObject().value(u"name"_s).toString().compare(artist, Qt::CaseInsensitive) == 0) {
                sameArtist = true;
            }
        }
        if (!sameArtist) {
            continue;
        }
        const auto releases = recording.value(u"releases"_s).toArray();
        for (const auto &releaseValue : releases) {
            const auto release = releaseValue.toObject();
            const auto group = release.value(u"release-group"_s).toObject();
            const auto type = group.value(u"primary-type"_s).toString();
            const bool plain = group.value(u"secondary-types"_s).toArray().isEmpty();
            const bool official = release.value(u"status"_s).toString() == u"Official"_s;
            int rank = 0;
            if (type == u"Album"_s) {
                rank += 4;
            } else if (type == u"EP"_s) {
                rank += 2;
            }
            rank += plain ? 2 : 0;
            rank += official ? 1 : 0;
            const auto date = release.value(u"date"_s).toString();
            const bool earlier = !date.isEmpty() && (bestDate.isEmpty() || date < bestDate);
            if (rank > bestRank || (rank == bestRank && earlier)) {
                best = release.value(u"title"_s).toString();
                bestRank = rank;
                bestDate = date;
            }
        }
    }
    return best;
}

QString quoted(QString text)
{
    text.remove(u'"');
    text.remove(u'\\');
    return u'"' + text + u'"';
}
}

AlbumLookup::AlbumLookup() = default;

AlbumLookup *AlbumLookup::instance()
{
    static auto *lookup = [] {
        auto *created = new AlbumLookup;
        if (QCoreApplication::instance()) {
            created->moveToThread(QCoreApplication::instance()->thread());
        }
        return created;
    }();
    return lookup;
}

QString AlbumLookup::cachedAlbum(const QString &artist, const QString &title, bool *known)
{
    QMutexLocker locker{&cacheMutex};
    loadCache();
    const auto it = cache.constFind(keyOf(artist, title));
    if (known) {
        *known = it != cache.cend();
    }
    return it != cache.cend() ? it.value() : QString{};
}

QString AlbumLookup::lookup(const QString &artist, const QString &title, int timeoutMs)
{
    if (artist.trimmed().isEmpty() || title.trimmed().isEmpty()) {
        return {};
    }
    bool known = false;
    const auto cached = cachedAlbum(artist, title, &known);
    if (known) {
        return cached;
    }

    QMutexLocker requestLocker{&requestMutex};
    // asked by another thread meanwhile?
    const auto again = cachedAlbum(artist, title, &known);
    if (known) {
        return again;
    }

    QUrl url{u"https://musicbrainz.org/ws/2/recording"_s};
    QUrlQuery query;
    query.addQueryItem(u"query"_s, u"recording:"_s + quoted(title) + u" AND artist:"_s + quoted(artist));
    query.addQueryItem(u"fmt"_s, u"json"_s);
    query.addQueryItem(u"limit"_s, u"10"_s);
    url.setQuery(query);

    QNetworkAccessManager network;
    QString album;
    bool answered = false;
    for (int attempt = 0; attempt < 3 && !answered; ++attempt) {
        // a second between requests, longer after "busy"
        const auto wait = lastRequest + 1100 * (attempt + 1) - QDateTime::currentMSecsSinceEpoch();
        if (wait > 0) {
            QThread::msleep(static_cast<unsigned long>(wait));
        }
        lastRequest = QDateTime::currentMSecsSinceEpoch();

        QNetworkRequest request{url};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          u"ElisaAlbumArtistFork/%1 ( https://github.com/omegamalkior/elisa )"_s.arg(QCoreApplication::applicationVersion()));
        request.setTransferTimeout(timeoutMs);
        auto *reply = network.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(timeoutMs + 1000, &loop, &QEventLoop::quit);
        loop.exec();

        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->isFinished() && reply->error() == QNetworkReply::NoError && status == 200) {
            album = bestAlbum(QJsonDocument::fromJson(reply->readAll()), artist.trimmed(), title.trimmed());
            // MusicBrainz writes typographic hyphens ("One‐X"): the plain one matches tagged albums
            album.replace(QChar(0x2010), u'-').replace(QChar(0x2011), u'-');
            answered = true;
        } else {
            qWarning() << "AlbumLookup: MusicBrainz did not answer for" << artist << title << status << reply->errorString();
        }
        reply->deleteLater();
    }

    if (answered) {
        // remember also "not found"; a song nobody could answer for is asked again next time
        QMutexLocker locker{&cacheMutex};
        cache.insert(keyOf(artist, title), album);
        saveCache();
    }
    return album;
}

void AlbumLookup::requestInBackground(const QString &artist, const QString &title, const QUrl &file)
{
    {
        QMutexLocker locker{&cacheMutex};
        const auto key = keyOf(artist, title);
        if (pending.contains(key)) {
            return;
        }
        pending.insert(key);
    }
    auto future = QtConcurrent::run([this, artist, title, file] {
        const auto album = lookup(artist, title);
        {
            QMutexLocker locker{&cacheMutex};
            pending.remove(keyOf(artist, title));
        }
        if (!album.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, file, album] {
                Q_EMIT albumFound(file, album);
            }, Qt::QueuedConnection);
        }
    });
    Q_UNUSED(future)
}
