#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#endif
#include "xrayProtocol.h"

#include "core/protocols/protocolUtils.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/constants/protocolConstants.h"
#include "core/utils/ipcClient.h"
#include "core/utils/networkUtilities.h"
#if defined(Q_OS_WIN) && D1MISVPN_WINDOWS_FORK
#include "core/utils/windowsVpnConflict.h"
#endif
#include "core/utils/serialization/serialization.h"
#include "ipc.h"
#include "version.h"
#include "logger.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTimer>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QtCore/qlogging.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qprocess.h>

#include <exception>

#ifdef Q_OS_MACOS
static const QString tunName = "utun22";
#else
#if D1MISVPN_WINDOWS_FORK
static const QString tunName = "d1misxray";
#else
static const QString tunName = "tun2";
#endif
#endif

XrayProtocol::XrayProtocol(const QJsonObject &configuration, QObject *parent) : VpnProtocol(configuration, parent)
{
    m_vpnGateway = amnezia::protocols::xray::defaultLocalAddr;
    m_vpnLocalAddress = amnezia::protocols::xray::defaultLocalAddr;
    m_routeGateway = NetworkUtilities::getGatewayAndIface().first;

    m_routeMode = static_cast<amnezia::RouteMode>(configuration.value(amnezia::configKey::splitTunnelType).toInt());
    m_remoteAddress = NetworkUtilities::getIPAddress(m_rawConfig.value(amnezia::configKey::hostName).toString());

    const QString primaryDns = configuration.value(amnezia::configKey::dns1).toString();
    m_dnsServers.push_back(QHostAddress(primaryDns));
    if (primaryDns != amnezia::protocols::dns::amneziaDnsIp) {
        const QString secondaryDns = configuration.value(amnezia::configKey::dns2).toString();
        m_dnsServers.push_back(QHostAddress(secondaryDns));
    }

    QJsonObject xrayConfiguration = configuration.value(ProtocolUtils::key_proto_config_data(Proto::Xray)).toObject();
    if (xrayConfiguration.isEmpty()) {
        xrayConfiguration = configuration.value(ProtocolUtils::key_proto_config_data(Proto::SSXray)).toObject();
    }

    if (xrayConfiguration.isEmpty()) {
        qWarning() << "Xray config wrapper is empty";
        m_xrayConfig = {};
    }

    m_xrayConfig = QJsonDocument::fromJson(xrayConfiguration.value(amnezia::configKey::config).toString().toUtf8()).object();
    if (m_xrayConfig.isEmpty()) {
        qWarning() << "Xray config string is not a valid JSON object";
        m_xrayConfig = {};
    }
}

XrayProtocol::~XrayProtocol()
{
    qDebug() << "XrayProtocol::~XrayProtocol()";
    XrayProtocol::stop();
}

