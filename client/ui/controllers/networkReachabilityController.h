#ifndef NETWORKREACHABILITYCONTROLLER_H
#define NETWORKREACHABILITYCONTROLLER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>

// Exposes QNetworkInformation to QML for UI that must not run remote operations offline.
// Note: mozilla/networkwatcher.h has NetworkWatcher::getReachability() using the same API,
// but networkwatcher.cpp is not linked into the desktop client (only the service process).

class NetworkReachabilityController final : public QObject {
Q_OBJECT

    Q_PROPERTY(bool hasInternetAccess READ hasInternetAccess NOTIFY hasInternetAccessChanged)
    Q_PROPERTY(QString vpnInternetStatus READ vpnInternetStatus NOTIFY vpnInternetStatusChanged)
    Q_PROPERTY(bool checkingVpnInternet READ checkingVpnInternet NOTIFY vpnInternetStatusChanged)

public:
    explicit NetworkReachabilityController(QObject *parent = nullptr);

    bool hasInternetAccess() const;
    QString vpnInternetStatus() const { return m_vpnInternetStatus; }
    bool checkingVpnInternet() const { return !m_reply.isNull(); }
    Q_INVOKABLE void verifyVpnInternet();
    void setVpnConnection(bool connected, const QString &expectedExitIp);

signals:

    void hasInternetAccessChanged();
    void vpnInternetStatusChanged();

private:
    void attachToNetworkInformation();
    void requestVerification(int stage, quint64 generation);
    void resetVerification();

    bool m_hasInternetAccess = true;
    bool m_vpnConnected = false;
    QString m_expectedExitIp;
    QString m_vpnInternetStatus;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    quint64 m_generation = 0;
};

#endif // NETWORKREACHABILITYCONTROLLER_H
