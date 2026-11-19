/*
   SPDX-FileCopyrightText: 2017 (c) Matthieu Gallien <matthieu_gallien@yahoo.fr>

   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef ELISAAPPLICATION_H
#define ELISAAPPLICATION_H

#include "elisaLib_export.h"

#include "config-upnp-qt.h"

#include "elisautils.h"
#include "datatypes.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <memory>

class QIcon;
class QAction;
class MusicListenersManager;
class MediaPlayList;
class MediaPlayListProxyModel;
class AudioWrapper;
class ManageAudioPlayer;
class SleepTimer;
class BlacklistManager;
class UpdateChecker;
class ManageMediaPlayerControl;
class ManageHeaderBar;
class ElisaApplicationPrivate;
class KColorSchemeManager;
class QAbstractItemModel;

class ELISALIB_EXPORT ElisaApplication : public QObject
{

    Q_OBJECT

    QML_ELEMENT

    QML_SINGLETON

    Q_PROPERTY(ElisaUtils::PlayListEntryType embeddedView
               READ embeddedView
               NOTIFY embeddedViewChanged)

    Q_PROPERTY(MusicListenersManager *musicManager
               READ musicManager
               NOTIFY musicManagerChanged)

    Q_PROPERTY(MediaPlayList *mediaPlayList
               READ mediaPlayList
               NOTIFY mediaPlayListChanged)


    Q_PROPERTY(MediaPlayListProxyModel *mediaPlayListProxyModel
               READ mediaPlayListProxyModel
               NOTIFY mediaPlayListProxyModelChanged)


    Q_PROPERTY(AudioWrapper *audioPlayer
               READ audioPlayer
               NOTIFY audioPlayerChanged)

    Q_PROPERTY(ManageAudioPlayer *audioControl
               READ audioControl
               NOTIFY audioControlChanged)

    Q_PROPERTY(SleepTimer *sleepTimer
               READ sleepTimer
               CONSTANT)

    Q_PROPERTY(BlacklistManager *blacklist
               READ blacklist
               CONSTANT)

    Q_PROPERTY(UpdateChecker *updateChecker
               READ updateChecker
               CONSTANT)

    Q_PROPERTY(bool importMenuEnabled
               READ importMenuEnabled
               WRITE setImportMenuEnabled
               NOTIFY importMenuEnabledChanged)

    Q_PROPERTY(bool showFileNames
               READ showFileNames
               WRITE setShowFileNames
               NOTIFY showFileNamesChanged)

    Q_PROPERTY(ManageMediaPlayerControl *playerControl
               READ playerControl
               NOTIFY playerControlChanged)

    Q_PROPERTY(ManageHeaderBar *manageHeaderBar
               READ manageHeaderBar
               NOTIFY manageHeaderBarChanged)

    Q_PROPERTY(bool showNowPlayingBackground
               READ showNowPlayingBackground
               NOTIFY showNowPlayingBackgroundChanged)

    Q_PROPERTY(bool showProgressOnTaskBar
               READ showProgressOnTaskBar
               NOTIFY showProgressOnTaskBarChanged)

    Q_PROPERTY(bool showSystemTrayIcon
               READ showSystemTrayIcon
               NOTIFY showSystemTrayIconChanged)

    Q_PROPERTY(bool useFavoriteStyleRatings
               READ useFavoriteStyleRatings
               NOTIFY useFavoriteStyleRatingsChanged)

    Q_PROPERTY(bool useAlbumArtistInArtistsView
               READ useAlbumArtistInArtistsView
               NOTIFY useAlbumArtistInArtistsViewChanged)

    Q_PROPERTY(QAbstractItemModel* colorSchemesModel
               READ colorSchemesModel
               CONSTANT)

    Q_PROPERTY(QString activeColorSchemeName
               READ activeColorSchemeName
               WRITE setActiveColorSchemeName
               NOTIFY activeColorSchemeNameChanged)

    Q_PROPERTY(bool darkMode
               READ darkMode
               WRITE setDarkMode
               NOTIFY darkModeChanged)

    Q_PROPERTY(int initialViewIndex
               READ initialViewIndex
               NOTIFY initialViewIndexChanged)

    Q_PROPERTY(QString initialFilesViewPath
               READ initialFilesViewPath
               NOTIFY initialFilesViewPathChanged)

public:

    explicit ElisaApplication(QObject *parent = nullptr);

    ~ElisaApplication() override;

    Q_INVOKABLE QAction* action(const QString& name);

    Q_INVOKABLE void showInFolder(const QUrl &filePath);

    [[nodiscard]] MusicListenersManager *musicManager() const;

    [[nodiscard]] MediaPlayList *mediaPlayList() const;

    [[nodiscard]] MediaPlayListProxyModel *mediaPlayListProxyModel() const;

    [[nodiscard]] AudioWrapper *audioPlayer() const;

    [[nodiscard]] ManageAudioPlayer *audioControl() const;

    [[nodiscard]] SleepTimer *sleepTimer() const;

    [[nodiscard]] BlacklistManager *blacklist() const;

    [[nodiscard]] UpdateChecker *updateChecker() const;

    /// Song lists show file names (true) or adjusted, cleaned up names (false, the default)
    [[nodiscard]] bool showFileNames() const;
    void setShowFileNames(bool value);

    /// "Elisa: Import to Music" in the file manager's right-click menu
    [[nodiscard]] bool importMenuEnabled() const;
    void setImportMenuEnabled(bool value);

    /// A song title without track number, file extension and download leftovers
    Q_INVOKABLE QString adjustedTitle(const QString &title, int trackNumber = 0) const;

    /// The name of the file a song is stored in
    Q_INVOKABLE QString fileNameOf(const QUrl &file) const;

    [[nodiscard]] ManageMediaPlayerControl *playerControl() const;

    [[nodiscard]] ManageHeaderBar *manageHeaderBar() const;

    [[nodiscard]] bool showNowPlayingBackground() const;

    [[nodiscard]] bool showProgressOnTaskBar() const;

    [[nodiscard]] bool showSystemTrayIcon() const;

    [[nodiscard]] bool useFavoriteStyleRatings() const;

    [[nodiscard]] bool useAlbumArtistInArtistsView() const;

    [[nodiscard]] ElisaUtils::PlayListEntryType embeddedView() const;

    int initialViewIndex() const;

    [[nodiscard]] QString initialFilesViewPath() const;

    QString activeColorSchemeName() const;
    void setActiveColorSchemeName(const QString &name);

    /**
     * True when the colors in use are dark. Setting it switches to a dark
     * (Breeze Dark) or light (Breeze Light) color scheme.
     */
    [[nodiscard]] bool darkMode() const;
    void setDarkMode(bool dark);