ErrorCode XrayProtocol::start()
{
    qDebug() << "XrayProtocol::start()";
    if (m_started)
        return ErrorCode::NoError;
#if defined(Q_OS_WIN) && D1MISVPN_WINDOWS_FORK
    if (windowsVpnConflict::hasActiveOfficialTunnel()) {
        return ErrorCode::ConflictingWindowsVpn;
    }
#endif
    m_tun2socksRetryCount = 0;
    m_routingSetup = false;

    // Inject SOCKS5 auth into the inbound before starting xray.
    // Re-uses existing credentials if the config already has them (e.g. imported config).
    amnezia::serialization::inbounds::InboundCredentials creds;
    try {
        creds = amnezia::serialization::inbounds::EnsureInboundAuth(m_xrayConfig);
    } catch (const std::exception &e) {
        qCritical() << "EnsureInboundAuth failed:" << e.what();
        return ErrorCode::InternalError;
    }
    m_socksUser = creds.username;
    m_socksPassword = creds.password;
    m_socksPort = creds.port;

    QString xrayConfigStr = QJsonDocument(m_xrayConfig).toJson(QJsonDocument::Compact);
    if (xrayConfigStr.isEmpty()) {
        qCritical() << "Xray config is empty";
        return ErrorCode::XrayExecutableCrashed;
    }

    // Fix fingerprint: old configs may contain "Mozilla/5.0" which xray-core rejects.
    // Replace with the correct default at runtime so stale stored configs still work.
    if (xrayConfigStr.contains("Mozilla/5.0", Qt::CaseInsensitive)) {
        xrayConfigStr.replace("Mozilla/5.0", amnezia::protocols::xray::defaultFingerprint,
                              Qt::CaseInsensitive);
        qDebug() << "XrayProtocol: patched legacy fingerprint to"
                 << amnezia::protocols::xray::defaultFingerprint;
    }

    // Fix inbound listen address: old configs may use "10.33.0.2" which doesn't exist
    // until TUN is created. xray must listen on 127.0.0.1 so tun2socks can connect.
    if (xrayConfigStr.contains(amnezia::protocols::xray::defaultLocalAddr)) {
        xrayConfigStr.replace(amnezia::protocols::xray::defaultLocalAddr,
                              amnezia::protocols::xray::defaultLocalListenAddr);
        qDebug() << "XrayProtocol: patched legacy inbound listen address to 127.0.0.1";
    }

    return IpcClient::withInterface(
            [&](QSharedPointer<IpcInterfaceReplica> iface) {
                auto xrayStart = iface->xrayStart(xrayConfigStr);
                if (!xrayStart.waitForFinished() || !xrayStart.returnValue()) {
                    qCritical() << "Failed to start xray";
                    return ErrorCode::XrayExecutableCrashed;
                }
                m_started = true;
                return startTun2Socks();
            },
            []() { return ErrorCode::AmneziaServiceConnectionFailed; });
}

void XrayProtocol::stop()
{
    qDebug() << "XrayProtocol::stop()";
    if (!m_started && !m_tun2socksProcess)
        return;
    ++m_generation;
    m_started = false;
    m_routingSetup = false;

    IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
        auto disableKillSwitch = iface->disableKillSwitch();
        if (!disableKillSwitch.waitForFinished() || !disableKillSwitch.returnValue())
            qWarning() << "Failed to disable killswitch";

        auto StartRoutingIpv6 = iface->StartRoutingIpv6();
        if (!StartRoutingIpv6.waitForFinished() || !StartRoutingIpv6.returnValue())
            qWarning() << "Failed to start routing ipv6";

        auto restoreResolvers = iface->restoreResolvers();
        if (!restoreResolvers.waitForFinished() || !restoreResolvers.returnValue())
            qWarning() << "Failed to restore resolvers";

        auto deleteTun = iface->deleteTun(tunName);
        if (!deleteTun.waitForFinished() || !deleteTun.returnValue())
            qWarning() << "Failed to delete tun";

        auto xrayStop = iface->xrayStop();
        if (!xrayStop.waitForFinished() || !xrayStop.returnValue())
            qWarning() << "Failed to stop xray";
    });

    if (m_tun2socksProcess) {
        m_tun2socksProcess->blockSignals(true);

#ifndef Q_OS_WIN
        m_tun2socksProcess->terminate();
        auto waitForFinished = m_tun2socksProcess->waitForFinished(1000);
        if (!waitForFinished.waitForFinished() || !waitForFinished.returnValue()) {
            qWarning() << "Failed to terminate tun2socks. Killing the process...";
            m_tun2socksProcess->kill();
        }
#else
        // terminate does not do anything useful on Windows
        // so just kill the process
        m_tun2socksProcess->kill();
#endif

        m_tun2socksProcess->close();
        m_tun2socksProcess.reset();
    }

    setConnectionState(Vpn::ConnectionState::Disconnected);
}

