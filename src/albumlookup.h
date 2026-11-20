/*
   SPDX-FileCopyrightText: 2026 Elisa album-artist fork

   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef ALBUMLOOKUP_H
#define ALBUMLOOKUP_H

#include "elisaLib_export.h"

#include <QObject>
#include <QString>
#include <QUrl>

/**
 * The album of a song that has none in its tags, from MusicBrainz (musicbrainz.org),
 * album covers from the Cover Art Archive and artist photos from Wikimedia Commons.
 *
 * Answers, also "not found", are kept in album-lookups.json in Elisa's data folder,
 * so a song is asked about only once. Requests are at least a second apart, as
 * MusicBrainz asks. Song files are never changed: only Elisa's library gets the album.
 */
class ELISALIB_EXPORT AlbumLookup : public QObject
{
    Q_OBJECT

public:
    static AlbumLookup *instance();

    /// The album already known for this song; \a known is false when it was never asked
    [[nodiscard]] QString cachedAlbum(const QString &artist, const QString &title, bool *known = nullptr);

    /// Asks MusicBrainz now and waits (for worker threads: sorting dropped music)
    QString lookup(const QString &artist, const QString &title, int timeoutMs = 10000);

    /// Asks MusicBrainz in the background; albumFound is emitted for \a file when found
    void requestInBackground(const QString &artist, const QString &title, const QUrl &file);

    /// A downloaded front cover for the album (local file), empty when none (yet)
    [[nodiscard]] QString cachedCover(const QString &artist, const QString &album, bool *known = nullptr);

    /// Downloads the album's front cover from the Cover Art Archive now and waits
    QString fetchCover(const QString &artist, const QString &album);

    /// Downloads the cover in the background; coverFound is emitted for \a file when found
    void requestCover(const QString &artist, const QString &album, const QUrl &file);

    /// A downloaded photo of the artist (local file), empty when none (yet)
    [[nodiscard]] QString cachedArtistPhoto(const QString &artist, bool *known = nullptr);

    /// Downloads a photo of the artist (MusicBrainz -> Wikidata -> Wikimedia Commons) now and waits
    QString fetchArtistPhoto(const QString &artist);

    /// Downloads the artist photo in the background; artistPhotoFound is emitted when found
    void requestArtistPhoto(const QString &artist);

Q_SIGNALS:
    void albumFound(const QUrl &file, const QString &album);
    void coverFound(const QUrl &file, const QString &coverFile);
    void artistPhotoFound(const QString &artist, const QString &photoFile);

private:
    AlbumLookup();
};

#endif // ALBUMLOOKUP_H
