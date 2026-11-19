/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef UPDATERELEASEINFO_H
#define UPDATERELEASEINFO_H

#include "elisaLib_export.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

/**
 * What the update check needs to know about the newest published build.
 */
struct ELISALIB_EXPORT UpdateReleaseInfo {
    /// Build number of the release (0 when the reply could not be understood)
    int build = 0;

    /// Web page of the release
    QUrl page;

    /// Download of the wanted kind of file (empty when the release has none)
    QUrl assetUrl;

    QString assetName;

    qint64 assetSize = 0;

    /// SHA-256 of the file as lower case hex, when the server gives one
    QByteArray assetSha256;

    [[nodiscard]] bool isValid() const
    {
        return build > 0;
    }

    /**
     * Read the answer of GitHub's "latest release" request.
     *
     * @param json the reply
     * @param assetSuffix ending of the file to look for (".AppImage" or ".flatpak")
     */
    [[nodiscard]] static UpdateReleaseInfo fromGitHubJson(const QByteArray &json, const QString &assetSuffix);

    /// Build number in a release tag such as "album-artist-build-12" (0 if there is none)
    [[nodiscard]] static int buildFromTag(const QString &tag);
};

#endif // UPDATERELEASEINFO_H
