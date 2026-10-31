#ifndef SSHMANAGEMENTTRANSPORT_H
#define SSHMANAGEMENTTRANSPORT_H

#include "core/utils/commonStructs.h"
#include <QList>
#include <QProcess>
#include <memory>

namespace amnezia
{
    class SshManagementTransport
    {
    public:
        explicit SshManagementTransport(const QString &executable = {});
        ~SshManagementTransport();
        bool start(const ServerCredentials &credentials, int interfaceIndex);
        void stop();
        bool isRunning() const;
        bool bindingOk();
        int localPort() const;
        static QList<int> uplinkCandidates();

    private:
        void readStatus();
        QString m_executable;
        std::unique_ptr<QProcess> m_process;
        QByteArray m_status;
        int m_port = 0;
        bool m_ready = false;
        bool m_bound = false;
        bool m_bindingFailed = false;
    };
}

#endif
