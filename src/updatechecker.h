/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include "elisaLib_export.h"

#include "updatereleaseinfo.h"

#include <QFile>
#include <QObject>
#include <QQmlEngine>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

/**
 * Looks for a newer build of this version of Elisa on GitHub and installs it.
 *
 * An AppImage replaces its own file. A Flatpak asks the host to install the
 * downloaded bundle. Any other kind of install can only open the download page.
 */
class ELISALIB_EXPORT UpdateChecker : public QObject
{
    Q_OBJECT

    QML_ELEMENT

    QML_UNCREATABLE("")

    Q_PROPERTY(State state READ state NOTIFY stateChanged)

    Q_PROPERTY(int currentBuild READ currentBuild CONSTANT)

    Q_PROPERTY(int latestBuild READ latestBuild NOTIFY stateChanged)

    Q_PROPERTY(bool canInstall READ canInstall NOTIFY stateChanged)

    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)

public:
    enum State {
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        Downloading,
        Installing,
        ReadyToRestart,
        Failed,
    };
    Q_ENUM(State)

    enum class InstallKind {
        Other,
        AppImage,
        Flatpak,
    };

    explicit UpdateChecker(QObject *parent = nullptr);

    ~UpdateChecker() override;

    [[nodiscard]] State state() const;

    [[nodiscard]] int currentBuild() const;

    [[nodiscard]] int latestBuild() const;

    /// True when "Update now" can install the new build by itself
    [[nodiscard]] bool canInstall() const;

    /// Download progress from 0 to 1
    [[nodiscard]] double progress() const;

    [[nodiscard]] QString errorText() const;

    /**
     * Ask GitHub for the newest build.
     *
     * @param userRequested true when the user asked: "up to date" and errors are then reported too
     */
    Q_INVOKABLE void check(bool userRequested = false);

    Q_INVOKABLE void installUpdate();

    Q_INVOKABLE void restart();

    Q_INVOKABLE void openReleasePage();

    /// Hide the current message
    Q_INVOKABLE void dismiss();

Q_SIGNALS:

    void stateChanged();

    void progressChanged();

private:
    void setState(State state);

    void fail(const QString &message);

    void checkFinished(QNetworkReply *reply, bool userRequested);

    void downloadFinished(QNetworkReply *reply);

    [[nodiscard]] bool verifyDownload(QString &problem);

    void installAppImage();

    void installFlatpak();

    QNetworkAccessManager *mNetwork = nullptr;

    QFile mDownload;

    QProcess *mInstaller = nullptr;

    UpdateReleaseInfo mRelease;

    QString mErrorText;

    QString mAppImagePath;

    InstallKind mKind = InstallKind::Other;

    State mState = Idle;

    int mCurrentBuild = 0;

    double mProgress = 0;
};

#endif // UPDATECHECKER_H
