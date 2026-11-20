/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.elisa

/**
 * The right-click menu of albums and songs: the usual actions, the choice between
 * adjusted names and file names, hiding from Elisa and deleting from the computer.
 */
Item {
    id: root

    property var entryActions: []
    property bool isAlbum: false
    property bool canHide: false
    property var databaseId: 0
    property url fileUrl
    property string title
    property string artist
    property bool isArtist: false
    // the album of a song row, to delete the whole album from there
    property string album
    property string albumArtist
    // what the delete dialog is about: "song", "album", "songAlbum" or "artist"
    property string deleting: "song"

    signal notify(string message)

    function openFor(info) {
        entryActions = info.actions ? info.actions : []
        isAlbum = info.isAlbum === true
        canHide = info.canHide === true
        databaseId = info.databaseId ? info.databaseId : 0
        fileUrl = info.fileUrl ? info.fileUrl : ""
        title = info.title ? info.title : ""
        artist = info.artist ? info.artist : ""
        isArtist = info.isArtist === true
        album = info.album ? info.album : ""
        albumArtist = info.albumArtist ? info.albumArtist : ""
        menu.popup()
    }

    QQC2.Menu {
        id: menu

        // do not keep the actions of a row that may go away
        onClosed: root.entryActions = []

        Repeater {
            model: root.entryActions

            delegate: QQC2.MenuItem {
                required property var modelData

                action: modelData
                visible: modelData !== null && modelData.visible
                height: visible ? implicitHeight : 0
            }
        }

        QQC2.MenuSeparator {
            visible: !root.isAlbum
            height: visible ? implicitHeight : 0
        }

        QQC2.MenuItem {
            visible: !root.isAlbum
            height: visible ? implicitHeight : 0
            icon.name: "edit-rename"
            text: ElisaApplication.showFileNames
                  ? KI18n.i18nc("@action:inmenu", "Show Adjusted Names")
                  : KI18n.i18nc("@action:inmenu", "Show File Names")
            onTriggered: ElisaApplication.showFileNames = !ElisaApplication.showFileNames
        }

        QQC2.MenuSeparator {
            visible: root.canHide
            height: visible ? implicitHeight : 0
        }

        QQC2.MenuItem {
            visible: root.canHide
            height: visible ? implicitHeight : 0
            icon.name: "view-hidden"
            text: root.isAlbum
                  ? KI18n.i18nc("@action:inmenu", "Hide Album from Elisa")
                  : KI18n.i18nc("@action:inmenu", "Hide Song from Elisa")
            onTriggered: {
                if (root.isAlbum) {
                    ElisaApplication.blacklist.hideAlbum(root.databaseId, root.title, root.artist)
                } else {
                    ElisaApplication.blacklist.hideTrack(root.fileUrl, root.title, root.artist)
                }
                root.notify(KI18n.i18nc("@info:status", "“%1” is hidden. Bring it back in Settings, under Hidden music.", root.title))
            }
        }

        QQC2.MenuItem {
            visible: root.canHide
            height: visible ? implicitHeight : 0
            icon.name: "edit-delete"
            text: root.isAlbum
                  ? KI18n.i18nc("@action:inmenu", "Delete Album from This Computer…")
                  : KI18n.i18nc("@action:inmenu", "Delete Song from This Computer…")
            onTriggered: {
                root.deleting = root.isAlbum ? "album" : "song"
                deleteDialog.open()
            }
        }

        QQC2.MenuItem {
            visible: !root.isAlbum && root.canHide && root.album !== ""
            height: visible ? implicitHeight : 0
            icon.name: "edit-delete"
            text: KI18n.i18nc("@action:inmenu %1 album name", "Delete Whole Album “%1” from This Computer…", root.album)
            onTriggered: {
                root.deleting = "songAlbum"
                deleteDialog.open()
            }
        }

        QQC2.MenuSeparator {
            visible: root.isArtist
            height: visible ? implicitHeight : 0
        }

        QQC2.MenuItem {
            visible: root.isArtist
            height: visible ? implicitHeight : 0
            icon.name: "edit-delete"
            text: KI18n.i18nc("@action:inmenu", "Delete Artist from This Computer…")
            onTriggered: {
                root.deleting = "artist"
                deleteDialog.open()
            }
        }
    }

    Kirigami.PromptDialog {
        id: deleteDialog

        title: root.deleting === "artist" ? KI18n.i18nc("@title:window", "Delete Artist Files?")
             : root.deleting === "song" ? KI18n.i18nc("@title:window", "Delete Song File?")
             : KI18n.i18nc("@title:window", "Delete Album Files?")
        subtitle: root.deleting === "artist"
                  ? KI18n.i18nc("@info", "All song files of “%1” will be moved to the Trash and leave your library. You can restore them from the Trash.", root.title)
                  : root.deleting === "album"
                  ? KI18n.i18nc("@info", "All song files of the album “%1” will be moved to the Trash and leave your library. You can restore them from the Trash.", root.title)
                  : root.deleting === "songAlbum"
                  ? KI18n.i18nc("@info", "All song files of the album “%1” will be moved to the Trash and leave your library. You can restore them from the Trash.", root.album)
                  : KI18n.i18nc("@info", "The file of the song “%1” will be moved to the Trash and leave your library. You can restore it from the Trash.", root.title)
        standardButtons: Kirigami.Dialog.Cancel

        customFooterActions: [
            Kirigami.Action {
                text: KI18n.i18nc("@action:button", "Move to Trash")
                icon.name: "edit-delete"
                onTriggered: {
                    deleteDialog.close()
                    let failed = 0
                    let name = root.title
                    if (root.deleting === "artist") {
                        failed = ElisaApplication.blacklist.deleteArtistFiles(root.title)
                    } else if (root.deleting === "album") {
                        failed = ElisaApplication.blacklist.deleteAlbumFiles(root.databaseId)
                    } else if (root.deleting === "songAlbum") {
                        failed = ElisaApplication.blacklist.deleteAlbumFilesByName(root.album, root.albumArtist)
                        name = root.album
                    } else {
                        failed = ElisaApplication.blacklist.deleteTrackFile(root.fileUrl) ? 0 : 1
                    }
                    if (failed > 0) {
                        root.notify(KI18n.i18ncp("@info:status", "%1 file could not be moved to the Trash.", "%1 files could not be moved to the Trash.", failed))
                    } else {
                        root.notify(KI18n.i18nc("@info:status", "“%1” was moved to the Trash.", name))
                    }
                }
            }
        ]
    }
}
