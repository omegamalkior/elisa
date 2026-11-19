/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "musicorganizer.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

using namespace Qt::Literals::StringLiterals;

namespace
{
// a free path: "Name.mp3", "Name (2).mp3", "Name (3).mp3"...
QString freePath(const QString &folder, const QString &fileName)
{
    QString candidate = folder + u'/' + fileName;
    if (!QFileInfo::exists(candidate)) {
        return candidate;
    }
    const QFileInfo info{fileName};
    const auto stem = info.completeBaseName().isEmpty() ? fileName : info.completeBaseName();
    const auto extension = info.completeBaseName().isEmpty() || info.suffix().isEmpty() ? QString{} : u'.' + info.suffix();
    for (int number = 2;; ++number) {
        candidate = folder + u'/' + stem + u" (%1)"_s.arg(number) + extension;
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

bool isInside(const QString &path, const QString &root)
{
    return path.startsWith(root + u'/');
}

// removes folders that became empty, from the deepest one up, never the root
void removeEmptyFolders(QSet<QString> folders, const QString &root)
{
    auto sorted = folders.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString &left, const QString &right) {
        return left.size() > right.size();
    });
    for (auto folder : std::as_const(sorted)) {
        while (isInside(folder, root) && QDir{folder}.isEmpty() && QDir{}.rmdir(folder)) {
            folder = QFileInfo{folder}.absolutePath();
        }
    }
}
}

QString MusicOrganizer::folderName(const QString &tag, const QString &fallback)
{
    QString name;
    name.reserve(tag.size());
    for (const auto character : tag) {
        if (character == u'/' || character == u'\\' || character == u':') {
            name += u'-';
        } else if (character.unicode() < 0x20 || character == u'*' || character == u'?' || character == u'"' || character == u'<'
                   || character == u'>' || character == u'|') {
            continue;
        } else {
            name += character;
        }
    }
    name = name.simplified();
    while (name.startsWith(u'.')) {
        name.remove(0, 1);
    }
    while (name.endsWith(u'.') || name.endsWith(u' ')) {
        name.chop(1);
    }
    name = name.left(120).trimmed();
    return name.isEmpty() ? fallback : name;
}

QString MusicOrganizer::albumFolder(const QString &root, const SongTags &tags)
{
    const auto artist = folderName(tags.albumArtist.isEmpty() ? tags.artist : tags.albumArtist, u"Unknown Artist"_s);
    const auto album = folderName(tags.album, u"Unknown Album"_s);
    return root + u'/' + artist + u'/' + album;
}

MusicOrganizer::Result MusicOrganizer::organize(const QString &rootPath, const QStringList &items, const TagReader &readTags)
{
    Result result;

    const auto root = QDir::cleanPath(QFileInfo{rootPath}.absoluteFilePath());
    if (root.isEmpty() || root == u"/"_s || !QFileInfo{root}.isDir()) {
        return result;
    }

    // every file to look at, grouped by the folder it is in
    QHash<QString, QStringList> filesOfFolder;
    const auto addFile = [&](const QString &file) {
        const QFileInfo info{file};
        if (info.fileName().startsWith(u'.') || info.isSymLink()) {
            return;
        }
        filesOfFolder[info.absolutePath()].push_back(info.absoluteFilePath());
    };
    for (const auto &item : items) {
        const auto path = QDir::cleanPath(QFileInfo{item}.absoluteFilePath());
        const QFileInfo info{path};
        if (info.isDir() && (path == root || isInside(path, root))) {
            QDirIterator files{path, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories};
            while (files.hasNext()) {
                addFile(files.next());
            }
        } else if (info.isFile() && isInside(path, root)) {
            addFile(path);
        }
    }

    QSet<QString> touchedFolders;

    for (auto folder = filesOfFolder.cbegin(); folder != filesOfFolder.cend(); ++folder) {
        QSet<QString> targets;
        QStringList others;
        bool songStayed = false;

        for (const auto &file : folder.value()) {
            const auto tags = readTags(file);
            if (!tags.isSong) {
                others.push_back(file);
                continue;
            }

            // no artist and no album: nothing to sort by, the song stays where it is
            if (tags.artist.isEmpty() && tags.albumArtist.isEmpty() && tags.album.isEmpty()) {
                ++result.alreadyInPlace;
                songStayed = true;
                continue;
            }

            const auto target = albumFolder(root, tags);
            targets.insert(target);
            if (target == folder.key()) {
                ++result.alreadyInPlace;
                songStayed = true;
                continue;
            }
            if (!QDir{}.mkpath(target) || !QFile::rename(file, freePath(target, QFileInfo{file}.fileName()))) {
                ++result.failed;
                songStayed = true;
                continue;
            }
            ++result.moved;
            touchedFolders.insert(folder.key());
        }

        // covers and booklets follow the songs when the whole folder was one album
        if (targets.size() == 1 && !songStayed && folder.key() != root) {
            const auto target = *targets.cbegin();
            for (const auto &file : std::as_const(others)) {
                QFile::rename(file, freePath(target, QFileInfo{file}.fileName()));
            }
        }
    }

    removeEmptyFolders(touchedFolders, root);

    return result;
}
