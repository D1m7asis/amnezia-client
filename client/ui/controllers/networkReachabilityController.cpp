#include "networkReachabilityController.h"

#include <QNetworkInformation>
#include <QNetworkProxy>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

    bool reachabilityAllowsRemoteOperations(QNetworkInformation::Reachability r) {
        using R = QNetworkInformation::Reachability;
        // Unknown: no backend or not yet determined — do not block UI.
        return r == R::Online || r == R::Unknown;
    }

} // namespace

NetworkReachabilityController::NetworkReachabilityController(QObject *parent) : QObject(parent) {
    m_network.setProxy(QNetworkProxy::NoProxy);
    attachToNetworkInformation();
}

bool NetworkReachabilityController::hasInternetAccess() const {
    return m_hasInternetAccess;
}

void NetworkReachabilityController::resetVerification()
{
    ++m_generation;
    if (m_reply) {
        auto reply = m_reply;
        m_reply.clear();
        reply->abort();
        reply->deleteLater();
    }
    m_vpnInternetStatus = m_vpnConnected ? tr("VPN started; internet not verified") : QString();
    emit vpnInternetStatusChanged();
}

void NetworkReachabilityController::setVpnConnection(bool connected, const QString &expectedExitIp)
{
    if (m_vpnConnected == connected && m_expectedExitIp == expectedExitIp) {
        return;
    }
    m_vpnConnected = connected;
    m_expectedExitIp = expectedExitIp;
    resetVerification();
}

void NetworkReachabilityController::verifyVpnInternet()
{
    resetVerification();
    if (!m_vpnConnected) {
        return;
    }
    m_vpnInternetStatus = tr("Checking HTTPS and exit IP…");
    requestVerification(0, m_generation);
}

void NetworkReachabilityController::requestVerification(int stage, quint64 generation)
{
    const QUrl url(stage == 0 ? QStringLiteral("https://api.ipify.org?format=json")
                             : QStringLiteral("https://www.gstatic.com/generate_204"));
    QNetworkRequest request(url);
    request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto reply = m_network.get(request);
    m_reply = reply;
    emit vpnInternetStatusChanged();
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 8192) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, stage, generation] {
        reply->deleteLater();
        if (generation != m_generation) {
            return;
        }
        m_reply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != (stage == 0 ? 200 : 204)) {
            m_vpnInternetStatus = tr("Internet verification failed: HTTPS request did not succeed.");
        } else if (stage == 0) {
            const QString ip = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("ip")).toString();
            const QHostAddress actual(ip);
            const QHostAddress expected(m_expectedExitIp);
            if (actual.isNull()) {
                m_vpnInternetStatus = tr("Internet verification failed: invalid exit IP response.");
            } else if (!expected.isNull() && actual != expected) {
                m_vpnInternetStatus = tr("HTTPS works, but exit IP differs from the VPN server: %1").arg(ip);
            } else {
                requestVerification(1, generation);
                return;
            }
        } else {
            const QHostAddress expected(m_expectedExitIp);
            m_vpnInternetStatus = expected.isNull()
                ? tr("HTTPS internet verified; expected server IP is unknown.")
                : tr("HTTPS internet and server exit IP verified.");
        }
        emit vpnInternetStatusChanged();
    });
}

void NetworkReachabilityController::attachToNetworkInformation() {
    if (!QNetworkInformation::loadDefaultBackend()) {
        return;
    }
    QNetworkInformation *ni = QNetworkInformation::instance();
    if (!ni) {
        return;
    }
    const bool initial = reachabilityAllowsRemoteOperations(ni->reachability());
    const bool previous = m_hasInternetAccess;
    m_hasInternetAccess = initial;
    if (previous != m_hasInternetAccess) {
        emit hasInternetAccessChanged();
    }
    connect(ni, &QNetworkInformation::reachabilityChanged, this,
            [this](QNetworkInformation::Reachability r) {
                const bool ok = reachabilityAllowsRemoteOperations(r);
                if (ok == m_hasInternetAccess) {
                    return;
                }
                m_hasInternetAccess = ok;
                resetVerification();
                emit hasInternetAccessChanged();
            });
}
