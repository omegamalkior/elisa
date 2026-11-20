/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef BLACKLISTMANAGER_H
#define BLACKLISTMANAGER_H

#include "elisaLib_export.h"

#include <QAbstractListModel>
#include <QList>
#include <QQmlEngine>
#include <QSet>
#include <QString>
#include <QUrl>

class DatabaseInterface;

/**
 * The list of albums and songs the user hid from Elisa (the "blacklist").
 *
 * Hidden files are taken out of the library and kept out when music is scanned.
 * Each entry can be switched off (shown again but kept in the list) or removed.
 * It is also the model shown in the settings.
 */
class ELISALIB_EXPORT BlacklistManager : public QAbstractListModel
{
    Q_OBJECT

    QML_ELEMENT

    QML_UNCREATABLE("")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        IsAlbumRole,
        HiddenRole,
        FileCountRole,
    };

    struct Entry {
        QString title;
        QString artist;
        QList<QUrl> files;
        bool isAlbum = false;
        bool hidden = true;
    };

    /**
     * @param storageFile where the list is saved (empty: the default place in the app's data folder)
     */
    explicit BlacklistManager(const QString &storageFile = {}, QObject *parent = nullptr);

    /// The library to keep in sync. Must be called before music is scanned.
    void setDatabase(DatabaseInterface *database);

    [[nodiscard]] int count() const;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;

    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QSet<QUrl> hiddenFiles() const;

    /// Hide a whole album (all of its songs)
    Q_INVOKABLE void hideAlbum(qulonglong albumId, const QString &title, const QString &artist);

    /// Hide one song
    Q_INVOKABLE void hideTrack(const QUrl &file, const QString &title, const QString &artist);

    /// Switch an entry on (hidden) or off (shown again, but kept in the list)
    Q_INVOKABLE void setHidden(int row, bool hidden);

    /// Take an entry out of the list: its music is shown again
    Q_INVOKABLE void remove(int row);

    /**
     * Delete the song files of an album from the computer. This cannot be undone.
     * @return how many files could not be deleted
     */
    Q_INVOKABLE int deleteAlbumFiles(qulonglong albumId);

    /// Delete one song file from the computer. @return false if it could not be deleted
    Q_INVOKABLE bool deleteTrackFile(const QUrl &file);

    /// Moves every song file of the artist to the trash; returns how many could not be moved
    Q_INVOKABLE int deleteArtistFiles(const QString &artist);

    /// Moves every song file of the album (by name and artist) to the trash; returns failures
    Q_INVOKABLE int deleteAlbumFilesByName(const QString &album, const QString &artist);

Q_SIGNALS:

    void countChanged();

    /// These files should be read again and added back to the library
    void filesToShowAgain(const QList<QUrl> &files);

    /// Song files that were moved to the trash (or removed): the player lets go of them
    void filesDeleted(const QList<QUrl> &files);

private:
    [[nodiscard]] QList<QUrl> albumFiles(qulonglong albumId) const;

    [[nodiscard]] QList<QUrl> artistFiles(const QString &artist, const QString &album) const;

    /// After deleting: album and artist folders left without songs go to the trash too
    void removeFoldersWithoutSongs(const QList<QUrl> &deleted);

    void addEntry(Entry entry);

    void removeFromLibrary(const QList<QUrl> &files);

    int deleteFiles(const QList<QUrl> &files);

    void syncDatabase();

    void load();

    void save() const;

    QList<Entry> mEntries;

    QString mStorageFile;

    DatabaseInterface *mDatabase = nullptr;
};

#endif // BLACKLISTMANAGER_H
