import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import SortFilterProxyModel 0.2

import PageEnum 1.0
import Style 1.0

import "../Controls2"
import "../Controls2/TextTypes"
import "../Components"
import "../Config"

PageType {
    id: root

    signal lastItemTabClickedSignal()

    property bool isServerWithWriteAccess: ServersUiController.isProcessedServerHasWriteAccess()

    Connections {
        target: InstallController

        function onScanServerFinished(isInstalledContainerFound) {
            var message = ""
            if (isInstalledContainerFound) {
                message = qsTr("All installed containers have been added to the application")
            } else {
                message = qsTr("No new installed containers found")
            }

            PageController.showErrorMessage(message)
        }

        function onRebootServerFinished(finishedMessage) {
            PageController.showNotificationMessage(finishedMessage)
        }
    }

    Connections {
        target: SettingsController
        function onChangeSettingsFinished(finishedMessage) {
            PageController.showNotificationMessage(finishedMessage)
        }
    }

    Connections {
        target: ServersUiController

        function onSshManagementStatusChanged() {
            managementDialog.statusText = ServersUiController.sshManagementStatus
        }

        function onProcessedServerIdChanged() {
            root.isServerWithWriteAccess = ServersUiController.isProcessedServerHasWriteAccess()
        }
    }

    ListViewType {
        id: listView

        anchors.fill: parent

        model: serverActions

        delegate: ColumnLayout {
            width: listView.width

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: isVisible

                text: title
                descriptionText: description
                textColor: tColor

                clickedFunction: function() {
                    clickedHandler()
                }
            }

            DividerType {
                visible: isVisible
            }
        }
    }

    property list<QtObject> serverActions: [
        management,
        check,
        reboot,
        remove,
        clear,
        reset,
    ]

    QtObject {
        id: management
        property bool isVisible: root.isServerWithWriteAccess
        readonly property string title: qsTr("SSH management")
        readonly property string description: qsTr("Direct SSH or an independent Xray Reality transport")
        readonly property var tColor: AmneziaStyle.color.paleGray
        readonly property var clickedHandler: function() {
            const settings = ServersUiController.sshManagementSettings()
            transport.currentIndex = settings.transport === "xray" ? 1 : 0
            sshHost.text = settings.targetHost || ""
            sshPort.text = String(settings.targetPort || 0)
            hostPin.text = settings.hostKeySha256 || ""
            managementDialog.profileFile = ""
            managementDialog.statusText = settings.hasProfile ? qsTr("Reality profile stored securely") : qsTr("No Reality profile imported")
            managementDialog.open()
        }
    }

    FileDialog {
        id: realityFile
        options: Qt.platform.os === "windows" && Qt.application.name === "D1m7asisVPN"
                 ? FileDialog.DontUseNativeDialog : 0
        title: qsTr("Import Reality client JSON")
        nameFilters: ["JSON (*.json)"]
        onAccepted: {
            managementDialog.profileFile = selectedFile
            managementDialog.statusText = qsTr("New profile selected; save to apply")
        }
    }

    Dialog {
        id: managementDialog
        property url profileFile
        property string statusText
        title: qsTr("SSH management")
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 24, 480)
        height: Math.min(root.height - 24, 620)
        standardButtons: Dialog.Close
        contentItem: ScrollView {
            clip: true
            ColumnLayout {
                width: managementDialog.availableWidth
                spacing: 10
                ComboBox {
                    id: transport
                    Layout.fillWidth: true
                    model: [qsTr("Direct SSH"), qsTr("SSH through Xray Reality")]
                }
                Label { text: qsTr("SSH destination (empty = server address)"); wrapMode: Text.Wrap; Layout.fillWidth: true }
                TextField { id: sshHost; Layout.fillWidth: true; selectByMouse: true; enabled: transport.currentIndex === 1 }
                Label { text: qsTr("SSH port (0 = original port)") }
                TextField { id: sshPort; Layout.fillWidth: true; enabled: transport.currentIndex === 1; validator: IntValidator { bottom: 0; top: 65535 } }
                Label { text: qsTr("Trusted SSH fingerprint (SHA256:…)"); wrapMode: Text.Wrap; Layout.fillWidth: true }
                TextField { id: hostPin; Layout.fillWidth: true; selectByMouse: true }
                Button {
                    text: qsTr("Import Reality JSON")
                    visible: transport.currentIndex === 1
                    onClicked: realityFile.open()
                }
                Label { text: managementDialog.statusText; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Button {
                    text: qsTr("Save")
                    enabled: sshPort.acceptableInput
                    onClicked: {
                        if (ServersUiController.saveSshManagement(transport.currentIndex === 1 ? "xray" : "direct",
                                sshHost.text, Number(sshPort.text), hostPin.text, managementDialog.profileFile)) {
                            managementDialog.profileFile = ""
                            managementDialog.statusText = qsTr("Settings saved. Run SSH verification.")
                        }
                    }
                }
                Button {
                    text: qsTr("Verify saved SSH settings")
                    onClicked: {
                        enabled = false
                        PageController.showBusyIndicator(true)
                        managementDialog.statusText = ServersUiController.testSshManagement()
                        PageController.showBusyIndicator(false)
                        enabled = true
                    }
                }
            }
        }
    }

    QtObject {
        id: check

        property bool isVisible: root.isServerWithWriteAccess
        readonly property string title: qsTr("Check the server for previously installed Amnezia services")
        readonly property string description: qsTr("Add them to the application if they were not displayed")
        readonly property var tColor: AmneziaStyle.color.paleGray
        readonly property var clickedHandler: function() {
            PageController.showBusyIndicator(true)
            InstallController.scanServerForInstalledContainers(ServersUiController.processedServerId)
            PageController.showBusyIndicator(false)
        }
    }

    QtObject {
        id: reboot

        property bool isVisible: root.isServerWithWriteAccess
        readonly property string title: qsTr("Reboot server")
        readonly property string description: ""
        readonly property var tColor: AmneziaStyle.color.vibrantRed
        readonly property var clickedHandler: function() {
            var headerText = qsTr("Do you want to reboot the server?")
            var descriptionText = qsTr("The reboot process may take approximately 30 seconds. Are you sure you wish to proceed?")
            var yesButtonText = qsTr("Continue")
            var noButtonText = qsTr("Cancel")

            var yesButtonFunction = function() {
                if (ServersUiController.isDefaultServerCurrentlyProcessed() && ConnectionController.isConnected) {
                    PageController.showNotificationMessage(qsTr("Cannot reboot server during active connection"))
                } else {
                    PageController.showBusyIndicator(true)
                    InstallController.rebootServer(ServersUiController.processedServerId)
                    PageController.showBusyIndicator(false)
                }
            }
            var noButtonFunction = function() {

            }

            showQuestionDrawer(headerText, descriptionText, yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
        }
    }

    QtObject {
        id: remove

        property bool isVisible: true
        readonly property string title: qsTr("Remove server from application")
        readonly property string description: ""
        readonly property var tColor: AmneziaStyle.color.vibrantRed
        readonly property var clickedHandler: function() {
            var headerText = qsTr("Do you want to remove the server from application?")
            var descriptionText = qsTr("All installed AmneziaVPN services will still remain on the server.")
            var yesButtonText = qsTr("Continue")
            var noButtonText = qsTr("Cancel")

            var yesButtonFunction = function() {
                if (ServersUiController.isDefaultServerCurrentlyProcessed() && ConnectionController.isConnected) {
                    PageController.showNotificationMessage(qsTr("Cannot remove server during active connection"))
                } else {
                    PageController.showBusyIndicator(true)
                    InstallController.removeServer(ServersUiController.processedServerId)
                    PageController.showBusyIndicator(false)
                }
            }
            var noButtonFunction = function() {

            }

            showQuestionDrawer(headerText, descriptionText, yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
        }
    }

    QtObject {
        id: clear

        property bool isVisible: root.isServerWithWriteAccess
        readonly property string title: qsTr("Clear server from Amnezia software")
        readonly property string description: ""
        readonly property var tColor: AmneziaStyle.color.vibrantRed
        readonly property var clickedHandler: function() {
            var headerText = qsTr("Do you want to clear server from Amnezia software?")
            var descriptionText = qsTr("All users whom you shared a connection with will no longer be able to connect to it.")
            var yesButtonText = qsTr("Continue")
            var noButtonText = qsTr("Cancel")

            var yesButtonFunction = function() {
                if (ServersUiController.isDefaultServerCurrentlyProcessed() && ConnectionController.isConnected) {
                    PageController.showNotificationMessage(qsTr("Cannot clear server from Amnezia software during active connection"))
                } else {
                    PageController.goToPage(PageEnum.PageDeinstalling)
                    InstallController.removeAllContainers(ServersUiController.processedServerId)
                }
            }
            var noButtonFunction = function() {

            }

            showQuestionDrawer(headerText, descriptionText, yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
        }
    }

    QtObject {
        id: reset

        property bool isVisible: ServersUiController.isServerFromApi(ServersUiController.processedServerId)
        readonly property string title: qsTr("Reset API config")
        readonly property string description: ""
        readonly property var tColor: AmneziaStyle.color.vibrantRed
        readonly property var clickedHandler: function() {
            var headerText = qsTr("Do you want to reset API config?")
            var descriptionText = ""
            var yesButtonText = qsTr("Continue")
            var noButtonText = qsTr("Cancel")

            var yesButtonFunction = function() {
                if (ServersUiController.isDefaultServerCurrentlyProcessed() && ConnectionController.isConnected) {
                    PageController.showNotificationMessage(qsTr("Cannot reset API config during active connection"))
                } else {
                    PageController.showBusyIndicator(true)
                    SubscriptionUiController.removeApiConfig(ServersUiController.processedServerId)
                    PageController.showBusyIndicator(false)
                }
            }
            var noButtonFunction = function() {

            }

            showQuestionDrawer(headerText, descriptionText, yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
        }
    }

}
