/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef TRACKNAMECLEANER_H
#define TRACKNAMECLEANER_H

#include "elisaLib_export.h"

#include <QString>
#include <QUrl>

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
}

#endif // TRACKNAMECLEANER_H
