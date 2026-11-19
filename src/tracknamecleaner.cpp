/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "tracknamecleaner.h"

#include <QRegularExpression>

using namespace Qt::Literals::StringLiterals;

QString TrackNameCleaner::adjustedTitle(const QString &title, int trackNumber)
{
    static const QRegularExpression extension{u"\\.(mp3|flac|m4a|mp4|aac|ogg|oga|opus|wav|wma|aiff?|ape|mka|webm)$"_s, QRegularExpression::CaseInsensitiveOption};
    // a track number in front of the name:
    //   "01 - ", "1. ", "03_", "2 – "     number, then a separator
    //   "03 Name"                         number with a leading zero, then a space
    //   "1-07 Name"                       disc-track, then a space
    //   "(04) Name", "[04] Name"          number in brackets
    static const QRegularExpression leadingNumber{
        u"^\\s*(?:"
        u"\\d{1,2}-\\d{1,3}\\s+(?=\\D)"
        u"|(?:\\d{1,2}[-.])?\\d{1,3}(?:\\s*[-–—._)]\\s*|\\s+[-–—]\\s+)"
        u"|0\\d{1,2}\\s+"
        u"|[\\[(]\\d{1,3}[\\])]\\s*"
        u")"_s};
    // "[dQw4w9WgXcQ]" or "-dQw4w9WgXcQ" at the end: a video id left by a downloader
    static const QRegularExpression videoId{u"\\s*(\\[[A-Za-z0-9_-]{11}\\]|\\([A-Za-z0-9_-]{11}\\))\\s*$"_s};
    // "(Official Video)", "[Lyrics]", "(Audio)", "(HD)", "(1080p)" ... at the end
    static const QRegularExpression leftover{
        u"\\s*[\\[(](official\\s+)?(music\\s+|lyric\\s+|lyrics\\s+)?(video|audio|visuali[sz]er|lyrics?|hd|hq|4k|\\d{3,4}p|\\d{2,3}\\s?kbps|full\\s+song|free\\s+download)[\\])]\\s*$"_s,
        QRegularExpression::CaseInsensitiveOption};
    static const QRegularExpression spaces{u"\\s{2,}"_s};

    auto result = title.trimmed();

    result.remove(extension);

    // underscores stand for spaces in file-like names ("Three_Days_Grace_-_Pain")
    if (result.count(u'_') >= 2 || (result.contains(u'_') && !result.contains(u' '))) {
        result.replace(u'_', u' ');
    }

    // drop the number only when a real name follows: "1979", "99 Luftballons",
    // "50 - 50" and "1-800-273-8255" stay as they are
    const auto numberMatch = leadingNumber.match(result);
    if (numberMatch.hasMatch()) {
        const auto rest = result.mid(numberMatch.capturedEnd());
        if (!rest.isEmpty() && !rest.at(0).isDigit() && !rest.at(0).isSpace()) {
            result = rest;
        }
    } else if (trackNumber > 0) {
        // "12 The Real You" for the song tagged as track 12
        const QRegularExpression ownNumber{u"^\\s*0*%1\\s+(?=\\D)"_s.arg(trackNumber)};
        const auto ownMatch = ownNumber.match(result);
        if (ownMatch.hasMatch() && result.size() > ownMatch.capturedEnd()) {
            result = result.mid(ownMatch.capturedEnd());
        }
    }

    // leftovers can be stacked: "Song (Official Video) [HD] [dQw4w9WgXcQ]"
    for (int pass = 0; pass < 6; ++pass) {
        const auto before = result;
        result.remove(videoId);
        result.remove(leftover);
        if (result == before) {
            break;
        }
    }

    result.replace(spaces, u" "_s);
    result = result.trimmed();

    return result.isEmpty() ? title : result;
}

QString TrackNameCleaner::fileName(const QUrl &file)
{
    return file.fileName();
}