Q_SIGNALS:

    void musicManagerChanged();

    void mediaPlayListChanged();

    void mediaPlayListProxyModelChanged();

    void audioPlayerChanged();

    void audioControlChanged();

    void playerControlChanged();

    void manageHeaderBarChanged();

    void enqueue(const DataTypes::EntryDataList &newEntries,
                 ElisaUtils::PlayListEnqueueMode enqueueMode,
                 ElisaUtils::PlayListEnqueueTriggerPlay triggerPlay);

    void initializationDone();

    void showNowPlayingBackgroundChanged();

    void showProgressOnTaskBarChanged();

    void showSystemTrayIconChanged();

    void useFavoriteStyleRatingsChanged();

    void useAlbumArtistInArtistsViewChanged();

    void showFileNamesChanged();

    void importMenuEnabledChanged();

    void commitDataRequest();

    void embeddedViewChanged();

    void initialViewIndexChanged();

    void initialFilesViewPathChanged();

    void activeColorSchemeNameChanged();

    void darkModeChanged();

    void raisePlayer();

    // Actions

    void openAboutAppPage();

    void openAboutKDEPage();

    void configureElisa();

    void goBack();

    void togglePlaylist();

    void seek();

    void scrub();

    void nextTrack();

    void previousTrack();

    void playPause();

    void find();

    void togglePartyMode();

public Q_SLOTS:

    void reportBug();

    void configureShortcuts();

    bool openFiles(const QList<QUrl> &files);
    bool openFiles(const QList<QUrl> &files, const QString &workingDirectory);

    void activateActionRequested(const QString &actionName, const QVariant &parameter);

    void activateRequested(const QStringList &arguments, const QString &workingDirectory);

    void openRequested(const QList<QUrl> &uris);

    void initialize();

private Q_SLOTS:

    void configChanged();

protected:

    bool eventFilter(QObject *watched, QEvent *event) override;

private:

    [[nodiscard]] QString colorSchemeNameFor(bool dark) const;

    void initializeModels();

    void initializePlayer();

    void setupActions();

    [[nodiscard]] DataTypes::EntryDataList checkFileListAndMakeAbsolute(const DataTypes::EntryDataList &filesList,
                                                              const QString &workingDirectory) const;

    QAbstractItemModel *colorSchemesModel();

    std::unique_ptr<ElisaApplicationPrivate> d;

};

#endif // ELISAAPPLICATION_H
