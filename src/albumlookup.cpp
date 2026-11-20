/*
   SPDX-FileCopyrightText: 2026 Elisa album-artist fork

   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "albumlookup.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>
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

// GET with retries; MusicBrainz requests are a second apart (requestMutex held by the caller)
QByteArray httpGet(const QUrl &url, bool *ok, int timeoutMs = 15000)
{
    *ok = false;
    const bool musicBrainz = url.host() == u"musicbrainz.org"_s;
    QNetworkAccessManager network;
    QByteArray body;
    for (int attempt = 0; attempt < 3 && !*ok; ++attempt) {
        if (musicBrainz) {
            const auto wait = lastRequest + 1100 * (attempt + 1) - QDateTime::currentMSecsSinceEpoch();
            if (wait > 0) {
                QThread::msleep(static_cast<unsigned long>(wait));
            }
            lastRequest = QDateTime::currentMSecsSinceEpoch();
        } else if (attempt > 0) {
            QThread::msleep(1000);
        }
        QNetworkRequest request{url};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          u"ElisaAlbumArtistFork/%1 ( https://github.com/omegamalkior/elisa )"_s.arg(QCoreApplication::applicationVersion()));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(timeoutMs);
        auto *reply = network.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(timeoutMs + 1000, &loop, &QEventLoop::quit);
        loop.exec();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->isFinished() && reply->error() == QNetworkReply::NoError && status == 200) {
            body = reply->readAll();
            *ok = true;
        } else if (status == 404) {
            *ok = true; // a real answer: nothing there
            body.clear();
            reply->deleteLater();
            break;
        } else {
            qWarning() << "AlbumLookup: no answer from" << url.host() << status << reply->errorString();
        }
        reply->deleteLater();
    }
    return body;
}

QUrl musicBrainzUrl(const QString &path, const QString &query)
{
    QUrl url{u"https://musicbrainz.org/ws/2/"_s + path};
    QUrlQuery items;
    if (!query.isEmpty()) {
        items.addQueryItem(u"query"_s, query);
    }
    items.addQueryItem(u"fmt"_s, u"json"_s);
    url.setQuery(items);
    return url;
}

QString artworkFolder()
{
    const auto folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/artwork"_s;
    QDir{}.mkpath(folder);
    return folder;
}

// a file name for a downloaded picture: a hash of what it shows
QString artworkPath(const QString &kind, const QString &key, const QByteArray &data)
{
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    const auto extension = data.startsWith("\x89PNG") ? u".png"_s : u".jpg"_s;
    return artworkFolder() + u'/' + kind + u'-' + hash + extension;
}

// the cached answer for a key: known (asked before) and the value (album, or picture file that still exists)
QString cachedValue(const QString &key, bool *known, bool isFile)
{
    QMutexLocker locker{&cacheMutex};
    loadCache();
    const auto it = cache.constFind(key);
    bool isKnown = it != cache.cend();
    QString value = isKnown ? it.value() : QString{};
    if (isFile && !value.isEmpty() && !QFileInfo::exists(value)) {
        isKnown = false; // the picture was removed: download it again
        value.clear();
    }
    if (known) {
        *known = isKnown;
    }
    return value;
}

void storeValue(const QString &key, const QString &value)
{
    QMutexLocker locker{&cacheMutex};
    cache.insert(key, value);
    saveCache();
}

bool startPending(const QString &key)
{
    QMutexLocker locker{&cacheMutex};
    if (pending.contains(key)) {
        return false;
    }
    pending.insert(key);
    return true;
}

void endPending(const QString &key)
{
    QMutexLocker locker{&cacheMutex};
    pending.remove(key);
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

    const auto url = musicBrainzUrl(u"recording"_s, u"recording:"_s + quoted(title) + u" AND artist:"_s + quoted(artist));
    bool answered = false;
    const auto body = httpGet(url, &answered);
    QString album;
    if (answered && !body.isEmpty()) {
        album = bestAlbum(QJsonDocument::fromJson(body), artist.trimmed(), title.trimmed());
        // MusicBrainz writes typographic hyphens ("One‐X"): the plain one matches tagged albums
        album.replace(QChar(0x2010), u'-').replace(QChar(0x2011), u'-');
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

QString AlbumLookup::cachedCover(const QString &artist, const QString &album, bool *known)
{
    return cachedValue(u"cover\n"_s + keyOf(artist, album), known, true);
}

QString AlbumLookup::fetchCover(const QString &artist, const QString &album)
{
    if (artist.trimmed().isEmpty() || album.trimmed().isEmpty()) {
        return {};
    }
    const auto key = u"cover\n"_s + keyOf(artist, album);
    bool known = false;
    auto cover = cachedValue(key, &known, true);
    if (known) {
        return cover;
    }
    QMutexLocker requestLocker{&requestMutex};
    cover = cachedValue(key, &known, true);
    if (known) {
        return cover;
    }

    // the album (release group) on MusicBrainz, then its front cover on the Cover Art Archive
    bool ok = false;
    const auto body = httpGet(musicBrainzUrl(u"release-group"_s, u"releasegroup:"_s + quoted(album) + u" AND artist:"_s + quoted(artist)), &ok);
    if (!ok) {
        return {};
    }
    QStringList groups;
    const auto found = QJsonDocument::fromJson(body).object().value(u"release-groups"_s).toArray();
    for (const auto &value : found) {
        const auto group = value.toObject();
        auto title = group.value(u"title"_s).toString();
        title.replace(QChar(0x2010), u'-').replace(QChar(0x2011), u'-');
        if (group.value(u"score"_s).toInt() >= 90 && title.compare(album.trimmed(), Qt::CaseInsensitive) == 0) {
            groups.push_back(group.value(u"id"_s).toString());
        }
    }
    for (const auto &id : std::as_const(groups)) {
        bool imageOk = false;
        const auto image = httpGet(QUrl{u"https://coverartarchive.org/release-group/%1/front-500"_s.arg(id)}, &imageOk, 30000);
        if (imageOk && !image.isEmpty()) {
            const auto path = artworkPath(u"cover"_s, key, image);
            QFile file{path};
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(image) == image.size()) {
                file.close();
                storeValue(key, path);
                return path;
            }
        }
    }
    storeValue(key, QString{}); // asked: no cover there
    return {};
}

void AlbumLookup::requestCover(const QString &artist, const QString &album, const QUrl &file)
{
    const auto key = u"cover\n"_s + keyOf(artist, album);
    if (!startPending(key)) {
        return;
    }
    auto future = QtConcurrent::run([this, artist, album, file, key] {
        const auto cover = fetchCover(artist, album);
        endPending(key);
        if (!cover.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, file, cover] {
                Q_EMIT coverFound(file, cover);
            }, Qt::QueuedConnection);
        }
    });
    Q_UNUSED(future)
}

QString AlbumLookup::cachedArtistPhoto(const QString &artist, bool *known)
{
    return cachedValue(u"artist\n"_s + artist.trimmed().toLower(), known, true);
}

QString AlbumLookup::fetchArtistPhoto(const QString &artist)
{
    if (artist.trimmed().isEmpty()) {
        return {};
    }
    const auto key = u"artist\n"_s + artist.trimmed().toLower();
    bool known = false;
    auto photo = cachedValue(key, &known, true);
    if (known) {
        return photo;
    }
    QMutexLocker requestLocker{&requestMutex};
    photo = cachedValue(key, &known, true);
    if (known) {
        return photo;
    }

    // 1. the artist on MusicBrainz
    bool ok = false;
    const auto body = httpGet(musicBrainzUrl(u"artist"_s, u"artist:"_s + quoted(artist)), &ok);
    if (!ok) {
        return {};
    }
    QString artistId;
    const auto artists = QJsonDocument::fromJson(body).object().value(u"artists"_s).toArray();
    for (const auto &value : artists) {
        const auto entry = value.toObject();
        if (entry.value(u"score"_s).toInt() >= 90 && entry.value(u"name"_s).toString().compare(artist.trimmed(), Qt::CaseInsensitive) == 0) {
            artistId = entry.value(u"id"_s).toString();
            break;
        }
    }

    // 2. its Wikidata entry, 3. the picture there (Wikimedia Commons)
    QString wikidataId;
    if (!artistId.isEmpty()) {
        const auto relations = httpGet(QUrl{u"https://musicbrainz.org/ws/2/artist/%1?inc=url-rels&fmt=json"_s.arg(artistId)}, &ok);
        const auto list = QJsonDocument::fromJson(relations).object().value(u"relations"_s).toArray();
        for (const auto &value : list) {
            const auto relation = value.toObject();
            if (relation.value(u"type"_s).toString() == u"wikidata"_s) {
                wikidataId = relation.value(u"url"_s).toObject().value(u"resource"_s).toString().section(u'/', -1);
            }
        }
    }
    QString commonsFile;
    if (!wikidataId.isEmpty()) {
        const auto entity = httpGet(QUrl{u"https://www.wikidata.org/wiki/Special:EntityData/%1.json"_s.arg(wikidataId)}, &ok);
        const auto claims = QJsonDocument::fromJson(entity).object().value(u"entities"_s).toObject().value(wikidataId).toObject().value(u"claims"_s).toObject();
        const auto images = claims.value(u"P18"_s).toArray();
        if (!images.isEmpty()) {
            commonsFile = images.first().toObject().value(u"mainsnak"_s).toObject().value(u"datavalue"_s).toObject().value(u"value"_s).toString();
        }
    }
    if (!commonsFile.isEmpty()) {
        QUrl url{u"https://commons.wikimedia.org/wiki/Special:FilePath/"_s + commonsFile.replace(u' ', u'_')};
        url.setQuery(u"width=500"_s);
        const auto image = httpGet(url, &ok, 30000);
        if (ok && !image.isEmpty()) {
            const auto path = artworkPath(u"artist"_s, key, image);
            QFile file{path};
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(image) == image.size()) {
                file.close();
                storeValue(key, path);
                return path;
            }
        }
    }
    if (ok) {
        storeValue(key, QString{});
    }
    return {};
}

void AlbumLookup::requestArtistPhoto(const QString &artist)
{
    const auto key = u"artist\n"_s + artist.trimmed().toLower();
    if (!startPending(key)) {
        return;
    }
    auto future = QtConcurrent::run([this, artist, key] {
        const auto photo = fetchArtistPhoto(artist);
        endPending(key);
        if (!photo.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, artist, photo] {
                Q_EMIT artistPhotoFound(artist, photo);
            }, Qt::QueuedConnection);
        }
    });
    Q_UNUSED(future)
}