ErrorCode XrayProtocol::startTun2Socks()
{
    const auto generation = ++m_generation;
    m_tun2socksProcess = IpcClient::CreatePrivilegedProcess();
    if (!m_tun2socksProcess->waitForSource()) {
        return ErrorCode::AmneziaServiceConnectionFailed;
    }

    const QString proxyUrl = QString("socks5://%1:%2@127.0.0.1:%3").arg(m_socksUser, m_socksPassword, QString::number(m_socksPort));

    m_tun2socksProcess->setProgram(PermittedProcess::Tun2Socks);
    m_tun2socksProcess->setArguments({ "-device", QString("tun://%1").arg(tunName), "-proxy", proxyUrl });

    connect(
            m_tun2socksProcess.data(), &IpcProcessInterfaceReplica::readyReadStandardError, this, 
            [this, generation]() {
                if (!m_started || generation != m_generation) return;
                auto readAllStandardError = m_tun2socksProcess->readAllStandardError();
                if (!readAllStandardError.waitForFinished()) {
                    qWarning() << "Failed to read output from tun2socks";
                    return;
                }

                const QString line = readAllStandardError.returnValue();

                if (!line.contains("[TCP]") && !line.contains("[UDP]") && !line.contains("socks5://"))
                    qDebug() << "[tun2socks]:" << line;

                if (line.contains("[STACK] tun://") && line.contains("<-> socks5://")) {
                    if (!m_started || m_routingSetup)
                        return;
                    m_routingSetup = true;

                    if (ErrorCode res = setupRouting(); res != ErrorCode::NoError) {
                        stop();
                        setLastError(res);
                    } else {
                        setConnectionState(Vpn::ConnectionState::Connected);
                    }
                }
            },
            Qt::QueuedConnection);

    connect(
            m_tun2socksProcess.data(), &IpcProcessInterfaceReplica::finished, this,
            [this, generation](int exitCode, QProcess::ExitStatus exitStatus) {
                if (!m_started || generation != m_generation) return;
                // Check stdout for "resource busy" — the TUN device was not yet released
                // by the previous tun2socks instance. Retry after a short delay.
                bool resourceBusy = false;
                if (m_tun2socksProcess) {
                    auto readOut = m_tun2socksProcess->readAllStandardOutput();
                    if (readOut.waitForFinished()) {
                        resourceBusy = readOut.returnValue().contains("resource busy");
                    }
                }

                if (resourceBusy && m_tun2socksRetryCount < maxTun2SocksRetries) {
                    m_tun2socksRetryCount++;
                    qWarning() << QString("Tun2socks: TUN resource busy, retrying (%1/%2) in %3ms...")
                                      .arg(m_tun2socksRetryCount)
                                      .arg(maxTun2SocksRetries)
                                      .arg(tun2socksRetryDelayMs);
                    QTimer::singleShot(tun2socksRetryDelayMs, this, [this, generation]() {
                        if (!m_started || generation != m_generation)
                            return;
                        if (ErrorCode err = startTun2Socks(); err != ErrorCode::NoError) {
                            stop();
                            setLastError(err);
                        }
                    });
                    return;
                }

                m_tun2socksRetryCount = 0;

                if (exitStatus == QProcess::ExitStatus::CrashExit) {
                    qCritical() << "Tun2socks process crashed!";
                } else {
                    qCritical() << QString("Tun2socks process was closed with %1 exit code").arg(exitCode);
                }
                stop();
                setLastError(ErrorCode::Tun2SockExecutableCrashed);
            },
            Qt::QueuedConnection);

    connect(m_tun2socksProcess.data(), &IpcProcessInterfaceReplica::errorOccurred, this,
            [this, generation](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || !m_started || generation != m_generation) return;
                Logger::recordFailure("xray-helper-start", static_cast<int>(error));
                stop();
                setLastError(ErrorCode::Tun2SockExecutableCrashed);
            }, Qt::QueuedConnection);
    m_tun2socksProcess->start();
    return ErrorCode::NoError;
}

