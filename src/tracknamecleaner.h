/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef TRACKNAMECLEANER_H
#define TRACKNAMECLEANER_H

#include "elisaLib_export.h"

#include <QString>
#include <QUrl>
#include <QStringList>

/**
 * Turns raw song titles and file names into the tidy names a music player shows.
 */
namespace TrackNameCleaner
{
/**
 * "03 - Three_Days_Grace - Painkiller (Official Video) [dQw4w9WgXcQ].mp3" -> "Three Days Grace - Painkiller"
 *
 * Removes a leading track number, a file extension, underscores and the usual
 * download leftovers. A title that would end up empty is returned unchanged.
 *
 * When the song's track number is known, a title starting with exactly that number
 * loses it too ("12 The Real You" as track 12 -> "The Real You"), while "99 Luftballons"
 * as track 3 stays as it is.
 */
[[nodiscard]] ELISALIB_EXPORT QString adjustedTitle(const QString &title, int trackNumber = 0);

/// The name of the file a song is stored in ("03 - Painkiller.mp3")
[[nodiscard]] ELISALIB_EXPORT QString fileName(const QUrl &file);

/// Artist and title read from a file name like "Three Days Grace - Pain.mp3"
struct GuessedTags {
    QString artist;
    QString title;
};

/**
 * For songs without tags: the artist and title in a file name "Artist - Title",
 * without junk words ("blah", "free download", "mp3"...), site names, a cut-off
 * "(Subtit" and the leftovers adjustedTitle removes. When \a knownArtists holds an
 * artist named inside the artist part ("blah Three Days Grace"), that name is used.
 * Both are empty when the name has no "Artist - Title" form. The file is not touched.
 */
[[nodiscard]] ELISALIB_EXPORT GuessedTags guessFromFileName(const QString &fileName, const QStringList &knownArtists = {});
}

#endif // TRACKNAMECLEANER_H
