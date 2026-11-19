/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef MUSICORGANIZER_H
#define MUSICORGANIZER_H

#include "elisaLib_export.h"

#include <QString>
#include <QStringList>

#include <functional>

/**
 * Sorts songs inside a music folder into "Artist/Album/song" folders.
 *
 * The artist folder is the album artist of the song (its artist when there is no album
 * artist), so an album with guest artists stays in one folder.
 */
namespace MusicOrganizer
{
struct SongTags {
    bool isSong = false;
    QString artist;
    QString albumArtist;
    QString album;
};

/// Reads the tags of a file; isSong is false for files that are not music
using TagReader = std::function<SongTags(const QString &filePath)>;

struct Result {
    int moved = 0;
    int alreadyInPlace = 0;
    int failed = 0;
};

/// A tag as a folder name: no slashes, no leading/trailing dots or spaces, \a fallback when empty
[[nodiscard]] ELISALIB_EXPORT QString folderName(const QString &tag, const QString &fallback);

/// "<root>/<artist>/<album>" for a song
[[nodiscard]] ELISALIB_EXPORT QString albumFolder(const QString &root, const SongTags &tags);

/**
 * Sorts \a items (files or folders inside \a root; \a root itself for everything).
 *
 * Covers, booklets and other files next to the songs follow them when all songs of
 * their folder went to one album. Folders left empty are removed. Nothing outside
 * \a root is touched.
 */
ELISALIB_EXPORT Result organize(const QString &root, const QStringList &items, const TagReader &readTags);
}

#endif // MUSICORGANIZER_H
