/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "updatechecker.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include <filesystem>
#include <system_error>

#ifndef ELISA_FORK_BUILD_NUMBER
#define ELISA_FORK_BUILD_NUMBER 0
#endif

using namespace Qt::Literals::StringLiterals;

namespace
{
const auto latestReleaseUrl = "https://api.github.com/repos/omegamalkior/elisa/releases/latest"_L1;
const auto releasesPageUrl = "https://github.com/omegamalkior/elisa/releases/latest"_L1;
const auto flatpakInfoFile = "/.flatpak-info"_L1;
const auto flatpakAppId = "org.kde.elisa"_L1;

QNetworkRequest gitHubRequest(const QUrl &url)
{
    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, u"Elisa-album-artist-updater"_s);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    return request;
}
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , mNetwork(new QNetworkAccessManager(this))
    , mCurrentBuild(ELISA_FORK_BUILD_NUMBER)
{
    // lets a test pretend to be an older build
    if (qEnvironmentVariableIsSet("ELISA_FORK_BUILD_NUMBER_OVERRIDE")) {
        mCurrentBuild = qEnvironmentVariableIntValue("ELISA_FORK_BUILD_NUMBER_OVERRIDE");
    }

    if (QFile::exists(flatpakInfoFile)) {
        mKind = InstallKind::Flatpak;
    } else {
        const auto appImage = qEnvironmentVariable("APPIMAGE");
        const QFileInfo appImageInfo{appImage};
        if (!appImage.isEmpty() && appImageInfo.isFile() && QFileInfo{appImageInfo.absolutePath()}.isWritable()) {
            mKind = InstallKind::AppImage;
            mAppImagePath = appImageInfo.absoluteFilePath();
        }
    }
}

UpdateChecker::~UpdateChecker() = default;

UpdateChecker::State UpdateChecker::state() const
{
    return mState;
}

int UpdateChecker::currentBuild() const
{
    return mCurrentBuild;
}

int UpdateChecker::latestBuild() const
{
    return mRelease.build;
}

bool UpdateChecker::canInstall() const
{
    return mKind != InstallKind::Other && mRelease.assetUrl.isValid();
}

double UpdateChecker::progress() const
{
    return mProgress;
}

QString UpdateChecker::errorText() const
{
    return mErrorText;
}

void UpdateChecker::check(bool userRequested)
{
    if (mState == Checking || mState == Downloading || mState == Installing || mState == ReadyToRestart) {
        return;
    }

    if (mCurrentBuild <= 0 && !userRequested) {
        // not one of the published builds: nothing to compare with
        return;
    }

    setState(Checking);

    auto request = gitHubRequest(QUrl{latestReleaseUrl});
    request.setRawHeader("Accept", "application/vnd.github+json");
    // optional: a GitHub token for machines that share their address with many others
    // (GitHub answers only 60 anonymous questions per hour and address)
    const auto token = qgetenv("ELISA_UPDATE_GITHUB_TOKEN");
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + token);
    }

    auto *reply = mNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, userRequested] {
        checkFinished(reply, userRequested);
    });
}

void UpdateChecker::checkFinished(QNetworkReply *reply, bool userRequested)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qInfo().noquote() << "Elisa update check failed:" << reply->errorString();
        if (userRequested) {
            fail(i18nc("@info", "Could not check for updates: %1", reply->errorString()));
        } else {
            // no network at startup is not worth a message
            setState(Idle);
        }
        return;
    }

    const auto suffix = mKind == InstallKind::AppImage ? u".AppImage"_s : (mKind == InstallKind::Flatpak ? u".flatpak"_s : QString{});
    mRelease = UpdateReleaseInfo::fromGitHubJson(reply->readAll(), suffix);

    if (!mRelease.isValid()) {
        if (userRequested) {
            fail(i18nc("@info", "Could not check for updates: the answer from GitHub was not understood."));
        } else {
            setState(Idle);
        }
        return;
    }

    qInfo() << "Elisa update check: this is build" << mCurrentBuild << "- latest published build is" << mRelease.build
            << "- can install by itself:" << canInstall();

    if (mRelease.build > mCurrentBuild) {
        setState(UpdateAvailable);
    } else {
        setState(userRequested ? UpToDate : Idle);
    }
}

void UpdateChecker::installUpdate()
{
    if (mState != UpdateAvailable && mState != Failed) {
        return;
    }

    if (!canInstall()) {
        openReleasePage();
        return;
    }

    QString target;
    if (mKind == InstallKind::AppImage) {
        target = mAppImagePath + u".update-part"_s;
    } else {
        const auto cacheFolder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir{}.mkpath(cacheFolder);
        target = cacheFolder + u"/elisa-update.flatpak"_s;
    }

    mDownload.setFileName(target);
    if (!mDownload.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(i18nc("@info", "Could not save the update to %1: %2", target, mDownload.errorString()));
        return;
    }

    mProgress = 0;
    Q_EMIT progressChanged();
    setState(Downloading);

    auto *reply = mNetwork->get(gitHubRequest(mRelease.assetUrl));
    // a big file: only stop when nothing arrives for a while
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        mDownload.write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const auto size = total > 0 ? total : mRelease.assetSize;
        mProgress = size > 0 ? double(received) / double(size) : 0;
        Q_EMIT progressChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        downloadFinished(reply);
    });
}

