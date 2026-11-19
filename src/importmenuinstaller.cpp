/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "importmenuinstaller.h"

#include <QDir>
#include <QFile>

using namespace Qt::Literals::StringLiterals;

namespace
{
const auto scriptName = u"elisa-import-to-music.sh"_s;
const auto entryName = u"elisa-import-to-music.desktop"_s;
constexpr auto executable = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther;

// writes only when the content differs, and makes the file executable
bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file{path};
    if (file.open(QIODevice::ReadOnly)) {
        const auto same = file.readAll() == content;
        file.close();
        if (same) {
            return file.permissions().testFlag(QFile::ExeOwner) || file.setPermissions(executable);
        }
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const auto written = file.write(content) == content.size();
    file.close();
    return written && file.setPermissions(executable);
}

QByteArray menuEntry(const QString &scriptPath)
{
    auto quoted = scriptPath;
    quoted.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s);

    return QByteArrayLiteral("[Desktop Entry]\n"
                             "Type=Service\n"
                             "MimeType=audio/*;inode/directory;application/zip;application/x-7z-compressed;application/vnd.rar;"
                             "application/x-tar;application/x-compressed-tar;application/x-bzip-compressed-tar;"
                             "application/x-bzip2-compressed-tar;application/x-xz-compressed-tar;application/x-zstd-compressed-tar;\n"
                             "Actions=importToMusic\n"
                             "X-KDE-Priority=TopLevel\n"
                             "\n"
                             "[Desktop Action importToMusic]\n"
                             "Name=Elisa: Import to Music\n"
                             "Icon=folder-music\n"
                             "Exec=\"")
        + quoted.toUtf8() + QByteArrayLiteral("\" %F\n");
}
}

QString ImportMenuInstaller::menuFolder(const QString &baseDir)
{
    // the real home folder also inside a Flatpak, where the usual data folder is private to the app
    const auto base = baseDir.isEmpty() ? QDir::homePath() + u"/.local/share"_s : baseDir;
    return base + u"/kio/servicemenus"_s;
}

bool ImportMenuInstaller::install(const QString &baseDir)
{
    const auto folder = menuFolder(baseDir);
    if (!QDir{}.mkpath(folder)) {
        return false;
    }

    QFile script{u":/elisa/elisa-import-to-music.sh"_s};
    if (!script.open(QIODevice::ReadOnly)) {
        return false;
    }

    const auto scriptPath = folder + u'/' + scriptName;
    return writeFile(scriptPath, script.readAll()) && writeFile(folder + u'/' + entryName, menuEntry(scriptPath));
}

void ImportMenuInstaller::uninstall(const QString &baseDir)
{
    const auto folder = menuFolder(baseDir);
    QFile::remove(folder + u'/' + entryName);
    QFile::remove(folder + u'/' + scriptName);
}
