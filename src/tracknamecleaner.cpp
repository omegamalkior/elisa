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

namespace
{
// words that are never part of an artist or a title, at the start or the end of a part
bool isJunkWord(const QString &word)
{
    static const QRegularExpression junk{
        u"^(blah+|free|download|downloaded|mp3|m4a|flac|lyrics?|official|audio|video|hd|hq|4k|\\d{3,4}p|\\d{2,3}kbps|kbps|full|song|"
        u"remastered|www\\..+|.+\\.(com|net|org|ru|to|cc|io|me)|[-–—_.,:;|~=+*#]+)$"_s,
        QRegularExpression::CaseInsensitiveOption};
    return junk.match(word).hasMatch();
}

QString withoutJunkWords(const QString &text)
{
    auto words = text.split(u' ', Qt::SkipEmptyParts);
    while (!words.isEmpty() && isJunkWord(words.first())) {
        words.removeFirst();
    }
    while (!words.isEmpty() && isJunkWord(words.last())) {
        words.removeLast();
    }
    return words.join(u' ');
}

// "Pain (Subtit" -> "Pain": a bracket cut off by a too-short file name
QString withoutOpenBracket(const QString &text)
{
    static const QRegularExpression openBracket{u"\\s*[\\[(][^\\])]*$"_s};
    auto result = text;
    result.remove(openBracket);
    return result.trimmed();
}
}

TrackNameCleaner::GuessedTags TrackNameCleaner::guessFromFileName(const QString &fileName, const QStringList &knownArtists)
{
    static const QRegularExpression extension{u"\\.(mp3|flac|m4a|mp4|aac|ogg|oga|opus|wav|wma|aiff?|ape|mka|webm)$"_s, QRegularExpression::CaseInsensitiveOption};
    static const QRegularExpression separator{u"\\s+[-–—]\\s+"_s};
    static const QRegularExpression spaces{u"\\s{2,}"_s};

    auto name = fileName.trimmed();
    name.remove(extension);
    if (name.count(u'_') >= 2 || (name.contains(u'_') && !name.contains(u' '))) {
        name.replace(u'_', u' ');
    }
    name.replace(spaces, u" "_s);

    const auto match = separator.match(name);
    if (!match.hasMatch()) {
        return {};
    }

    // artist: "01 - " track numbers and junk words go; a known artist inside wins
    auto artist = withoutJunkWords(adjustedTitle(name.left(match.capturedStart())));
    QString bestKnown;
    for (const auto &known : knownArtists) {
        const auto trimmed = known.trimmed();
        if (trimmed.size() <= bestKnown.size() || trimmed.isEmpty()) {
            continue;
        }
        const QRegularExpression whole{u"(^|\\s)"_s + QRegularExpression::escape(trimmed) + u"($|\\s)"_s, QRegularExpression::CaseInsensitiveOption};
        if (whole.match(artist).hasMatch()) {
            bestKnown = trimmed;
        }
    }
    if (!bestKnown.isEmpty()) {
        artist = bestKnown;
    }

    // title: download leftovers, a cut-off bracket and junk words go
    auto title = name.mid(match.capturedEnd());
    for (int pass = 0; pass < 3; ++pass) {
        const auto before = title;
        title = withoutJunkWords(withoutOpenBracket(adjustedTitle(title)));
        if (title == before) {
            break;
        }
    }

    if (artist.isEmpty() || title.isEmpty()) {
        return {};
    }
    return {artist, title};
}
