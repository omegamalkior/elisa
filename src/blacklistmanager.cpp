/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "blacklistmanager.h"

#include "databaseinterface.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

using namespace Qt::Literals::StringLiterals;

BlacklistManager::BlacklistManager(const QString &storageFile, QObject *parent)
    : QAbstractListModel(parent)
    , mStorageFile(storageFile)
{
    if (mStorageFile.isEmpty()) {
        mStorageFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/hidden-music.json"_s;
    }

    load();
}

void BlacklistManager::setDatabase(DatabaseInterface *database)
{
    mDatabase = database;
    syncDatabase();
}

int BlacklistManager::count() const
{
    return int(mEntries.size());
}

int BlacklistManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant BlacklistManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= mEntries.size()) {
        return {};
    }

    const auto &entry = mEntries[index.row()];

    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return entry.title;
    case SubtitleRole: {
        const auto kind = entry.isAlbum ? i18ncp("@info kind of hidden music and its size", "Album, %1 song", "Album, %1 songs", int(entry.files.size()))
                                        : i18nc("@info kind of hidden music", "Song");
        return entry.artist.isEmpty() ? kind : i18nc("@info artist, then kind of hidden music", "%1 · %2", entry.artist, kind);
    }
    case IsAlbumRole:
        return entry.isAlbum;
    case HiddenRole:
        return entry.hidden;
    case FileCountRole:
        return int(entry.files.size());
    }

    return {};
}

QHash<int, QByteArray> BlacklistManager::roleNames() const
{
    return {
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {IsAlbumRole, "isAlbum"},
        {HiddenRole, "hidden"},
        {FileCountRole, "fileCount"},
    };
}

QSet<QUrl> BlacklistManager::hiddenFiles() const
{
    QSet<QUrl> result;

    for (const auto &entry : mEntries) {
        if (!entry.hidden) {
            continue;
        }
        for (const auto &file : entry.files) {
            result.insert(file);
        }
    }

    return result;
}

void BlacklistManager::hideAlbum(qulonglong albumId, const QString &title, const QString &artist)
{
    const auto files = albumFiles(albumId);
    if (files.isEmpty()) {
        return;
    }

    addEntry({title, artist, files, true, true});
}

void BlacklistManager::hideTrack(const QUrl &file, const QString &title, const QString &artist)
{
    if (!file.isValid()) {
        return;
    }

    addEntry({title, artist, {file}, false, true});
}

void BlacklistManager::setHidden(int row, bool hidden)
{
    if (row < 0 || row >= mEntries.size() || mEntries[row].hidden == hidden) {
        return;
    }

    mEntries[row].hidden = hidden;
    Q_EMIT dataChanged(index(row), index(row), {HiddenRole});
    save();
    syncDatabase();

    if (hidden) {
        removeFromLibrary(mEntries[row].files);
    } else {
        // a file can also be in another entry that still hides it
        const auto stillHidden = hiddenFiles();
        QList<QUrl> toShow;
        for (const auto &file : std::as_const(mEntries[row].files)) {
            if (!stillHidden.contains(file)) {
                toShow.push_back(file);
            }
        }
        if (!toShow.isEmpty()) {
            Q_EMIT filesToShowAgain(toShow);
        }
    }
}

void BlacklistManager::remove(int row)
{
    if (row < 0 || row >= mEntries.size()) {
        return;
    }

    // show its music again first
    setHidden(row, false);

    beginRemoveRows({}, row, row);
    mEntries.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    save();
}

int BlacklistManager::deleteAlbumFiles(qulonglong albumId)
{
    return deleteFiles(albumFiles(albumId));
}

bool BlacklistManager::deleteTrackFile(const QUrl &file)
{
    return deleteFiles({file}) == 0;
}

int BlacklistManager::deleteArtistFiles(const QString &artist)
{
    return deleteFiles(artistFiles(artist, {}));
}

int BlacklistManager::deleteAlbumFilesByName(const QString &album, const QString &artist)
{
    if (album.trimmed().isEmpty()) {
        return 0;
    }
    return deleteFiles(artistFiles(artist, album));
}

