/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef IMPORTMENUINSTALLER_H
#define IMPORTMENUINSTALLER_H

#include "elisaLib_export.h"

#include <QString>

/**
 * Adds "Elisa: Import to Music" to the right-click menu of the file manager (Dolphin).
 *
 * The entry runs a small script that moves the selected files and folders into the
 * Music folder and unpacks archives there. Both files live in the user's own
 * ~/.local/share/kio/servicemenus folder.
 */
namespace ImportMenuInstaller
{
/// Folder the menu entry and its script are written to (\a baseDir is for tests)
[[nodiscard]] ELISALIB_EXPORT QString menuFolder(const QString &baseDir = {});

/// Writes the menu entry and the script when missing or outdated. False when it could not be written.
ELISALIB_EXPORT bool install(const QString &baseDir = {});

/// Removes the menu entry and the script again
ELISALIB_EXPORT void uninstall(const QString &baseDir = {});
}

#endif // IMPORTMENUINSTALLER_H
