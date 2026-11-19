/*
   SPDX-License-Identifier: LGPL-3.0-or-later
 */

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.elisa

/**
 * Tells about a newer build of Elisa and installs it.
 */
Kirigami.InlineMessage {
    id: banner

    readonly property var updater: ElisaApplication.updateChecker
    readonly property int updateState: updater.state

    visible: updateState === UpdateChecker.UpToDate || updateState === UpdateChecker.UpdateAvailable
             || updateState === UpdateChecker.Downloading || updateState === UpdateChecker.Installing
             || updateState === UpdateChecker.ReadyToRestart || updateState === UpdateChecker.Failed

    type: updateState === UpdateChecker.Failed ? Kirigami.MessageType.Error
          : (updateState === UpdateChecker.ReadyToRestart || updateState === UpdateChecker.UpToDate
             ? Kirigami.MessageType.Positive : Kirigami.MessageType.Information)

    text: {
        switch (updateState) {
        case UpdateChecker.UpToDate:
            return KI18n.i18nc("@info", "Elisa is up to date (build %1).", updater.currentBuild)
        case UpdateChecker.UpdateAvailable:
            return KI18n.i18nc("@info", "A newer Elisa is available: build %1 (you have build %2).", updater.latestBuild, updater.currentBuild)
        case UpdateChecker.Downloading:
            return KI18n.i18nc("@info", "Downloading build %1… %2%", updater.latestBuild, Math.round(updater.progress * 100))
        case UpdateChecker.Installing:
            return KI18n.i18nc("@info", "Installing build %1…", updater.latestBuild)
        case UpdateChecker.ReadyToRestart:
            return KI18n.i18nc("@info", "Build %1 is installed. Restart Elisa to use it.", updater.latestBuild)
        case UpdateChecker.Failed:
            return updater.errorText
        }
        return ""
    }

    actions: [
        Kirigami.Action {
            text: KI18n.i18nc("@action:button", "Update Now")
            icon.name: "update-none"
            visible: banner.updateState === UpdateChecker.UpdateAvailable && banner.updater.canInstall
            onTriggered: banner.updater.installUpdate()
        },
        Kirigami.Action {
            text: KI18n.i18nc("@action:button", "Open Download Page")
            icon.name: "internet-web-browser"
            visible: (banner.updateState === UpdateChecker.UpdateAvailable && !banner.updater.canInstall) || banner.updateState === UpdateChecker.Failed
            onTriggered: banner.updater.openReleasePage()
        },
        Kirigami.Action {
            text: KI18n.i18nc("@action:button", "Restart Now")
            icon.name: "system-reboot"
            visible: banner.updateState === UpdateChecker.ReadyToRestart
            onTriggered: banner.updater.restart()
        },
        Kirigami.Action {
            text: banner.updateState === UpdateChecker.UpdateAvailable ? KI18n.i18nc("@action:button", "Later") : KI18n.i18nc("@action:button", "Close")
            icon.name: "dialog-close"
            visible: banner.updateState === UpdateChecker.UpdateAvailable || banner.updateState === UpdateChecker.UpToDate || banner.updateState === UpdateChecker.Failed
            onTriggered: banner.updater.dismiss()
        }
    ]
}