// the song files of an artist (as artist or album artist), only of \a album when given
QList<QUrl> BlacklistManager::artistFiles(const QString &artist, const QString &album) const
{
    QList<QUrl> result;

    if (!mDatabase || artist.trimmed().isEmpty()) {
        return result;
    }

    DataTypes::ListTrackDataType tracks;
    const auto readArtist = [this, artist, &tracks] {
        tracks = mDatabase->tracksDataFromAuthor(artist);
    };

    // the library lives in its own thread
    if (mDatabase->thread() == QThread::currentThread()) {
        readArtist();
    } else {
        QMetaObject::invokeMethod(mDatabase, readArtist, Qt::BlockingQueuedConnection);
    }

    for (const auto &track : std::as_const(tracks)) {
        if (!album.isEmpty() && track.album().compare(album, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const auto file = track.resourceURI();
        if (file.isValid() && !result.contains(file)) {
            result.push_back(file);
        }
    }

    return result;
}

QList<QUrl> BlacklistManager::albumFiles(qulonglong albumId) const
{
    QList<QUrl> result;

    if (!mDatabase || albumId == 0) {
        return result;
    }

    DataTypes::ListTrackDataType tracks;
    const auto readAlbum = [this, albumId, &tracks] {
        tracks = mDatabase->albumData(albumId);
    };

    // the library lives in its own thread
    if (mDatabase->thread() == QThread::currentThread()) {
        readAlbum();
    } else {
        QMetaObject::invokeMethod(mDatabase, readAlbum, Qt::BlockingQueuedConnection);
    }

    for (const auto &track : std::as_const(tracks)) {
        const auto file = track.resourceURI();
        if (file.isValid() && !result.contains(file)) {
            result.push_back(file);
        }
    }

    return result;
}

void BlacklistManager::addEntry(Entry entry)
{
    // hiding the same thing twice keeps one entry
    for (int row = 0; row < mEntries.size(); ++row) {
        if (mEntries[row].isAlbum == entry.isAlbum && mEntries[row].files == entry.files) {
            setHidden(row, true);
            return;
        }
    }

    const auto files = entry.files;

    beginInsertRows({}, count(), count());
    mEntries.push_back(std::move(entry));
    endInsertRows();
    Q_EMIT countChanged();

    save();
    syncDatabase();
    removeFromLibrary(files);
}

void BlacklistManager::removeFromLibrary(const QList<QUrl> &files)
{
    if (!mDatabase || files.isEmpty()) {
        return;
    }

    QMetaObject::invokeMethod(mDatabase, [database = mDatabase, files] {
        database->removeTracksList(files);
    });
}

int BlacklistManager::deleteFiles(const QList<QUrl> &files)
{
    QList<QUrl> deleted;
    auto failures = 0;

    for (const auto &file : files) {
        if (!file.isLocalFile()) {
            ++failures;
            continue;
        }

        const auto path = file.toLocalFile();
        // to the trash, so a mistake can be undone; removed for good only where there is no trash
        if (!QFileInfo::exists(path) || QFile::moveToTrash(path) || QFile::remove(path)) {
            deleted.push_back(file);
        } else {
            ++failures;
        }
    }

    if (!deleted.isEmpty()) {
        Q_EMIT filesDeleted(deleted);
    }
    removeFromLibrary(deleted);

    return failures;
}

void BlacklistManager::syncDatabase()
{
    if (mDatabase) {
        mDatabase->setHiddenFiles(hiddenFiles());
    }
}

void BlacklistManager::load()
{
    QFile file{mStorageFile};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const auto entries = QJsonDocument::fromJson(file.readAll()).object().value("entries"_L1).toArray();
    for (const auto &value : entries) {
        const auto object = value.toObject();

        Entry entry;
        entry.title = object.value("title"_L1).toString();
        entry.artist = object.value("artist"_L1).toString();
        entry.isAlbum = object.value("album"_L1).toBool();
        entry.hidden = object.value("hidden"_L1).toBool(true);
        const auto files = object.value("files"_L1).toArray();
        for (const auto &fileValue : files) {
            const QUrl url{fileValue.toString()};
            if (url.isValid()) {
                entry.files.push_back(url);
            }
        }

        if (!entry.files.isEmpty()) {
            mEntries.push_back(std::move(entry));
        }
    }
}

void BlacklistManager::save() const
{
    QJsonArray entries;
    for (const auto &entry : mEntries) {
        QJsonArray files;
        for (const auto &file : entry.files) {
            files.push_back(file.toString());
        }
        entries.push_back(QJsonObject{
            {"title"_L1, entry.title},
            {"artist"_L1, entry.artist},
            {"album"_L1, entry.isAlbum},
            {"hidden"_L1, entry.hidden},
            {"files"_L1, files},
        });
    }

    QDir{}.mkpath(QFileInfo{mStorageFile}.absolutePath());

    QSaveFile file{mStorageFile};
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument{QJsonObject{{"entries"_L1, entries}}}.toJson());
    file.commit();
}

#include "moc_blacklistmanager.cpp"