ErrorCode XrayProtocol::setupRouting()
{
    return IpcClient::withInterface(
            [this](QSharedPointer<IpcInterfaceReplica> iface) -> ErrorCode {
#ifdef Q_OS_WIN
                const int inetAdapterIndex = NetworkUtilities::AdapterIndexTo(QHostAddress(m_remoteAddress));
#endif
                auto createTun = iface->createTun(tunName, amnezia::protocols::xray::defaultLocalAddr);
                if (!createTun.waitForFinished() || !createTun.returnValue()) {
                    Logger::recordFailure("xray-tun-address", 106);
                    qCritical() << "Xray stage=tun-address failed";
                    return ErrorCode::XrayTunAddressFailed;
                }

                auto updateResolvers = iface->updateResolvers(tunName, m_dnsServers);
                if (!updateResolvers.waitForFinished() || !updateResolvers.returnValue()) {
                    Logger::recordFailure("xray-dns", 101);
                    qCritical() << "Failed to set DNS resolvers for TUN";
                    return ErrorCode::InternalError;
                }

#ifdef Q_OS_WIN
                NET_LUID tunLuid {};
                NET_IFINDEX tunIndex = 0;
                const DWORD resolve = ConvertInterfaceAliasToLuid(
                    reinterpret_cast<const wchar_t *>(tunName.utf16()), &tunLuid);
                const int vpnAdapterIndex = resolve == NO_ERROR
                    && ConvertInterfaceLuidToIndex(&tunLuid, &tunIndex) == NO_ERROR
                    ? static_cast<int>(tunIndex) : -1;
#else
                static const int vpnAdapterIndex = 0;
#endif
                const bool killSwitchEnabled = QVariant(m_rawConfig.value(configKey::killSwitchOption).toString()).toBool();
                if (killSwitchEnabled) {
                    if (vpnAdapterIndex != -1) {
                        QJsonObject config = m_rawConfig;
                        config.insert("vpnServer", m_remoteAddress);

                        auto enableKillSwitch = IpcClient::Interface()->enableKillSwitch(config, vpnAdapterIndex);
                        if (!enableKillSwitch.waitForFinished() || !enableKillSwitch.returnValue()) {
                            qCritical() << "Failed to enable killswitch";
                            return ErrorCode::InternalError;
                        }
                    } else
                        qWarning() << "Failed to get vpnAdapterIndex. Killswitch disabled";
                }

                if (m_routeMode == amnezia::RouteMode::VpnAllSites) {
                    static const QStringList subnets = { "1.0.0.0/8",  "2.0.0.0/7",  "4.0.0.0/6",  "8.0.0.0/5",
                                                         "16.0.0.0/4", "32.0.0.0/3", "64.0.0.0/2", "128.0.0.0/1" };

                    auto routeAddList = iface->routeAddList(m_vpnGateway, subnets);
                    if (!routeAddList.waitForFinished() || routeAddList.returnValue() != subnets.count()) {
                        Logger::recordFailure("xray-routes", 101);
                        qCritical() << "Failed to set routes for TUN";
                        return ErrorCode::InternalError;
                    }
                }

                auto StopRoutingIpv6 = iface->StopRoutingIpv6();
                if (!StopRoutingIpv6.waitForFinished() || !StopRoutingIpv6.returnValue()) {
                    qCritical() << "Failed to disable IPv6 routing";
                    return ErrorCode::InternalError;
                }

#ifdef Q_OS_WIN
                if (inetAdapterIndex != -1 && vpnAdapterIndex != -1) {
                    QJsonObject config = m_rawConfig;
                    config.insert("inetAdapterIndex", inetAdapterIndex);
                    config.insert("vpnAdapterIndex", vpnAdapterIndex);
                    config.insert("vpnGateway", m_vpnGateway);
                    config.insert("vpnServer", m_remoteAddress);

                    auto enablePeerTraffic = iface->enablePeerTraffic(config);
                    if (!enablePeerTraffic.waitForFinished() || !enablePeerTraffic.returnValue()) {
                        qCritical() << "Failed to enable peer traffic";
                        return ErrorCode::InternalError;
                    }
                } else
                    qWarning() << "Failed to get adapter indexes. Split-tunneling disabled";
#endif
                return ErrorCode::NoError;
            },
            []() { return ErrorCode::AmneziaServiceConnectionFailed; });
}
