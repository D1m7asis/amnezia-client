#include "sshManagementTransport.h"

#ifdef Q_OS_WIN
    #include <winsock2.h>
    #include <ws2tcpip.h>

    #include <windows.h>

    #include <iphlpapi.h>
    #include <netioapi.h>
#endif

#include "version.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTcpServer>
#include <algorithm>

namespace amnezia
{
    SshManagementTransport::SshManagementTransport(const QString &executable) : m_executable(executable)
    {
        if (m_executable.isEmpty()) {
            m_executable = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral(SERVICE_NAME) + QStringLiteral(".exe"));
        }
    }

    SshManagementTransport::~SshManagementTransport()
    {
        stop();
    }

    bool SshManagementTransport::start(const ServerCredentials &credentials, int interfaceIndex)
    {
        stop();
#ifndef Q_OS_WIN
        return false;
#else
        QTcpServer reservation;
        if (!reservation.listen(QHostAddress::LocalHost, 0)) {
            return false;
        }
        m_port = reservation.serverPort();
        const auto config = credentials.sshManagement.forwardingConfig(credentials.hostName, credentials.port, m_port);
        if (config.isEmpty() || interfaceIndex <= 0) {
            stop();
            return false;
        }
        const QByteArray bytes = QJsonDocument(config).toJson(QJsonDocument::Compact);
        if (bytes.size() > 1024 * 1024) {
            stop();
            return false;
        }
        reservation.close();
        m_process = std::make_unique<QProcess>();
        m_process->setProgram(m_executable);
        m_process->setWorkingDirectory(QFileInfo(m_executable).absolutePath());
        m_process->setArguments({ QStringLiteral("--management-xray"),
                                  QString::number(QCoreApplication::applicationPid()), QString::number(interfaceIndex) });
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        m_process->start();
        if (!m_process->waitForStarted(5000)) {
            stop();
            return false;
        }
        if (m_process->write(bytes) != bytes.size()) {
            stop();
            return false;
        }
        m_process->closeWriteChannel();
        QElapsedTimer timeout;
        timeout.start();
        while (timeout.elapsed() < 5000 && isRunning()) {
            m_process->waitForReadyRead(100);
            readStatus();
            if (m_ready) {
                return true;
            }
        }
        stop();
        return false;
#endif
    }

    void SshManagementTransport::readStatus()
    {
        if (!m_process) {
            return;
        }
        m_status += m_process->readAllStandardOutput();
        m_process->readAllStandardError();
        if (m_status.size() > 8192) {
            m_bindingFailed = true;
            return;
        }
        int end = -1;
        while ((end = m_status.indexOf('\n')) >= 0) {
            const auto status = QJsonDocument::fromJson(m_status.left(end)).object();
            m_status.remove(0, end + 1);
            const auto event = status.value(QStringLiteral("event")).toString();
            if (event == QLatin1String("ready") && status.value(QStringLiteral("port")).toInt() == m_port) {
                m_ready = true;
            } else if (event == QLatin1String("bound")) {
                m_bound = true;
                m_bindingFailed |= !status.value(QStringLiteral("ok")).toBool();
            }
        }
    }

    void SshManagementTransport::stop()
    {
        if (m_process) {
            if (m_process->state() != QProcess::NotRunning) {
                m_process->kill();
                m_process->waitForFinished(2000);
            }
            m_process.reset();
        }
        m_status.clear();
        m_port = 0;
        m_ready = false;
        m_bound = false;
        m_bindingFailed = false;
    }

    bool SshManagementTransport::isRunning() const
    {
        return m_process && m_process->state() != QProcess::NotRunning;
    }

    bool SshManagementTransport::bindingOk()
    {
        readStatus();
        if (!m_bound && isRunning()) {
            m_process->waitForReadyRead(100);
            readStatus();
        }
        return isRunning() && m_bound && !m_bindingFailed;
    }

    int SshManagementTransport::localPort() const
    {
        return m_port;
    }

    QList<int> SshManagementTransport::uplinkCandidates()
    {
        QList<int> result;
#ifdef Q_OS_WIN
        ULONG size = 0;
        if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW) {
            return result;
        }
        QByteArray buffer(size, '\0');
        auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
        if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size) != NO_ERROR) {
            return result;
        }
        QList<QPair<ULONG, int>> candidates;
        for (auto *adapter = adapters; adapter; adapter = adapter->Next) {
            MIB_IF_ROW2 row {};
            row.InterfaceIndex = adapter->IfIndex;
            if (adapter->OperStatus == IfOperStatusUp && adapter->FirstGatewayAddress && adapter->FirstUnicastAddress
                && GetIfEntry2(&row) == NO_ERROR && row.InterfaceAndOperStatusFlags.HardwareInterface) {
                candidates.append({ adapter->Ipv4Metric, static_cast<int>(adapter->IfIndex) });
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
            return a.first != b.first ? a.first < b.first : a.second > b.second;
        });
        for (const auto &candidate : candidates) {
            result.append(candidate.second);
        }
#endif
        return result;
    }
}
