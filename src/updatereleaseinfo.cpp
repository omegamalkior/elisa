/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "updatereleaseinfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using namespace Qt::Literals::StringLiterals;

int UpdateReleaseInfo::buildFromTag(const QString &tag)
{
    static const QRegularExpression buildExpression{u"^album-artist-build-(\\d{1,6})$"_s};

    const auto match = buildExpression.match(tag);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

UpdateReleaseInfo UpdateReleaseInfo::fromGitHubJson(const QByteArray &json, const QString &assetSuffix)
{
    UpdateReleaseInfo result;

    const auto release = QJsonDocument::fromJson(json).object();
    if (release.value("draft"_L1).toBool() || release.value("prerelease"_L1).toBool()) {
        return result;
    }

    result.build = buildFromTag(release.value("tag_name"_L1).toString());
    if (!result.isValid()) {
        return result;
    }

    result.page = QUrl{release.value("html_url"_L1).toString()};

    if (assetSuffix.isEmpty()) {
        return result;
    }

    const auto assets = release.value("assets"_L1).toArray();
    for (const auto &assetValue : assets) {
        const auto asset = assetValue.toObject();
        const auto name = asset.value("name"_L1).toString();
        if (!name.endsWith(assetSuffix)) {
            continue;
        }

        const QUrl url{asset.value("browser_download_url"_L1).toString()};
        // only ever download over https from GitHub
        if (url.scheme() != "https"_L1 || url.host() != "github.com"_L1) {
            continue;
        }

        result.assetUrl = url;
        result.assetName = name;
        result.assetSize = qint64(asset.value("size"_L1).toDouble());

        const auto digest = asset.value("digest"_L1).toString();
        if (digest.startsWith("sha256:"_L1)) {
            result.assetSha256 = digest.mid(7).toLatin1().toLower();
        }
        break;
    }

    return result;
}
