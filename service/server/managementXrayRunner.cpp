#include "managementXrayRunner.h"
#include <QtGlobal>

#ifdef Q_OS_WIN
    #include <winsock2.h>
    #include <ws2tcpip.h>

    #include <windows.h>

    #include <iphlpapi.h>
    #include <netioapi.h>
#endif

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <amnezia_xray.h>
#include <cstdio>

namespace
{
    QMutex outputMutex;

    void status(const QJsonObject &value)
    {
        QMutexLocker lock(&outputMutex);
        const QByteArray line = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
        fwrite(line.constData(), 1, line.size(), stdout);
        fflush(stdout);
    }

#ifdef Q_OS_WIN
    void bindSocket(uintptr_t fd, void *context)
    {
        DWORD index = *static_cast<DWORD *>(context);
        DWORD networkIndex = htonl(index);
        WSAPROTOCOL_INFOW protocol {};
        int size = sizeof(protocol);
        int result = getsockopt(fd, SOL_SOCKET, SO_PROTOCOL_INFOW, reinterpret_cast<char *>(&protocol), &size);
        if (result == 0 && protocol.iAddressFamily == AF_INET) {
            result = setsockopt(fd, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char *>(&networkIndex),
                                sizeof(networkIndex));
        } else if (result == 0 && protocol.iAddressFamily == AF_INET6) {
            result = setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_IF, reinterpret_cast<char *>(&index), sizeof(index));
        } else {
            result = SOCKET_ERROR;
        }
        status({ { QStringLiteral("event"), QStringLiteral("bound") }, { QStringLiteral("ok"), result == 0 } });
    }
#endif

    bool succeeded(char *error)
    {
        if (!error) {
            return true;
        }
        amnezia_xray_free(error);
        return false;
    }
}

int runManagementXray(int argc, char **argv)
{
#ifndef Q_OS_WIN
    return 2;
#else
    QCoreApplication app(argc, argv);
    if (argc != 4) {
        return 2;
    }
    bool pidOk = false;
    bool indexOk = false;
    const DWORD parentPid = QString::fromLocal8Bit(argv[2]).toUInt(&pidOk);
    DWORD interfaceIndex = QString::fromLocal8Bit(argv[3]).toUInt(&indexOk);
    MIB_IF_ROW2 row {};
    row.InterfaceIndex = interfaceIndex;
    if (!pidOk || !indexOk || !parentPid || parentPid == GetCurrentProcessId() || !interfaceIndex
        || GetIfEntry2(&row) != NO_ERROR || row.OperStatus != IfOperStatusUp
        || !row.InterfaceAndOperStatusFlags.HardwareInterface) {
        return 2;
    }
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (!parent) {
        return 2;
    }
    QFile input;
    input.open(stdin, QIODevice::ReadOnly);
    const QByteArray bytes = input.read(1024 * 1024 + 1);
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    const auto config = document.object();
    const auto inbounds = config.value(QStringLiteral("inbounds")).toArray();
    const auto inbound = inbounds.isEmpty() ? QJsonObject {} : inbounds.first().toObject();
    const int port = inbound.value(QStringLiteral("port")).toInt();
    if (bytes.size() > 1024 * 1024 || parseError.error != QJsonParseError::NoError || inbounds.size() != 1
        || inbound.value(QStringLiteral("listen")).toString() != QLatin1String("127.0.0.1")
        || inbound.value(QStringLiteral("protocol")).toString() != QLatin1String("dokodemo-door") || port < 1
        || port > 65535) {
        CloseHandle(parent);
        return 2;
    }
    if (!succeeded(amnezia_xray_setsockcallback(bindSocket, &interfaceIndex))
        || !succeeded(amnezia_xray_configure(const_cast<char *>(bytes.constData()))) || !succeeded(amnezia_xray_start())) {
        CloseHandle(parent);
        return 3;
    }
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&app, parent] {
        if (WaitForSingleObject(parent, 0) != WAIT_TIMEOUT) {
            app.quit();
        }
    });
    timer.start(200);
    status({ { QStringLiteral("event"), QStringLiteral("ready") }, { QStringLiteral("port"), port } });
    const int result = app.exec();
    succeeded(amnezia_xray_stop());
    CloseHandle(parent);
    return result;
#endif
}