void UpdateChecker::downloadFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    mDownload.write(reply->readAll());
    mDownload.close();

    if (reply->error() != QNetworkReply::NoError) {
        mDownload.remove();
        fail(i18nc("@info", "The download failed: %1", reply->errorString()));
        return;
    }

    QString problem;
    if (!verifyDownload(problem)) {
        mDownload.remove();
        fail(problem);
        return;
    }

    setState(Installing);

    if (mKind == InstallKind::AppImage) {
        installAppImage();
    } else {
        installFlatpak();
    }
}

bool UpdateChecker::verifyDownload(QString &problem)
{
    if (mRelease.assetSize > 0 && mDownload.size() != mRelease.assetSize) {
        problem = i18nc("@info", "The downloaded update is incomplete.");
        return false;
    }

    if (!mDownload.open(QIODevice::ReadOnly)) {
        problem = i18nc("@info", "The downloaded update could not be read.");
        return false;
    }

    const auto start = mDownload.peek(8);
    QCryptographicHash hash{QCryptographicHash::Sha256};
    const auto hashed = hash.addData(&mDownload);
    mDownload.close();

    if (!mRelease.assetSha256.isEmpty() && (!hashed || hash.result().toHex() != mRelease.assetSha256)) {
        problem = i18nc("@info", "The downloaded update is damaged (its checksum does not match).");
        return false;
    }

    const auto looksRight = mKind == InstallKind::AppImage ? start.startsWith("\x7f" "ELF") : start.startsWith("flatpak");
    if (!looksRight) {
        problem = i18nc("@info", "The downloaded file is not an Elisa update.");
        return false;
    }

    return true;
}

void UpdateChecker::installAppImage()
{
    const auto permissions = QFile::permissions(mAppImagePath) | QFileDevice::ExeOwner | QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    mDownload.setPermissions(permissions);

    // replaces the file in one step: the running copy keeps using the old one
    std::error_code error;
    std::filesystem::rename(mDownload.fileName().toStdString(), mAppImagePath.toStdString(), error);
    if (error) {
        mDownload.remove();
        fail(i18nc("@info", "Could not replace %1: %2", mAppImagePath, QString::fromStdString(error.message())));
        return;
    }

    setState(ReadyToRestart);
}

void UpdateChecker::installFlatpak()
{
    // a Flatpak cannot install anything itself: ask the host to install the bundle
    QSettings flatpakInfo{flatpakInfoFile, QSettings::IniFormat};
    const auto appPath = flatpakInfo.value("Instance/app-path"_L1).toString();
    const auto scope = appPath.startsWith("/var/"_L1) ? u"--system"_s : u"--user"_s;

    delete mInstaller;
    mInstaller = new QProcess(this);
    mInstaller->setProcessChannelMode(QProcess::MergedChannels);

    connect(mInstaller, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && mState == Installing) {
            fail(i18nc("@info", "Could not start the installer. Download the update from the web page instead."));
        }
    });
    connect(mInstaller, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (mState != Installing) {
            return;
        }
        if (status == QProcess::NormalExit && exitCode == 0) {
            mDownload.remove();
            setState(ReadyToRestart);
        } else {
            const auto output = QString::fromLocal8Bit(mInstaller->readAll()).trimmed().right(300);
            fail(i18nc("@info", "The update could not be installed: %1", output.isEmpty() ? i18nc("@info", "unknown error") : output));
        }
    });

    mInstaller->start(u"flatpak-spawn"_s,
                      {u"--host"_s, u"flatpak"_s, u"install"_s, scope, u"--assumeyes"_s, u"--noninteractive"_s, u"--reinstall"_s, u"--bundle"_s, mDownload.fileName()});
}

void UpdateChecker::restart()
{
    if (mState != ReadyToRestart) {
        return;
    }

    // start again a moment later, once this copy has closed (only one Elisa runs at a time)
    if (mKind == InstallKind::AppImage) {
        QProcess::startDetached(u"/bin/sh"_s, {u"-c"_s, u"sleep 2; exec \"$0\""_s, mAppImagePath});
    } else if (mKind == InstallKind::Flatpak) {
        QProcess::startDetached(u"flatpak-spawn"_s, {u"--host"_s, u"sh"_s, u"-c"_s, u"sleep 2; exec flatpak run "_s + flatpakAppId});
    }

    QCoreApplication::quit();
}

void UpdateChecker::openReleasePage()
{
    QDesktopServices::openUrl(mRelease.page.isValid() ? mRelease.page : QUrl{releasesPageUrl});
}

void UpdateChecker::dismiss()
{
    if (mState == UpToDate || mState == UpdateAvailable || mState == Failed) {
        setState(Idle);
    }
}

void UpdateChecker::setState(State state)
{
    if (state != Failed) {
        mErrorText.clear();
    }

    mState = state;
    Q_EMIT stateChanged();
}

void UpdateChecker::fail(const QString &message)
{
    mErrorText = message;
    setState(Failed);
}

#include "moc_updatechecker.cpp"
