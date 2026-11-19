/*
   SPDX-FileCopyrightText: 2016 (c) Matthieu Gallien <matthieu_gallien@yahoo.fr>
   SPDX-FileCopyrightText: 2018 (c) Alexander Stippich <a.stippich@gmx.net>

   SPDX-License-Identifier: LGPL-3.0-or-later
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import org.kde.elisa
import org.kde.ki18n
import org.kde.kirigami as Kirigami

Menu {
    id: applicationMenu
    title: KI18n.i18nc("@title:menu", "Application Menu")

    Action {
        text: mainWindow.visibility == Window.FullScreen ? KI18n.i18nc("@action:inmenu", "Exit Full Screen") : KI18n.i18nc("@action:inmenu", "Enter Full Screen")
        icon.name: mainWindow.visibility == Window.FullScreen ? "view-restore" : "view-fullscreen"
        onTriggered: mainWindow.visibility == Window.FullScreen ? mainWindow.restorePreviousStateBeforeFullScreen() : mainWindow.goFullScreen()
    }

    Kirigami.Action {
        readonly property bool togglesDrawer: mainWindow.width < Theme.viewSelectorSmallSizeThreshold
        readonly property bool playlistVisible: togglesDrawer ? playlistDrawer.visible : contentView.showPlaylist

        text: playlistVisible
            ? KI18n.i18nc("@action:inmenu", "Hide Playlist")
            : KI18n.i18nc("@action:inmenu", "Show Playlist")
        icon.name: "view-media-playlist"

        fromQAction: ElisaApplication.action("toggle_playlist")
    }

    MenuSeparator {}

    Action {
        text: KI18n.i18nc("@action:inmenu", "Scan for New Music")
        icon.name: "view-refresh"
        onTriggered: {
            applicationMenu.close()
            ElisaApplication.musicManager.scanCollection(MusicListenersManager.Soft)
        }
    }

    Action {
        text: KI18n.i18nc("@action:inmenu", "Reset Database and Re-Scan Everything")
        icon.name: "edit-clear-all"
        onTriggered: {
            applicationMenu.close()
            ElisaApplication.musicManager.scanCollection(MusicListenersManager.Hard)
        }
    }

    MenuSeparator {}

    Action {
        id: darkModeAction
        text: KI18n.i18nc("@action:inmenu", "Dark Mode")
        icon.name: "weather-clear-night"
        checkable: true
        checked: ElisaApplication.darkMode
        onTriggered: {
            ElisaApplication.darkMode = checked
            // clicking breaks the binding; restore it so the tick follows the real colors
            darkModeAction.checked = Qt.binding(() => ElisaApplication.darkMode)
            applicationMenu.close()
        }
    }

    Menu {
        id: sleepTimerMenu

        readonly property var timer: ElisaApplication.sleepTimer

        title: timer.active
               ? KI18n.i18ncp("@action:inmenu this has child menu items", "Sleep Timer (%1 minute left)", "Sleep Timer (%1 minutes left)", timer.remainingMinutes)
               : (timer.stopAfterCurrentTrack
                  ? KI18n.i18nc("@action:inmenu this has child menu items", "Sleep Timer (end of track)")
                  : KI18n.i18nc("@action:inmenu this has child menu items", "Sleep Timer"))
        icon.name: "chronometer"

        MenuItem {
            text: KI18n.i18nc("@action:inmenu switch the sleep timer off", "Off")
            checkable: true
            checked: !sleepTimerMenu.timer.active && !sleepTimerMenu.timer.stopAfterCurrentTrack
            onTriggered: {
                sleepTimerMenu.timer.cancel()
                sleepTimerMenu.timer.stopAfterCurrentTrack = false
                applicationMenu.close()
            }
        }

        Repeater {
            model: [15, 30, 45, 60, 90]
            delegate: MenuItem {
                required property int modelData

                text: KI18n.i18ncp("@action:inmenu pause playback after this many minutes", "%1 minute", "%1 minutes", modelData)
                onTriggered: {
                    sleepTimerMenu.timer.stopAfterCurrentTrack = false
                    sleepTimerMenu.timer.start(modelData)
                    applicationMenu.close()
                }
            }
        }

        MenuItem {
            text: KI18n.i18nc("@action:inmenu pause playback when the current track ends", "End of Current Track")
            checkable: true
            checked: sleepTimerMenu.timer.stopAfterCurrentTrack
            onTriggered: {
                sleepTimerMenu.timer.cancel()
                sleepTimerMenu.timer.stopAfterCurrentTrack = true
                applicationMenu.close()
            }
        }
    }

    Menu {
        title: KI18n.i18nc("@action:inmenu this has child menu items", "Color Scheme")
        Repeater {
            model: ElisaApplication.colorSchemesModel
            delegate: MenuItem {
                required property var model

                icon.source: "image://colorScheme/" + model.display
                icon.color: "transparent"
                text: model.display
                checkable: true
                checked: {
                    if (model.index === 0) {
                        return ElisaApplication.activeColorSchemeName === "";
                    }

                    return model.display === ElisaApplication.activeColorSchemeName;
                }
                autoExclusive: true
                onTriggered: {
                    ElisaApplication.activeColorSchemeName = model.display
                    applicationMenu.close()
                }
            }
        }
    }

    Action {
        text: KI18n.i18nc("@action:inmenu", "Check for Updates")
        icon.name: "update-none"
        onTriggered: {
            applicationMenu.close()
            ElisaApplication.updateChecker.check(true)
        }
    }

    Action {
        text: KI18n.i18nc("@action:inmenu", "Sort Music into Artist/Album Folders…")
        icon.name: "folder-music"
        onTriggered: {
            applicationMenu.close()
            mainWindow.askToSortMusic()
        }
    }

    Kirigami.Action {
        fromQAction: ElisaApplication.action("options_configure")
    }

    Kirigami.Action {
        fromQAction: ElisaApplication.action("options_configure_keybinding")
    }

    MenuSeparator {}

    Kirigami.Action {
        fromQAction: ElisaApplication.action("help_about_app")
    }

    Kirigami.Action {
        fromQAction: ElisaApplication.action("help_about_kde")
    }

    Kirigami.Action {
        fromQAction: ElisaApplication.action("help_report_bug")
    }

    MenuSeparator {
        visible: ElisaApplication.showSystemTrayIcon
    }

    MenuItem {
        action: Kirigami.Action {
            fromQAction: ElisaApplication.action("file_quit")
        }
        visible: ElisaApplication.showSystemTrayIcon
    }
}
