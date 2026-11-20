/*
   SPDX-FileCopyrightText: 2017 (c) Matthieu Gallien <matthieu_gallien@yahoo.fr>
   SPDX-FileCopyrightText: 2012 (c) Aleix Pol Gonzalez <aleixpol@blue-systems.com>

   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "elisaapplication.h"

#include "config-upnp-qt.h"

#if QtDBus_FOUND
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#endif

#include <memory>
#include <functional>

#include "musiclistenersmanager.h"

#include "mediaplaylist.h"
#include "mediaplaylistproxymodel.h"
#include "audiowrapper.h"
#include "manageaudioplayer.h"
#include "managemediaplayercontrol.h"
#include "manageheaderbar.h"
#include "databaseinterface.h"

#include "elisa_settings.h"
#include <KAuthorized>
#include <KLocalizedString>
#include <KStandardActions>

#if KFXmlGui_FOUND
#include <KActionCollection>
#include <KHelpMenu>
#include <KBugReport>
#include <KShortcutsDialog>
#endif

#include <KColorSchemeManager>
#include <KAboutData>

#if KFKIO_FOUND
#include <KIO/OpenFileManagerWindowJob>
#endif

#include <QQmlEngine>
#include <QQmlComponent>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QPointer>
#include <QIcon>
#include <QAction>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QFileSystemWatcher>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDirIterator>
#include <QStandardPaths>
#include <QProcess>
#include <QKeyEvent>
#include <QFileSystemWatcher>
#include <QMimeDatabase>
#include <QMimeType>
#include "sleeptimer.h"
#include "blacklistmanager.h"
#include "updatechecker.h"
#include "tracknamecleaner.h"
#include "importmenuinstaller.h"
#include "musicorganizer.h"
#include "albumlookup.h"
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include "filescanner.h"
#include "databaseinterface.h"
#include <QTimer>
#include <QPalette>
#include <QDebug>
#include <QEvent>
#include <QColor>
#include <KConfig>
#include <KConfigGroup>

using namespace Qt::Literals::StringLiterals;

class ElisaApplicationPrivate
{
public:

    explicit ElisaApplicationPrivate(QObject *parent)
#if KFXmlGui_FOUND
        : mCollection(parent)
#endif
    {
        Q_UNUSED(parent)
    }

#if KFXmlGui_FOUND
    KActionCollection mCollection;
#endif

    std::unique_ptr<MusicListenersManager> mMusicManager;

    bool mUseAlbumArtistInArtistsView = false;

    std::unique_ptr<MediaPlayList> mMediaPlayList;

    std::unique_ptr<MediaPlayListProxyModel> mMediaPlayListProxyModel;

    std::unique_ptr<AudioWrapper> mAudioWrapper;

    std::unique_ptr<ManageAudioPlayer> mAudioControl;

    std::unique_ptr<SleepTimer> mSleepTimer = std::make_unique<SleepTimer>();

    std::unique_ptr<BlacklistManager> mBlacklist = std::make_unique<BlacklistManager>();

    std::unique_ptr<UpdateChecker> mUpdateChecker = std::make_unique<UpdateChecker>();

    bool mShowFileNames = false;

    bool mImportMenuEnabled = true;

    bool mSortImportedMusic = true;

    bool mImportBusy = false;

    qreal mImportProgress = -1;

    QString mImportStatus;

    bool mImportDone = false;

    QFileSystemWatcher mImportWatcher;

    QTimer mImportTimer;

    std::unique_ptr<ManageMediaPlayerControl> mPlayerControl;

    std::unique_ptr<ManageHeaderBar> mManageHeaderBar;

    QFileSystemWatcher mConfigFileWatcher;

    KColorSchemeManager *mSchemes = nullptr;
};

ElisaApplication::ElisaApplication(QObject *parent) : QObject(parent), d(std::make_unique<ElisaApplicationPrivate>(this))
{
    auto configurationFileName = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    configurationFileName += QStringLiteral("/elisarc");
    Elisa::ElisaConfiguration::instance(configurationFileName);
    Elisa::ElisaConfiguration::self()->load();
    Elisa::ElisaConfiguration::self()->save();
    d->mSchemes = KColorSchemeManager::instance();

    // notice palette changes (color scheme switched from the menu, settings or the system)
    QCoreApplication::instance()->installEventFilter(this);

    d->mConfigFileWatcher.addPath(Elisa::ElisaConfiguration::self()->config()->name());

    connect(Elisa::ElisaConfiguration::self(), &Elisa::ElisaConfiguration::configChanged,
            this, &ElisaApplication::configChanged);

    connect(&d->mConfigFileWatcher, &QFileSystemWatcher::fileChanged,
            this, &ElisaApplication::configChanged);

    connect(static_cast<QGuiApplication*>(QCoreApplication::instance()), &QGuiApplication::commitDataRequest,
            this, &ElisaApplication::commitDataRequest);

    setupActions();
}

ElisaApplication::~ElisaApplication()
= default;

QAbstractItemModel *ElisaApplication::colorSchemesModel()
{
    return d->mSchemes->model();
}

QString ElisaApplication::activeColorSchemeName() const
{
    return d->mSchemes->activeSchemeName();
}

void ElisaApplication::setActiveColorSchemeName(const QString &name)
{
    d->mSchemes->activateScheme(d->mSchemes->indexForScheme(name));
    Q_EMIT activeColorSchemeNameChanged();
    Q_EMIT darkModeChanged();
}

bool ElisaApplication::darkMode() const
{
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

void ElisaApplication::setDarkMode(bool dark)
{
    if (darkMode() == dark) {
        return;
    }

    const auto schemeName = colorSchemeNameFor(dark);
    if (schemeName.isEmpty()) {
        qWarning() << "ElisaApplication::setDarkMode: no" << (dark ? "dark" : "light") << "color scheme is installed";
        Q_EMIT darkModeChanged();
        return;
    }

    setActiveColorSchemeName(schemeName);
}

QString ElisaApplication::colorSchemeNameFor(bool dark) const
{
    const QStringList preferred = dark ? QStringList{QStringLiteral("Breeze Dark")}
                                       : QStringList{QStringLiteral("Breeze Light"), QStringLiteral("Breeze")};
    for (const auto &name : preferred) {
        if (d->mSchemes->indexForScheme(name).isValid()) {
            return name;
        }
    }

    // otherwise pick the first installed scheme with a dark (or light) window background
    const auto *model = d->mSchemes->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        const auto path = index.data(Qt::UserRole).toString(); // KColorSchemeModel::PathRole
        if (path.isEmpty()) {
            continue;
        }
        KConfig schemeConfig(path, KConfig::SimpleConfig);
        const auto background = KConfigGroup(&schemeConfig, QStringLiteral("Colors:Window")).readEntry("BackgroundNormal", QColor());
        if (background.isValid() && (background.lightness() < 128) == dark) {
            return index.data(Qt::DisplayRole).toString();
        }
    }

    return {};
}

bool ElisaApplication::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == QCoreApplication::instance() && event->type() == QEvent::ApplicationPaletteChange) {
        Q_EMIT darkModeChanged();
    }

    return QObject::eventFilter(watched, event);
}

void ElisaApplication::setupActions()
{
#if KFXmlGui_FOUND
    QString actionName;

    actionName = u"file_quit"_s;
    auto quitAction = KStandardActions::quit(QCoreApplication::instance(), &QCoreApplication::quit, &d->mCollection);
    d->mCollection.addAction(actionName, quitAction);

    actionName = u"help_report_bug"_s;
    if (KAuthorized::authorizeAction(actionName) && !KAboutData::applicationData().bugAddress().isEmpty()) {
        auto reportBugAction = KStandardActions::reportBug(this, &ElisaApplication::reportBug, &d->mCollection);
        d->mCollection.addAction(reportBugAction->objectName(), reportBugAction);
    }

    actionName = u"help_about_app"_s;
    if (KAuthorized::authorizeAction(actionName)) {
        auto aboutAppAction = KStandardActions::aboutApp(this, &ElisaApplication::openAboutAppPage, this);
        d->mCollection.addAction(aboutAppAction->objectName(), aboutAppAction);
    }

    actionName = u"help_about_kde"_s;
    if (KAuthorized::authorizeAction(actionName)) {
        auto aboutKDEAction = KStandardActions::aboutKDE(this, &ElisaApplication::openAboutKDEPage, this);
        d->mCollection.addAction(aboutKDEAction->objectName(), aboutKDEAction);
    }

    actionName = u"options_configure"_s;
    if (KAuthorized::authorizeAction(actionName)) {
        auto preferencesAction = KStandardActions::preferences(this, &ElisaApplication::configureElisa, this);
        d->mCollection.addAction(preferencesAction->objectName(), preferencesAction);
    }

    actionName = u"options_configure_keybinding"_s;
    if (KAuthorized::authorizeAction(actionName)) {
        auto keyBindingsAction = KStandardActions::keyBindings(this, &ElisaApplication::configureShortcuts, this);
        d->mCollection.addAction(keyBindingsAction->objectName(), keyBindingsAction);
    }

    actionName = u"go_back"_s;
    if (KAuthorized::authorizeAction(actionName)) {
        auto goBackAction = KStandardActions::back(this, &ElisaApplication::goBack, this);
        d->mCollection.addAction(goBackAction->objectName(), goBackAction);
    }

    actionName = u"toggle_playlist"_s;
    if (KAuthorized::authorizeAction(actionName)) {
        auto togglePlaylistAction = d->mCollection.addAction(actionName, this, &ElisaApplication::togglePlaylist);
        togglePlaylistAction->setShortcut(QKeySequence(Qt::Key_F9));
        togglePlaylistAction->setText(i18nc("@action", "Toggle Playlist"));
    }

    actionName = u"Seek"_s;
    if (KAuthorized::authorizeAction(actionName)) {
            auto seekAction = d->mCollection.addAction(actionName, this, &ElisaApplication::seek);
            seekAction->setText(i18nc("@action", "Seek forward 10 seconds"));
            d->mCollection.setDefaultShortcut(seekAction, QKeySequence(Qt::SHIFT | Qt::Key_Right));
    }

    actionName = u"Scrub"_s;
    if (KAuthorized::authorizeAction(actionName)) {
            auto scrubAction = d->mCollection.addAction(actionName, this, &ElisaApplication::scrub);
            scrubAction->setText(i18nc("@action", "Scrub backwards 10 seconds"));
            d->mCollection.setDefaultShortcut(scrubAction, QKeySequence(Qt::SHIFT | Qt::Key_Left));
    }

    actionName = u"NextTrack"_s;
    if (KAuthorized::authorizeAction(actionName)) {
            auto nextTrackAction = d->mCollection.addAction(actionName, this, &ElisaApplication::nextTrack);
            nextTrackAction->setText(i18nc("@action", "Go to next track"));
            d->mCollection.setDefaultShortcut(nextTrackAction, QKeySequence(Qt::CTRL | Qt::Key_Right));
    }

    actionName = u"PreviousTrack"_s;
    if (KAuthorized::authorizeAction(actionName)) {
            auto previousTrackAction = d->mCollection.addAction(actionName, this, &ElisaApplication::previousTrack);
            previousTrackAction->setText(i18nc("@action", "Go to previous track"));
            d->mCollection.setDefaultShortcut(previousTrackAction, QKeySequence(Qt::CTRL | Qt::Key_Left));
    }

    actionName = u"Play-Pause"_s;
    if (KAuthorized::authorizeAction(actionName)) {
            auto playPauseAction = d->mCollection.addAction(actionName, this, &ElisaApplication::playPause);
            playPauseAction->setText(i18nc("@action", "Play/pause"));
            d->mCollection.setDefaultShortcut(playPauseAction, QKeySequence(Qt::Key_Space));
    }

    actionName = u"edit_find"_s;
    if (KAuthorized::authorizeAction(actionName)) {
        auto findAction = KStandardActions::find(this, &ElisaApplication::find, this);
        d->mCollection.addAction(findAction->objectName(), findAction);
    }

    actionName = u"togglePartyMode"_s;
    if (KAuthorized::authorizeAction(actionName)) {
        auto togglePartyModeAction = d->mCollection.addAction(actionName, this, &ElisaApplication::togglePartyMode);
        togglePartyModeAction->setText(i18nc("@action", "Toggle Party Mode"));
        d->mCollection.setDefaultShortcut(togglePartyModeAction, QKeySequence(Qt::Key_F11));
        d->mCollection.addAction(togglePartyModeAction->objectName(), togglePartyModeAction);
    }

    d->mCollection.readSettings();
#else
#endif
}

bool ElisaApplication::openFiles(const QList<QUrl> &files)
{
    return openFiles(files, QDir::currentPath());
}

bool ElisaApplication::openFiles(const QList<QUrl> &files, const QString &workingDirectory)
{
    auto audioFiles = DataTypes::EntryDataList{};
    const QMimeDatabase mimeDB;
    for (const auto &file : files) {
        const QMimeType mime = mimeDB.mimeTypeForUrl(file);
        if (ElisaUtils::isPlayList(mime)) {
            d->mMediaPlayListProxyModel->loadPlayList(file);
        } else if (mime.name().startsWith(QStringLiteral("audio/"))) {
            audioFiles.push_back(DataTypes::EntryData{{{DataTypes::ElementTypeRole, ElisaUtils::FileName},
                            {DataTypes::ResourceRole, file}}, {}, {}});
        }
    }
    auto filesToOpen = checkFileListAndMakeAbsolute(audioFiles, workingDirectory);
    if (!filesToOpen.isEmpty()) {
        Q_EMIT enqueue(filesToOpen,
                       ElisaUtils::PlayListEnqueueMode::AppendPlayList,
                       ElisaUtils::PlayListEnqueueTriggerPlay::TriggerPlay);
    }
    return (audioFiles.count() == filesToOpen.count());
}

void ElisaApplication::activateActionRequested(const QString &actionName, const QVariant &parameter)
{
    Q_UNUSED(actionName)
    Q_UNUSED(parameter)
}

void ElisaApplication::activateRequested(const QStringList &arguments, const QString &workingDirectory)
{
    Q_EMIT raisePlayer();
    if (arguments.size() > 1) {
        QList<QUrl> urls;
        bool isFirst = true;
        for (const auto &oneArgument : arguments) {
            if (isFirst) {
                isFirst = false;
                continue;
            }
            urls.push_back(QUrl::fromUserInput(oneArgument));
        }
        openFiles(urls, workingDirectory);
    }
}

void ElisaApplication::openRequested(const QList<QUrl> &uris)
{
    Q_EMIT raisePlayer();
    openFiles(uris);
}

void ElisaApplication::reportBug()
{
#if KFXmlGui_FOUND
    static QPointer<QDialog> dialog;
    if (!dialog) {
        dialog = new KBugReport(KAboutData::applicationData(), nullptr);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    dialog->show();
#endif
}

void ElisaApplication::configureShortcuts()
{
#if KFXmlGui_FOUND
    KShortcutsDialog dlg(KShortcutsEditor::ApplicationAction, KShortcutsEditor::LetterShortcutsAllowed, nullptr);
    dlg.setModal(true);
    dlg.addCollection(&d->mCollection);
    dlg.configure();
#endif
}

void ElisaApplication::configChanged()
{
    auto currentConfiguration = Elisa::ElisaConfiguration::self();

    d->mConfigFileWatcher.addPath(currentConfiguration->config()->name());

    currentConfiguration->load();
    currentConfiguration->read();

    Q_EMIT showNowPlayingBackgroundChanged();
    Q_EMIT showProgressOnTaskBarChanged();
    Q_EMIT showSystemTrayIconChanged();
    Q_EMIT useFavoriteStyleRatingsChanged();
    Q_EMIT embeddedViewChanged();

    // the database must know about the new value before the views reload their data
    const auto useAlbumArtist = currentConfiguration->useAlbumArtistInArtistsView();
    if (d->mMusicManager) {
        d->mMusicManager->viewDatabase()->setUseAlbumArtistInArtistsView(useAlbumArtist);
    }
    if (d->mUseAlbumArtistInArtistsView != useAlbumArtist) {
        d->mUseAlbumArtistInArtistsView = useAlbumArtist;
        Q_EMIT useAlbumArtistInArtistsViewChanged();
    }
    Q_EMIT initialViewIndexChanged();
    Q_EMIT initialFilesViewPathChanged();
}

DataTypes::EntryDataList ElisaApplication::checkFileListAndMakeAbsolute(const DataTypes::EntryDataList &filesList,
                                                                         const QString &workingDirectory) const
{
    auto filesToOpen = DataTypes::EntryDataList{};

    for (const auto &oneFile : filesList) {
        auto trackUrl = oneFile.musicData[DataTypes::ResourceRole].toUrl();
        if (trackUrl.scheme().isEmpty() || trackUrl.isLocalFile()) {
            auto newFile = QFileInfo(trackUrl.toLocalFile());
            if (trackUrl.scheme().isEmpty()) {
                newFile = QFileInfo(trackUrl.toString());
            }

            if (newFile.isRelative()) {
                if (trackUrl.scheme().isEmpty()) {
                    newFile.setFile(workingDirectory, trackUrl.toString());
                } else {
                    newFile.setFile(workingDirectory, trackUrl.toLocalFile());
                }
            }

            if (newFile.exists()) {
                auto trackData = oneFile.musicData;
                trackData[DataTypes::ResourceRole] = QUrl::fromLocalFile(newFile.canonicalFilePath());
                filesToOpen.push_back({trackData, oneFile.title, {}});
            }
        } else {
            filesToOpen.push_back(oneFile);
        }
    }

    return filesToOpen;
}

void ElisaApplication::initialize()
{
    initializeModels();
    initializePlayer();

    Q_EMIT initializationDone();
}

void ElisaApplication::initializeModels()
{
    d->mMusicManager = std::make_unique<MusicListenersManager>();
    d->mUseAlbumArtistInArtistsView = Elisa::ElisaConfiguration::self()->useAlbumArtistInArtistsView();
    d->mMusicManager->viewDatabase()->setUseAlbumArtistInArtistsView(d->mUseAlbumArtistInArtistsView);

    // hidden music: keep it out of the library, and read files again when they are shown again
    d->mBlacklist->setDatabase(d->mMusicManager->viewDatabase());
    connect(d->mBlacklist.get(), &BlacklistManager::filesToShowAgain, this, [this](const QList<QUrl> &files) {
        FileScanner scanner;
        DataTypes::ListTrackDataType tracks;
        for (const auto &file : files) {
            if (!file.isLocalFile() || !scanner.shouldScanFile(file.toLocalFile())) {
                continue;
            }
            auto track = scanner.scanOneFile(file);
            if (track.isValid()) {
                tracks.push_back(std::move(track));
            }
        }
        if (tracks.isEmpty()) {
            return;
        }
        auto *database = d->mMusicManager->viewDatabase();
        QMetaObject::invokeMethod(database, [database, tracks] {
            database->insertTracksList(tracks);
        });
    });

    // an album or a cover found online for a song: read the song again, so the library has it
    const auto readAgain = [this](const QUrl &file) {
        if (!d->mMusicManager || !file.isLocalFile()) {
            return;
        }
        FileScanner scanner;
        auto track = scanner.scanOneFile(file);
        if (!track.isValid()) {
            return;
        }
        auto *database = d->mMusicManager->viewDatabase();
        QMetaObject::invokeMethod(database, [database, track] {
            database->insertTracksList({track});
        });
    };
    connect(AlbumLookup::instance(), &AlbumLookup::albumFound, this, [readAgain](const QUrl &file, const QString &) {
        readAgain(file);
    });
    connect(AlbumLookup::instance(), &AlbumLookup::coverFound, this, [readAgain](const QUrl &file, const QString &) {
        readAgain(file);
    });

    // songs already in the library without an album or a cover, and artists without a
    // photo: ask once, a little after start (songs are only read again when they change)
    QTimer::singleShot(15000, this, [this] {
        if (!d->mMusicManager) {
            return;
        }
        auto *database = d->mMusicManager->viewDatabase();
        DataTypes::ListTrackDataType tracks;
        DataTypes::ListArtistDataType artists;
        QMetaObject::invokeMethod(database, [database, &tracks, &artists] {
            tracks = database->allTracksData();
            artists = database->allArtistsData();
        }, Qt::BlockingQueuedConnection);
        for (const auto &track : std::as_const(tracks)) {
            const auto file = track.resourceURI();
            if (!file.isLocalFile()) {
                continue;
            }
            const auto artist = track.albumArtist().isEmpty() ? track.artist() : track.albumArtist();
            if (artist.isEmpty()) {
                continue;
            }
            bool known = false;
            if (track.album().isEmpty()) {
                AlbumLookup::instance()->cachedAlbum(artist, track.title(), &known);
                if (!known) {
                    AlbumLookup::instance()->requestInBackground(artist, track.title(), file);
                }
            } else if (track.albumCover().isEmpty()) {
                AlbumLookup::instance()->cachedCover(artist, track.album(), &known);
                if (!known) {
                    AlbumLookup::instance()->requestCover(artist, track.album(), file);
                }
            }
        }
        for (const auto &artist : std::as_const(artists)) {
            bool known = false;
            const auto name = artist.value(DataTypes::TitleRole).toString();
            AlbumLookup::instance()->cachedArtistPhoto(name, &known);
            if (!known && !name.isEmpty()) {
                AlbumLookup::instance()->requestArtistPhoto(name);
            }
        }
    });

    d->mShowFileNames = Elisa::ElisaConfiguration::self()->showFileNames();

#if defined Q_OS_LINUX && !defined Q_OS_ANDROID
    // keep the file manager's "Elisa: Import to Music" action in place
    d->mImportMenuEnabled = Elisa::ElisaConfiguration::self()->installImportMenu();
    if (d->mImportMenuEnabled) {
        ImportMenuInstaller::install();
    }
#endif

    // Sorting into Artist/Album folders only happens for archives dropped on Elisa:
    // changes inside the music folders never start a sort. Notes left by older builds
    // of "Elisa: Import to Music" are removed without sorting anything.
    d->mSortImportedMusic = Elisa::ElisaConfiguration::self()->sortImportedMusic();
    {
        const auto roots = Elisa::ElisaConfiguration::self()->rootPath();
        for (const auto &root : roots) {
            QFile::remove(QDir::cleanPath(root) + u"/.elisa-import-queue"_s);
        }
    }

    // look for a newer build shortly after every start
    if (Elisa::ElisaConfiguration::self()->checkForUpdatesAtStartup()) {
        QTimer::singleShot(2500, d->mUpdateChecker.get(), [this] {
            d->mUpdateChecker->check(false);
        });
    }
    Q_EMIT musicManagerChanged();

    d->mMediaPlayList = std::make_unique<MediaPlayList>();
    d->mMusicManager->subscribeForTracks(d->mMediaPlayList.get());
    Q_EMIT mediaPlayListChanged();

    d->mMediaPlayListProxyModel = std::make_unique<MediaPlayListProxyModel>();
    d->mMediaPlayListProxyModel->setPlayListModel(d->mMediaPlayList.get());
    Q_EMIT mediaPlayListProxyModelChanged();

    d->mMusicManager->setElisaApplication(this);

    QObject::connect(this, &ElisaApplication::enqueue,
                     d->mMediaPlayListProxyModel.get(), static_cast<void (MediaPlayListProxyModel::*)(const DataTypes::EntryDataList&,
                                                                                            ElisaUtils::PlayListEnqueueMode,
                                                                                            ElisaUtils::PlayListEnqueueTriggerPlay)>(&MediaPlayListProxyModel::enqueue));
}

void ElisaApplication::initializePlayer()
{
    d->mAudioWrapper = std::make_unique<AudioWrapper>();
    Q_EMIT audioPlayerChanged();

    // sleep timer: pause when the time is up or at the end of the current track
    connect(d->mSleepTimer.get(), &SleepTimer::timeout, d->mAudioWrapper.get(), &AudioWrapper::pause);
    connect(d->mAudioWrapper.get(), &AudioWrapper::positionChanged, d->mSleepTimer.get(), [this](qint64 position) {
        d->mSleepTimer->trackPositionChanged(position, d->mAudioWrapper->duration());
    });
    connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::currentTrackChanged, d->mSleepTimer.get(), &SleepTimer::trackChanged);
    d->mAudioControl = std::make_unique<ManageAudioPlayer>();
    Q_EMIT audioControlChanged();
    d->mPlayerControl = std::make_unique<ManageMediaPlayerControl>();
    Q_EMIT playerControlChanged();
    d->mManageHeaderBar = std::make_unique<ManageHeaderBar>();
    Q_EMIT manageHeaderBarChanged();

    d->mAudioControl->setAlbumNameRole(MediaPlayList::AlbumRole);
    d->mAudioControl->setArtistNameRole(MediaPlayList::ArtistRole);
    d->mAudioControl->setTitleRole(MediaPlayList::TitleRole);
    d->mAudioControl->setUrlRole(MediaPlayList::ResourceRole);
    d->mAudioControl->setIsPlayingRole(MediaPlayList::IsPlayingRole);
    d->mAudioControl->setPlayListModel(d->mMediaPlayListProxyModel.get());

    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::playerPlay, d->mAudioWrapper.get(), &AudioWrapper::play);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::playerPause, d->mAudioWrapper.get(), &AudioWrapper::pause);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::playerStop, d->mAudioWrapper.get(), &AudioWrapper::stop);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::seek, d->mAudioWrapper.get(), &AudioWrapper::seek);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::saveUndoPositionInAudioWrapper, d->mAudioWrapper.get(), &AudioWrapper::saveUndoPosition);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::restoreUndoPositionInAudioWrapper, d->mAudioWrapper.get(), &AudioWrapper::restoreUndoPosition);

    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::skipNextTrack, d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::skipNextTrack);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::skipPreviousTrack, d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::skipPreviousTrack);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::sourceInError, d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::trackInError);

    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::sourceInError, d->mMusicManager.get(), &MusicListenersManager::playBackError);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::playerSourceChanged, d->mAudioWrapper.get(), &AudioWrapper::setSource);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::startedPlayingTrack,
                     d->mMusicManager->viewDatabase(), &DatabaseInterface::trackHasStartedPlaying);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::finishedPlayingTrack,
                     d->mMusicManager->viewDatabase(), &DatabaseInterface::trackHasFinishedPlaying);
    QObject::connect(d->mAudioControl.get(), &ManageAudioPlayer::updateData, d->mMediaPlayList.get(), &MediaPlayList::setData);

    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::ensurePlay, d->mAudioControl.get(), &ManageAudioPlayer::ensurePlay);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::requestPlay, d->mAudioControl.get(), &ManageAudioPlayer::requestPlay);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::playListFinished, d->mAudioControl.get(), &ManageAudioPlayer::playListFinished);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::currentTrackChanged, d->mAudioControl.get(), &ManageAudioPlayer::setCurrentTrack);

    // songs deleted from the computer: stop if one is playing, take them out of the
    // playlist, so the header (title, artist, album, cover) no longer shows them
    QObject::connect(d->mBlacklist.get(), &BlacklistManager::filesDeleted, this, [this](const QList<QUrl> &files) {
        auto *playlist = d->mMediaPlayListProxyModel.get();
        if (!playlist) {
            return;
        }
        const auto current = playlist->currentTrackRow();
        bool currentDeleted = false;
        QList<int> rows;
        for (int row = 0; row < playlist->rowCount(); ++row) {
            const auto url = playlist->index(row, 0).data(MediaPlayList::ResourceRole).toUrl();
            if (files.contains(url)) {
                rows.push_back(row);
                currentDeleted = currentDeleted || row == current;
            }
        }
        if (currentDeleted) {
            d->mAudioControl->stop();
            d->mAudioWrapper->stop();
            d->mAudioWrapper->setSource(QUrl{});
        }
        std::sort(rows.begin(), rows.end(), std::greater<int>{});
        for (const auto row : std::as_const(rows)) {
            playlist->removeRow(row);
        }
    });
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::clearPlayListPlayer, d->mAudioControl.get(), &ManageAudioPlayer::saveForUndoClearPlaylist);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::undoClearPlayListPlayer, d->mAudioControl.get(), &ManageAudioPlayer::restoreForUndoClearPlaylist);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::seek, d->mAudioWrapper.get(), &AudioWrapper::seek);

    // the song after the current one, for crossfading into it
    auto updateNextSource = [this]() {
        d->mAudioWrapper->setNextSource(d->mMediaPlayListProxyModel->nextTrack().data(MediaPlayList::ResourceRole).toUrl());
    };
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::nextTrackChanged, d->mAudioWrapper.get(), updateNextSource);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::nextTrackDataChanged, d->mAudioWrapper.get(), updateNextSource);
    updateNextSource();

    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::playbackStateChanged,
                     d->mAudioControl.get(), &ManageAudioPlayer::setPlayerPlaybackState);
    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::statusChanged, d->mAudioControl.get(), &ManageAudioPlayer::setPlayerStatus);
    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::errorChanged, d->mAudioControl.get(), &ManageAudioPlayer::setPlayerError);
    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::durationChanged, d->mAudioControl.get(), &ManageAudioPlayer::setAudioDuration);
    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::seekableChanged, d->mAudioControl.get(), &ManageAudioPlayer::setPlayerIsSeekable);
    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::positionChanged, d->mAudioControl.get(), &ManageAudioPlayer::setPlayerPosition);
    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::currentPlayingForRadiosChanged, d->mAudioControl.get(), &ManageAudioPlayer::setCurrentPlayingForRadios);

    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::currentTrackChanged, d->mPlayerControl.get(), &ManageMediaPlayerControl::setCurrentTrack);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::previousTrackChanged, d->mPlayerControl.get(), &ManageMediaPlayerControl::setPreviousTrack);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::nextTrackChanged, d->mPlayerControl.get(), &ManageMediaPlayerControl::setNextTrack);

    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::playing, d->mPlayerControl.get(), &ManageMediaPlayerControl::playerPlaying);
    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::paused, d->mPlayerControl.get(), &ManageMediaPlayerControl::playerPausedOrStopped);
    QObject::connect(d->mAudioWrapper.get(), &AudioWrapper::stopped, d->mPlayerControl.get(), &ManageMediaPlayerControl::playerPausedOrStopped);

    d->mManageHeaderBar->setTitleRole(MediaPlayList::TitleRole);
    d->mManageHeaderBar->setAlbumRole(MediaPlayList::AlbumRole);
    d->mManageHeaderBar->setAlbumArtistRole(MediaPlayList::AlbumArtistRole);
    d->mManageHeaderBar->setArtistRole(MediaPlayList::ArtistRole);
    d->mManageHeaderBar->setFileNameRole(MediaPlayList::ResourceRole);
    d->mManageHeaderBar->setImageRole(MediaPlayList::ImageUrlRole);
    d->mManageHeaderBar->setDatabaseIdRole(MediaPlayList::DatabaseIdRole);
    d->mManageHeaderBar->setTrackTypeRole(MediaPlayList::ElementTypeRole);
    d->mManageHeaderBar->setAlbumIdRole(MediaPlayList::AlbumIdRole);
    d->mManageHeaderBar->setIsValidRole(MediaPlayList::IsValidRole);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::currentTrackChanged, d->mManageHeaderBar.get(), &ManageHeaderBar::setCurrentTrack);
    QObject::connect(d->mMediaPlayListProxyModel.get(), &MediaPlayListProxyModel::currentTrackDataChanged, d->mManageHeaderBar.get(), &ManageHeaderBar::updateCurrentTrackData);
}

QAction * ElisaApplication::action(const QString& name)
{
#if KFXmlGui_FOUND
    auto resultAction = d->mCollection.action(name);

    return resultAction;
#else
    Q_UNUSED(name);

    return new QAction();
#endif
}

void ElisaApplication::showInFolder(const QUrl &filePath)
{
#if KFKIO_FOUND
    KIO::highlightInFileManager({filePath});
#else
    Q_UNUSED(filePath)
#endif
}

MusicListenersManager *ElisaApplication::musicManager() const
{
    return d->mMusicManager.get();
}

MediaPlayList *ElisaApplication::mediaPlayList() const
{
    return d->mMediaPlayList.get();
}

MediaPlayListProxyModel *ElisaApplication::mediaPlayListProxyModel() const
{
    return d->mMediaPlayListProxyModel.get();
}


AudioWrapper *ElisaApplication::audioPlayer() const
{
    return d->mAudioWrapper.get();
}

ManageAudioPlayer *ElisaApplication::audioControl() const
{
    return d->mAudioControl.get();
}

ManageMediaPlayerControl *ElisaApplication::playerControl() const
{
    return d->mPlayerControl.get();
}

ManageHeaderBar *ElisaApplication::manageHeaderBar() const
{
    return d->mManageHeaderBar.get();
}


bool ElisaApplication::showNowPlayingBackground() const
{
    auto currentConfiguration = Elisa::ElisaConfiguration::self();
    return currentConfiguration->showNowPlayingBackground();
}

bool ElisaApplication::showProgressOnTaskBar() const
{
    auto currentConfiguration = Elisa::ElisaConfiguration::self();

    return currentConfiguration->showProgressOnTaskBar();
}

bool ElisaApplication::showSystemTrayIcon() const
{
    auto currentConfiguration = Elisa::ElisaConfiguration::self();

    return currentConfiguration->showSystemTrayIcon();
}

bool ElisaApplication::useFavoriteStyleRatings() const
{
    auto currentConfiguration = Elisa::ElisaConfiguration::self();

    return currentConfiguration->useFavoriteStyleRatings();
}

bool ElisaApplication::useAlbumArtistInArtistsView() const
{
    return d->mUseAlbumArtistInArtistsView;
}

BlacklistManager *ElisaApplication::blacklist() const
{
    return d->mBlacklist.get();
}

UpdateChecker *ElisaApplication::updateChecker() const
{
    return d->mUpdateChecker.get();
}

bool ElisaApplication::showFileNames() const
{
    return d->mShowFileNames;
}

void ElisaApplication::setShowFileNames(bool value)
{
    if (d->mShowFileNames == value) {
        return;
    }

    d->mShowFileNames = value;
    Q_EMIT showFileNamesChanged();

    Elisa::ElisaConfiguration::setShowFileNames(value);
    Elisa::ElisaConfiguration::self()->save();
}

bool ElisaApplication::importMenuEnabled() const
{
    return d->mImportMenuEnabled;
}

void ElisaApplication::setImportMenuEnabled(bool value)
{
    if (d->mImportMenuEnabled == value) {
        return;
    }

    d->mImportMenuEnabled = value;
    Q_EMIT importMenuEnabledChanged();

    if (value) {
        ImportMenuInstaller::install();
    } else {
        ImportMenuInstaller::uninstall();
    }

    Elisa::ElisaConfiguration::setInstallImportMenu(value);
    Elisa::ElisaConfiguration::self()->save();
}

bool ElisaApplication::sortImportedMusic() const
{
    return d->mSortImportedMusic;
}

void ElisaApplication::setSortImportedMusic(bool value)
{
    if (d->mSortImportedMusic == value) {
        return;
    }

    d->mSortImportedMusic = value;
    Q_EMIT sortImportedMusicChanged();

    Elisa::ElisaConfiguration::setSortImportedMusic(value);
    Elisa::ElisaConfiguration::self()->save();
}

void ElisaApplication::processImportQueues()
{
    QList<std::pair<QString, QStringList>> work;

    const auto roots = Elisa::ElisaConfiguration::self()->rootPath();
    for (const auto &configuredRoot : roots) {
        const auto root = QDir::cleanPath(configuredRoot);
        QFile queue{root + u"/.elisa-import-queue"_s};
        if (!queue.open(QIODevice::ReadOnly)) {
            continue;
        }
        QStringList items;
        const auto lines = queue.readAll().split('\n');
        for (const auto &line : lines) {
            if (!line.trimmed().isEmpty()) {
                items.push_back(QString::fromUtf8(line));
            }
        }
        queue.close();
        queue.remove();

        if (d->mSortImportedMusic && !items.isEmpty()) {
            work.push_back({root, items});
        }
    }

    sortInBackground(work);
}

bool ElisaApplication::canImportArchives() const
{
#if defined Q_OS_LINUX && !defined Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

namespace {

// size of everything in a folder, to follow how far the unpacking has come
qint64 folderSize(const QString &folder)
{
    qint64 size = 0;
    QDirIterator it{folder, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories};
    while (it.hasNext()) {
        it.next();
        size += it.fileInfo().size();
    }
    return size;
}

// "Name", "Name (2)", "Name (3)"... : a path that does not exist yet
QString uniquePath(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        return path;
    }
    for (int n = 2;; ++n) {
        const auto candidate = path + u" ("_s + QString::number(n) + u")"_s;
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

QString archiveStem(const QString &fileName)
{
    static const QRegularExpression extension{u"\\.(tar\\.(gz|bz2|xz|zst)|zip|7z|rar|tar|tgz|tbz2|txz)$"_s, QRegularExpression::CaseInsensitiveOption};
    auto stem = fileName;
    stem.remove(extension);
    return stem.isEmpty() ? fileName : stem;
}

QProcessEnvironment systemToolsEnvironment()
{
    // the unpacking tools come from the system, not from Elisa's own (AppImage) libraries
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(u"LD_LIBRARY_PATH"_s);
    environment.remove(u"LD_PRELOAD"_s);
    return environment;
}

// total unpacked size of an archive, from bsdtar's listing ("-rw-r--r-- 0 user group SIZE date name")
qint64 archiveContentSize(const QString &bsdtar, const QString &archive)
{
    QProcess list;
    list.setProcessEnvironment(systemToolsEnvironment());
    list.start(bsdtar, {u"-tvf"_s, archive});
    if (!list.waitForFinished(120000) || list.exitCode() != 0) {
        return -1;
    }
    qint64 total = 0;
    const auto lines = QString::fromUtf8(list.readAllStandardOutput()).split(u'\n', Qt::SkipEmptyParts);
    for (const auto &line : lines) {
        if (!line.startsWith(u'-')) {
            continue; // folders and links take no room
        }
        const auto fields = line.simplified().split(u' ');
        if (fields.size() > 4) {
            total += fields.at(4).toLongLong();
        }
    }
    return total;
}

// progress of an import: (progress 0..1 or -1 when unknown, status, done)
using ImportReport = std::function<void(qreal, const QString &, bool)>;

// the first music folder: where imported music goes
QString importDestination()
{
    QString destination;
    const auto roots = Elisa::ElisaConfiguration::self()->rootPath();
    for (const auto &root : roots) {
        if (QFileInfo{root}.isDir()) {
            destination = QDir::cleanPath(root);
            break;
        }
    }
    if (destination.isEmpty()) {
        destination = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    }
    QDir{}.mkpath(destination);
    return destination;
}

// Reads the tags of every song in \a items once, then asks MusicBrainz for the albums
// that are missing, in a few requests per artist; returns a TagReader for
// MusicOrganizer::organize that answers from that and reports what it sorts.
// Progress: reading tags 0-35 %, looking up albums 35-65 %, sorting 65-100 %.
MusicOrganizer::TagReader prepareTags(const QStringList &items, const QSet<QUrl> &hidden, const ImportReport &report)
{
    struct Song {
        MusicOrganizer::SongTags tags;
        QString title;
    };
    auto songs = std::make_shared<QHash<QString, Song>>();
    FileScanner scanner;

    QStringList files;
    for (const auto &item : items) {
        if (QFileInfo{item}.isFile()) {
            files.push_back(item);
            continue;
        }
        QDirIterator it{item, QDir::Files, QDirIterator::Subdirectories};
        while (it.hasNext()) {
            files.push_back(it.next());
        }
    }
    files.erase(std::remove_if(files.begin(), files.end(), [&](const QString &file) {
        return hidden.contains(QUrl::fromLocalFile(file)) || !scanner.shouldScanFile(file);
    }), files.end());
    const auto total = files.size();

    // 1. tags
    QHash<QString, QStringList> missingByArtist;
    for (int i = 0; i < total; ++i) {
        const auto &file = files.at(i);
        report(0.35 * i / std::max<qsizetype>(total, 1),
               i18nc("@info:progress %1 file name, %2 number, %3 count", "Reading tags: %1 (%2 of %3)", QFileInfo{file}.fileName(), i + 1, total), false);
        const auto track = scanner.scanOneFile(QUrl::fromLocalFile(file));
        Song song;
        song.tags.isSong = track.isValid();
        song.tags.artist = track.artist();
        song.tags.albumArtist = track.albumArtist();
        song.tags.album = track.album();
        song.title = track.title();
        const auto artist = song.tags.albumArtist.isEmpty() ? song.tags.artist : song.tags.albumArtist;
        if (song.tags.isSong && song.tags.album.trimmed().isEmpty() && !artist.isEmpty() && !song.title.isEmpty()) {
            missingByArtist[artist].push_back(song.title);
        }
        songs->insert(file, song);
    }

    // 2. missing albums, all songs of an artist together
    int asked = 0;
    const auto artists = missingByArtist.keys();
    for (const auto &artist : artists) {
        const auto &titles = missingByArtist[artist];
        report(0.35 + 0.30 * asked / std::max<qsizetype>(artists.size(), 1),
               i18ncp("@info:progress %1 artist", "Looking up albums: %2 (%1 song)…", "Looking up albums: %2 (%1 songs)…", titles.size(), artist), false);
        const auto albums = AlbumLookup::instance()->lookupMany(artist, titles);
        for (auto it = songs->begin(); it != songs->end(); ++it) {
            auto &song = it.value();
            const auto songArtist = song.tags.albumArtist.isEmpty() ? song.tags.artist : song.tags.albumArtist;
            if (song.tags.album.trimmed().isEmpty() && songArtist == artist) {
                song.tags.album = albums.value(song.title);
            }
        }
        ++asked;
    }

    // 3. sorting: the organizer asks for each file
    auto sorted = std::make_shared<int>(0);
    return [songs, sorted, total, report](const QString &file) {
        const auto it = songs->constFind(file);
        if (it == songs->cend()) {
            return MusicOrganizer::SongTags{}; // covers, booklets, hidden songs
        }
        ++*sorted;
        const auto &song = it.value();
        const auto artist = song.tags.albumArtist.isEmpty() ? song.tags.artist : song.tags.albumArtist;
        const auto name = artist.isEmpty() ? QFileInfo{file}.fileName() : artist + u" – "_s + song.title;
        report(0.65 + 0.35 * *sorted / std::max<qsizetype>(total, 1),
               i18nc("@info:progress %1 artist and song, %2 number, %3 count", "Sorting: %1 (%2 of %3)", name, std::min<qsizetype>(*sorted, total), total), false);
        return song.tags;
    };
}

// sorts the songs of \a imported (files or folders inside \a destination) into
// Artist/Album folders; returns how many songs
int sortIntoAlbumFolders(const QString &destination, const QStringList &imported, const QSet<QUrl> &hidden, const ImportReport &report)
{
    const auto readTags = prepareTags(imported, hidden, report);
    const auto result = MusicOrganizer::organize(destination, imported, readTags);
    return result.moved + result.alreadyInPlace;
}

// what a folder holds: number of files and their total size
std::pair<int, qint64> folderContents(const QString &folder)
{
    int files = 0;
    qint64 size = 0;
    QDirIterator it{folder, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories};
    while (it.hasNext()) {
        it.next();
        ++files;
        size += it.fileInfo().size();
    }
    return {files, size};
}

}

void ElisaApplication::importArchives(const QList<QUrl> &archives)
{
#if defined Q_OS_LINUX && !defined Q_OS_ANDROID
    QStringList files;
    for (const auto &url : archives) {
        if (url.isLocalFile()) {
            files.push_back(url.toLocalFile());
        }
    }
    if (files.isEmpty() || d->mImportBusy) {
        return;
    }

    const auto destination = importDestination();

    const auto bsdtar = QStandardPaths::findExecutable(u"bsdtar"_s);
    if (bsdtar.isEmpty()) {
        // no bsdtar: let the "Import to Music" script do it (no detailed progress)
        const auto cacheFolder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir{}.mkpath(cacheFolder);
        const auto scriptPath = cacheFolder + u"/elisa-import-to-music.sh"_s;
        QFile resource{u":/elisa/elisa-import-to-music.sh"_s};
        QFile script{scriptPath};
        if (!resource.open(QIODevice::ReadOnly) || !script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return;
        }
        script.write(resource.readAll());
        script.close();

        QProcess process;
        process.setProgram(u"/bin/sh"_s);
        process.setArguments(QStringList{scriptPath} + files);
        auto environment = systemToolsEnvironment();
        environment.insert(u"ELISA_IMPORT_DEST"_s, destination);
        process.setProcessEnvironment(environment);
        process.startDetached();
        return;
    }

    const bool sortMusic = d->mSortImportedMusic;
    const auto hidden = d->mBlacklist->hiddenFiles();

    setImportState(true, 0, i18nc("@info:progress", "Unpacking…"), false);

    // progress updates from the worker thread
    const ImportReport report = [this](qreal progress, const QString &status, bool done) {
        reportImport(progress, status, done);
    };

    auto importFuture = QtConcurrent::run([files, destination, bsdtar, sortMusic, hidden, report] {
        // 1. unpack, following the unpacked size against the archives' content size
        QList<qint64> sizes;
        qint64 totalSize = 0;
        for (const auto &file : files) {
            const auto size = archiveContentSize(bsdtar, file);
            sizes.push_back(size);
            totalSize += std::max<qint64>(size, 0);
        }

        QStringList imported;
        int failedArchives = 0;
        qint64 doneSize = 0;
        for (int index = 0; index < files.size(); ++index) {
            const auto &file = files.at(index);
            const auto name = QFileInfo{file}.fileName();
            const auto label = files.size() > 1 ? i18nc("@info:progress %1 archive name, %2 number, %3 count", "Unpacking %1 (%2 of %3)…", name, index + 1, files.size())
                                                : i18nc("@info:progress %1 archive name", "Unpacking %1…", name);

            const auto temporary = destination + u"/.elisa-import-"_s + QString::number(QCoreApplication::applicationPid()) + u'-' + QString::number(index);
            QDir{temporary}.removeRecursively();
            QDir{}.mkpath(temporary);

            QProcess unpack;
            unpack.setProcessEnvironment(systemToolsEnvironment());
            unpack.start(bsdtar, {u"-xf"_s, file, u"-C"_s, temporary});
            unpack.waitForStarted();
            while (!unpack.waitForFinished(200)) {
                if (unpack.state() == QProcess::NotRunning) {
                    break;
                }
                const auto progress = totalSize > 0 ? std::clamp(static_cast<qreal>(doneSize + folderSize(temporary)) / totalSize, 0.0, 1.0) : -1.0;
                report(progress, label, false);
            }

            if (unpack.exitStatus() != QProcess::NormalExit || unpack.exitCode() != 0) {
                QDir{temporary}.removeRecursively();
                ++failedArchives;
                continue;
            }
            doneSize += std::max<qint64>(sizes.at(index), 0);
            report(totalSize > 0 ? static_cast<qreal>(doneSize) / totalSize : -1.0, label, false);

            // one folder inside: keep that folder, not a folder in a folder
            const auto entries = QDir{temporary}.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
            QString target;
            if (entries.size() == 1 && QFileInfo{temporary + u'/' + entries.first()}.isDir()) {
                target = uniquePath(destination + u'/' + entries.first());
                QDir{}.rename(temporary + u'/' + entries.first(), target);
                QDir{}.rmdir(temporary);
            } else {
                target = uniquePath(destination + u'/' + archiveStem(name));
                QDir{}.rename(temporary, target);
            }
            imported.push_back(target);
            QFile::moveToTrash(file);
        }

        // 2. sort into Artist/Album folders, following the songs whose tags are read
        int songs = 0;
        if (sortMusic && !imported.isEmpty()) {
            songs = sortIntoAlbumFolders(destination, imported, hidden, report);
        }

        QString summary;
        if (imported.isEmpty()) {
            summary = i18nc("@info:status", "Could not unpack the archive");
        } else if (sortMusic) {
            summary = i18ncp("@info:status", "Done: %1 song sorted into Artist/Album folders", "Done: %1 songs sorted into Artist/Album folders", songs);
        } else {
            summary = i18ncp("@info:status", "Done: %1 archive unpacked into the music folder", "Done: %1 archives unpacked into the music folder", imported.size());
        }
        if (failedArchives > 0 && !imported.isEmpty()) {
            summary += u" · "_s + i18ncp("@info:status", "%1 archive could not be unpacked", "%1 archives could not be unpacked", failedArchives);
        }
        report(1.0, summary, true);
    });
    Q_UNUSED(importFuture)
#else
    Q_UNUSED(archives)
#endif
}

void ElisaApplication::reportImport(qreal progress, const QString &status, bool done)
{
    // may be called from a worker thread
    QMetaObject::invokeMethod(this, [this, progress, status, done] {
        setImportState(true, progress, status, done);
        if (done) {
            if (d->mMusicManager) {
                d->mMusicManager->scanCollection(MusicListenersManager::CollectionScan::Soft);
            }
            QTimer::singleShot(6000, this, &ElisaApplication::dismissImportStatus);
        }
    }, Qt::QueuedConnection);
}

void ElisaApplication::importFromArk(const QString &service, const QString &objectPath)
{
#if QtDBus_FOUND && defined Q_OS_LINUX && !defined Q_OS_ANDROID
    // Songs dragged out of an archive opened in Ark come without a file: Ark hands out only
    // the dragged songs once it is told where to put them. The archive itself stays packed.
    if (service.trimmed().isEmpty() || objectPath.trimmed().isEmpty() || d->mImportBusy) {
        return;
    }

    const auto destination = importDestination();
    static int arkImports = 0;
    const QString temporary = destination + u"/.elisa-ark-"_s + QString::number(QCoreApplication::applicationPid()) + u'-' + QString::number(arkImports++);
    QDir{temporary}.removeRecursively();
    QDir{}.mkpath(temporary);

    setImportState(true, -1, i18nc("@info:progress", "Taking the songs out of the archive…"), false);

    auto message = QDBusMessage::createMethodCall(service.trimmed(), objectPath.trimmed(), u"org.kde.ark.DndExtract"_s, u"extractSelectedFilesTo"_s);
    message << temporary;
    auto *call = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);

    connect(call, &QDBusPendingCallWatcher::finished, this, [this, call, temporary, destination] {
        call->deleteLater();
        if (call->isError()) {
            qWarning() << "ElisaApplication::importFromArk" << "Ark did not hand out the songs:" << call->error().message();
            QDir{temporary}.removeRecursively();
            reportImport(1.0, i18nc("@info:status", "Could not take the songs out of the archive"), true);
            return;
        }

        // Ark writes the songs on its own time: wait until they stop growing
        struct Wait {
            int stableTicks = 0;
            int ticks = 0;
            qint64 lastSize = -1;
        };
        auto wait = std::make_shared<Wait>();
        auto *timer = new QTimer(this);
        timer->setInterval(400);
        connect(timer, &QTimer::timeout, this, [this, timer, wait, temporary, destination] {
            ++wait->ticks;
            const auto [files, size] = folderContents(temporary);
            if (files > 0 && size == wait->lastSize) {
                ++wait->stableTicks;
            } else {
                wait->stableTicks = 0;
            }
            wait->lastSize = size;

            const bool ready = files > 0 && wait->stableTicks >= 4;
            const bool gaveUp = files == 0 && wait->ticks >= 75; // 30 seconds without anything
            if (!ready && !gaveUp) {
                return;
            }
            timer->stop();
            timer->deleteLater();

            if (gaveUp) {
                QDir{temporary}.removeRecursively();
                reportImport(1.0, i18nc("@info:status", "Could not take the songs out of the archive"), true);
                return;
            }

            const bool sortMusic = d->mSortImportedMusic;
            const auto hidden = d->mBlacklist->hiddenFiles();
            const ImportReport report = [this](qreal progress, const QString &status, bool done) {
                reportImport(progress, status, done);
            };

            auto importFuture = QtConcurrent::run([temporary, destination, sortMusic, hidden, report] {
                int songs = 0;
                if (sortMusic) {
                    songs = sortIntoAlbumFolders(destination, {temporary}, hidden, report);
                } else {
                    FileScanner scanner;
                    QDirIterator it{temporary, QDir::Files, QDirIterator::Subdirectories};
                    while (it.hasNext()) {
                        if (scanner.shouldScanFile(it.next())) {
                            ++songs;
                        }
                    }
                }

                // what is still there (no sorting, or files that are not songs) goes into the music folder
                if (QFileInfo{temporary}.isDir()) {
                    const auto entries = QDir{temporary}.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
                    for (const auto &entry : entries) {
                        QDir{}.rename(temporary + u'/' + entry, uniquePath(destination + u'/' + entry));
                    }
                    QDir{temporary}.removeRecursively();
                }

                QString summary;
                if (songs == 0) {
                    summary = i18nc("@info:status", "No music among the dragged files");
                } else if (sortMusic) {
                    summary = i18ncp("@info:status", "Done: %1 song sorted into Artist/Album folders", "Done: %1 songs sorted into Artist/Album folders", songs);
                } else {
                    summary = i18ncp("@info:status", "Done: %1 song added to the music folder", "Done: %1 songs added to the music folder", songs);
                }
                report(1.0, summary, true);
            });
            Q_UNUSED(importFuture)
        });
        timer->start();
    });
#else
    Q_UNUSED(service)
    Q_UNUSED(objectPath)
#endif
}

bool ElisaApplication::importBusy() const
{
    return d->mImportBusy;
}

qreal ElisaApplication::importProgress() const
{
    return d->mImportProgress;
}

QString ElisaApplication::importStatus() const
{
    return d->mImportStatus;
}

bool ElisaApplication::importDone() const
{
    return d->mImportDone;
}

void ElisaApplication::dismissImportStatus()
{
    if (!d->mImportDone) {
        return;
    }
    setImportState(false, -1, {}, false);
}

void ElisaApplication::setImportState(bool busy, qreal progress, const QString &status, bool done)
{
    d->mImportBusy = busy;
    d->mImportProgress = progress;
    d->mImportStatus = status;
    d->mImportDone = done;
    Q_EMIT importStateChanged();
}

void ElisaApplication::sortMusicFolders()
{
    QList<std::pair<QString, QStringList>> work;

    const auto roots = Elisa::ElisaConfiguration::self()->rootPath();
    for (const auto &configuredRoot : roots) {
        const auto root = QDir::cleanPath(configuredRoot);
        work.push_back({root, QStringList{root}});
    }

    sortInBackground(work);
}

void ElisaApplication::sortInBackground(const QList<std::pair<QString, QStringList>> &work)
{
    if (work.isEmpty()) {
        return;
    }

    auto *watcher = new QFutureWatcher<MusicOrganizer::Result>(this);
    connect(watcher, &QFutureWatcher<MusicOrganizer::Result>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        Q_EMIT musicSorted(result.moved, result.failed);
        if (result.moved > 0 && d->mMusicManager) {
            d->mMusicManager->scanCollection(MusicListenersManager::CollectionScan::Soft);
        }
    });
    // hidden songs stay where they are, so they stay hidden
    const auto hidden = d->mBlacklist->hiddenFiles();
    setImportState(true, 0, i18nc("@info:progress", "Reading tags…"), false);
    const ImportReport report = [this](qreal progress, const QString &status, bool done) {
        if (done) {
            reportImport(progress, status, true);
        } else {
            // the bar only; the collection scan comes with the "finished" handler
            QMetaObject::invokeMethod(this, [this, progress, status] {
                setImportState(true, progress, status, false);
            }, Qt::QueuedConnection);
        }
    };
    watcher->setFuture(QtConcurrent::run([work, hidden, report] {
        MusicOrganizer::Result total;
        for (const auto &[root, items] : work) {
            const auto readTags = prepareTags(items, hidden, report);
            const auto result = MusicOrganizer::organize(root, items, readTags);
            total.moved += result.moved;
            total.alreadyInPlace += result.alreadyInPlace;
            total.failed += result.failed;
        }
        report(1.0, i18ncp("@info:status", "Done: %1 song sorted into Artist/Album folders", "Done: %1 songs sorted into Artist/Album folders", total.moved), true);
        return total;
    }));
}

QString ElisaApplication::adjustedTitle(const QString &title, int trackNumber) const
{
    return TrackNameCleaner::adjustedTitle(title, trackNumber);
}

QString ElisaApplication::fileNameOf(const QUrl &file) const
{
    return TrackNameCleaner::fileName(file);
}

SleepTimer *ElisaApplication::sleepTimer() const
{
    return d->mSleepTimer.get();
}

ElisaUtils::PlayListEntryType ElisaApplication::embeddedView() const
{
    ElisaUtils::PlayListEntryType result = ElisaUtils::Unknown;

    switch (Elisa::ElisaConfiguration::self()->embeddedView())
    {
    case 0:
        result = ElisaUtils::Unknown;
        break;
    case 1:
        result = ElisaUtils::Album;
        break;
    case 2:
        result = ElisaUtils::Artist;
        break;
    case 3:
        result = ElisaUtils::Genre;
        break;
    }

    return result;
}

int ElisaApplication::initialViewIndex() const
{
    int result = Elisa::ElisaConfiguration::initialView();

    return result;
}

QString ElisaApplication::initialFilesViewPath() const
{
    return Elisa::ElisaConfiguration::initialFilesViewPath();
}


#include "moc_elisaapplication.cpp"
