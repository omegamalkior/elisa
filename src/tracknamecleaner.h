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
 */
[[nodiscard]] ELISALIB_EXPORT QString adjustedTitle(const QString &title);

/// The name of the file a song is stored in ("03 - Painkiller.mp3")
[[nodiscard]] ELISALIB_EXPORT QString fileName(const QUrl &file);
}

#endif // TRACKNAMECLEANER_H
