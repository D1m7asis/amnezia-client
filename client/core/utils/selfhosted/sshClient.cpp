#include "sshClient.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QEventLoop>
#include <QJsonDocument>
#include <QtConcurrent>
#include <cstring>

#include <fstream>

namespace libssh
{
    constexpr auto libsshTimeoutError { "Timeout connecting to" };

    std::function<QString()> Client::m_passphraseCallback;

    int Client::callback(const char *prompt, char *buf, size_t len, int echo, int verify, void *userdata)
    {
        if (!m_passphraseCallback || !buf || len == 0) {
            return SSH_ERROR;
        }
        const auto passphrase = m_passphraseCallback().toUtf8();
        if (static_cast<size_t>(passphrase.size()) >= len) {
            return SSH_ERROR;
        }
        std::memcpy(buf, passphrase.constData(), passphrase.size());
        buf[passphrase.size()] = '\0';
        return 0;
    }

    Client::Client(QObject *parent, const QString &managementExecutable)
        : QObject(parent), m_managementTransport(managementExecutable)
    {
    }

    Client::~Client()
    {
        disconnectFromHost();
    }

    ErrorCode Client::verifyHostKey(const ServerCredentials &credentials)
    {
        const QString expected = credentials.sshManagement.hostKeySha256;
        if (expected.isEmpty()) {
            const auto known = ssh_session_is_known_server(m_session);
            if (known == SSH_KNOWN_HOSTS_OK) {
                return ErrorCode::NoError;
            }
            return known == SSH_KNOWN_HOSTS_UNKNOWN || known == SSH_KNOWN_HOSTS_NOT_FOUND
                    ? ErrorCode::SshHostKeyUnknownError
                    : ErrorCode::SshHostKeyMismatchError;
        }
        ssh_key key = nullptr;
        unsigned char *hash = nullptr;
        size_t size = 0;
        if (ssh_get_server_publickey(m_session, &key) != SSH_OK) {
            return ErrorCode::SshInternalError;
        }
        const int result = ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &size);
        ssh_key_free(key);
        if (result != SSH_OK) {
            return ErrorCode::SshInternalError;
        }
        const auto actual = QStringLiteral("SHA256:")
                + QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(hash), static_cast<int>(size))
                                              .toBase64(QByteArray::OmitTrailingEquals));
        ssh_clean_pubkey_hash(&hash);
        return actual == expected ? ErrorCode::NoError : ErrorCode::SshHostKeyMismatchError;
    }

    ErrorCode Client::connectToHost(const ServerCredentials &credentials)
    {
        emit managementStageChanged(tr("Validating SSH management settings…"));
        if (!credentials.isValid()) {
            disconnectFromHost();
            return ErrorCode::SshManagementConfigError;
        }
        QByteArray identity;
        QDataStream stream(&identity, QIODevice::WriteOnly);
        stream << credentials.hostName << credentials.port << credentials.userName << credentials.secretData
               << QJsonDocument(credentials.sshManagement.toJson()).toJson(QJsonDocument::Compact);
        const auto fingerprint = QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
        const bool viaXray = credentials.sshManagement.transport == QLatin1String("xray");
        if (m_session && ssh_is_connected(m_session) && fingerprint == m_credentialsFingerprint
            && (!viaXray || m_managementTransport.isRunning())) {
            return ErrorCode::NoError;
        }
        disconnectFromHost();
        const auto fail = [this](ErrorCode code) {
            emit managementStageChanged(tr("SSH connection failed (error %1).").arg(static_cast<int>(code)));
            disconnectFromHost();
            return code;
        };
        const QList<int> interfaces = viaXray ? SshManagementTransport::uplinkCandidates() : QList<int> { 0 };
        ErrorCode lastError = ErrorCode::SshManagementTransportError;
        bool connected = false;
        for (int interfaceIndex : interfaces) {
            disconnectFromHost();
            emit managementStageChanged(viaXray ? tr("Starting Reality management transport…") : tr("Connecting to SSH…"));
            if (viaXray && !m_managementTransport.start(credentials, interfaceIndex)) {
                lastError = ErrorCode::SshManagementTransportError;
                continue;
            }
            m_session = ssh_new();
            if (!m_session) {
                return fail(ErrorCode::InternalError);
            }
            int port = viaXray ? m_managementTransport.localPort() : credentials.port;
            const auto host = (viaXray ? QStringLiteral("127.0.0.1") : credentials.hostName).toStdString();
            const auto username = credentials.userName.toStdString();
            int verbosity = SSH_LOG_NOLOG;
            long timeout = 12;
            if (ssh_options_set(m_session, SSH_OPTIONS_HOST, host.c_str()) != SSH_OK
                || ssh_options_set(m_session, SSH_OPTIONS_PORT, &port) != SSH_OK
                || ssh_options_set(m_session, SSH_OPTIONS_USER, username.c_str()) != SSH_OK
                || ssh_options_set(m_session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity) != SSH_OK
                || ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout) != SSH_OK) {
                return fail(ErrorCode::SshInternalError);
            }
            QFutureWatcher<int> watcher;
            QEventLoop wait;
            connect(&watcher, &QFutureWatcher<int>::finished, &wait, &QEventLoop::quit);
            watcher.setFuture(QtConcurrent::run([this] { return ssh_connect(m_session); }));
            wait.exec(QEventLoop::ExcludeUserInputEvents);
            if (watcher.result() != SSH_OK) {
                lastError = fromLibsshErrorCode();
                if (lastError == ErrorCode::NoError) {
                    lastError = ErrorCode::SshInterruptedError;
                }
                continue;
            }
            if (viaXray && !m_managementTransport.bindingOk()) {
                lastError = ErrorCode::SshManagementTransportError;
                continue;
            }
            emit managementStageChanged(tr("SSH transport reached; verifying host key…"));
            const auto keyError = verifyHostKey(credentials);
            if (keyError != ErrorCode::NoError) {
                return fail(keyError);
            }
            connected = true;
            break;
        }
        if (!connected) {
            return fail(lastError);
        }
        std::string authUsername = credentials.userName.toStdString();
        emit managementStageChanged(tr("SSH host key verified; authenticating…"));

        int authResult = SSH_ERROR;
        if (credentials.secretData.contains("BEGIN") && credentials.secretData.contains("PRIVATE KEY")) {
            ssh_key privateKey = nullptr;
            ssh_key publicKey = nullptr;
            authResult = ssh_pki_import_privkey_base64(credentials.secretData.toStdString().c_str(), nullptr, callback,
                                                       nullptr, &privateKey);
            if (authResult == SSH_OK) {
                authResult = ssh_pki_export_privkey_to_pubkey(privateKey, &publicKey);
            }

            if (authResult == SSH_OK) {
                authResult = ssh_userauth_try_publickey(m_session, authUsername.c_str(), publicKey);
            }

            if (authResult == SSH_OK) {
                authResult = ssh_userauth_publickey(m_session, authUsername.c_str(), privateKey);
            }

            if (publicKey) {
                ssh_key_free(publicKey);
            }
            if (privateKey) {
                ssh_key_free(privateKey);
            }
            if (authResult != SSH_OK) {
                qCritical() << ssh_get_error(m_session);
                ErrorCode errorCode = fromLibsshErrorCode();
                if (errorCode == ErrorCode::NoError) {
                    errorCode = ErrorCode::SshPrivateKeyFormatError;
                }
                return fail(errorCode);
            }
        } else {
            authResult =
                    ssh_userauth_password(m_session, authUsername.c_str(), credentials.secretData.toStdString().c_str());
            if (authResult != SSH_OK) {
                const auto error = fromLibsshErrorCode();
                return fail(error == ErrorCode::NoError ? ErrorCode::SshRequestDeniedError : error);
            }
        }
        m_credentialsFingerprint = fingerprint;
        emit managementStageChanged(tr("SSH authenticated."));
        return ErrorCode::NoError;
    }

    void Client::disconnectFromHost()
    {
        closeScpSession();
        if (m_session != nullptr) {
            if (ssh_is_connected(m_session)) {
                ssh_disconnect(m_session);
            }
            ssh_free(m_session);
            m_session = nullptr;
        }
        m_managementTransport.stop();
        m_credentialsFingerprint.clear();
    }

    ErrorCode Client::executeCommand(const QString &data,
                                        const std::function<ErrorCode (const QString &, Client &)> &cbReadStdOut,
                                        const std::function<ErrorCode (const QString &, Client &)> &cbReadStdErr)
    {
        if (!m_session || m_credentialsFingerprint.isEmpty() || !ssh_is_connected(m_session)
            || (m_managementTransport.localPort() > 0 && !m_managementTransport.isRunning())) {
            disconnectFromHost();
            return ErrorCode::SshInterruptedError;
        }
        m_channel = ssh_channel_new(m_session);

        if (m_channel == nullptr) {
            return closeChannel();
        }

        int result = ssh_channel_open_session(m_channel);

        if (result == SSH_OK && ssh_channel_is_open(m_channel)) {
            qDebug() << "SSH chanel opened";
        } else {
            return closeChannel();
        }

        QFutureWatcher<ErrorCode> watcher;
        connect(&watcher, &QFutureWatcher<ErrorCode>::finished, this, &Client::writeToChannelFinished);

        QFuture<ErrorCode> future = QtConcurrent::run([this, &data, &cbReadStdOut, &cbReadStdErr]() {
            const size_t bufferSize = 2048;

            int bytesRead = 0;
            char buffer[bufferSize];

            int result = ssh_channel_request_exec(m_channel, data.toUtf8());
            if (result == SSH_OK) {
                std::string output;
                auto readOutput = [&](bool isStdErr) {
                    bytesRead = ssh_channel_read(m_channel, buffer, sizeof(buffer), isStdErr);
                    while (bytesRead > 0)
                    {
                        output = std::string(buffer, bytesRead);
                        if (!output.empty()) {
                            if (cbReadStdOut && !isStdErr){
                                auto error = cbReadStdOut(output.c_str(), *this);
                                if (error != ErrorCode::NoError) {
                                    return error;
                                }
                            }
                            if (cbReadStdErr && isStdErr){
                                auto error = cbReadStdErr(output.c_str(), *this);
                                if (error != ErrorCode::NoError) {
                                    return error;
                                }
                            }
                        }
                        bytesRead = ssh_channel_read(m_channel, buffer, sizeof(buffer), isStdErr);
                    }
                    return ErrorCode::NoError;
                };

                auto errorCode = readOutput(false);
                if (errorCode != ErrorCode::NoError) {
                    return errorCode;
                }
                errorCode = readOutput(true);
                if (errorCode != ErrorCode::NoError) {
                    return errorCode;
                }
            } else {
                return closeChannel();
            }
            return closeChannel();
        });
        watcher.setFuture(future);

        QEventLoop wait;
        QObject::connect(this, &Client::writeToChannelFinished, &wait, &QEventLoop::quit);
        wait.exec(QEventLoop::ExcludeUserInputEvents);

        return watcher.result();
    }

    ErrorCode Client::writeResponse(const QString &data)
    {
        if (m_channel == nullptr) {
            qCritical() << "ssh channel not initialized";
            return fromLibsshErrorCode();
        }

        int bytesWritten = ssh_channel_write(m_channel, data.toUtf8(), (uint32_t)data.size());
        if (bytesWritten == data.size() && ssh_channel_write(m_channel, "\n", 1)) {
            return fromLibsshErrorCode();
        }
        return fromLibsshErrorCode();
    }

    ErrorCode Client::closeChannel()
    {
        if (m_channel != nullptr) {
            if (ssh_channel_is_eof(m_channel)) {
                ssh_channel_send_eof(m_channel);
            }
            if (ssh_channel_is_open(m_channel)) {
                ssh_channel_close(m_channel);
            }
            ssh_channel_free(m_channel);
            m_channel = nullptr;
        }
        return fromLibsshErrorCode();
    }

    ErrorCode Client::scpFileCopy(const ScpOverwriteMode overwriteMode, const QString& localPath, const QString& remotePath, const QString &fileDesc)
    {
        Q_UNUSED(overwriteMode)
        if (!m_session || m_credentialsFingerprint.isEmpty() || !ssh_is_connected(m_session)) {
            return ErrorCode::SshInterruptedError;
        }
        m_scpSession = ssh_scp_new(m_session, SSH_SCP_WRITE, remotePath.toStdString().c_str());

        if (m_scpSession == nullptr) {
            return fromLibsshErrorCode();
        }

        if (ssh_scp_init(m_scpSession) != SSH_OK) {
            auto errorCode = fromLibsshErrorCode();
            ssh_scp_free(m_scpSession);
            m_scpSession = nullptr;
            return errorCode;
        }

        QFutureWatcher<ErrorCode> watcher;
        connect(&watcher, &QFutureWatcher<ErrorCode>::finished, this, &Client::scpFileCopyFinished);
        QFuture<ErrorCode> future = QtConcurrent::run([this, &localPath, &remotePath, &fileDesc]() {
            const int accessType = 0600;
            const int localFileSize = QFileInfo(localPath).size();

            const auto fileName = QFileInfo(remotePath).fileName().toStdString();
            int result = ssh_scp_push_file(m_scpSession, fileName.c_str(), localFileSize, accessType);
            if (result != SSH_OK) {
                return fromLibsshErrorCode();
            }

            QFile fin(localPath);

            if (fin.open(QIODevice::ReadOnly)) {
                constexpr size_t bufferSize = 16384;
                int transferred = 0;
                int currentChunkSize = bufferSize;

                while (transferred < localFileSize) {

                    // Last Chunk
                    if ((localFileSize - transferred) < bufferSize) {
                        currentChunkSize = localFileSize % bufferSize;
                    }

                    QByteArray chunk = fin.read(currentChunkSize);
                    if (chunk.size() != currentChunkSize) {
                        return fromFileErrorCode(fin.error());
                    }

                    result = ssh_scp_write(m_scpSession, chunk.data(), chunk.size());
                    if (result != SSH_OK) {
                        return fromLibsshErrorCode();
                    }

                    transferred += currentChunkSize;
                }
            } else {
                return fromFileErrorCode(fin.error());
            }

            return ErrorCode::NoError;
        });
        watcher.setFuture(future);

        QEventLoop wait;
        QObject::connect(this, &Client::scpFileCopyFinished, &wait, &QEventLoop::quit);
        wait.exec(QEventLoop::ExcludeUserInputEvents);

        closeScpSession();
        return watcher.result();
    }

    void Client::closeScpSession()
    {
        if (m_scpSession != nullptr) {
            ssh_scp_close(m_scpSession);
            ssh_scp_free(m_scpSession);
            m_scpSession = nullptr;
        }
    }

    ErrorCode Client::fromLibsshErrorCode()
    {
        int errorCode = ssh_get_error_code(m_session);
        if (errorCode != SSH_NO_ERROR) {
            QString errorMessage = ssh_get_error(m_session);
            qCritical() << errorMessage;
            if (errorMessage.contains(libsshTimeoutError)) {
                return ErrorCode::SshTimeoutError;
            }
        }

        switch (errorCode) {
        case(SSH_NO_ERROR): return ErrorCode::NoError;
        case(SSH_REQUEST_DENIED): return ErrorCode::SshRequestDeniedError;
        case(SSH_EINTR): return ErrorCode::SshInterruptedError;
        case(SSH_FATAL): return ErrorCode::SshInternalError;
        default: return ErrorCode::SshInternalError;
        }
    }

    ErrorCode Client::fromFileErrorCode(QFileDevice::FileError fileError)
    {
        switch (fileError) {
        case QFileDevice::NoError: return ErrorCode::NoError;
        case QFileDevice::ReadError: return ErrorCode::ReadError;
        case QFileDevice::OpenError: return ErrorCode::OpenError;
        case QFileDevice::PermissionsError: return ErrorCode::PermissionsError;
        case QFileDevice::FatalError: return ErrorCode::FatalError;
        case QFileDevice::AbortError: return ErrorCode::AbortError;
        default: return ErrorCode::UnspecifiedError;
        }
    }

    ErrorCode Client::getDecryptedPrivateKey(const ServerCredentials &credentials, QString &decryptedPrivateKey, const std::function<QString()> &passphraseCallback)
    {
        int authResult = SSH_ERROR;
        ErrorCode errorCode = ErrorCode::NoError;

        ssh_key privateKey = nullptr;
        m_passphraseCallback = passphraseCallback;
        authResult = ssh_pki_import_privkey_base64(credentials.secretData.toStdString().c_str(), nullptr, callback, nullptr, &privateKey);
        if (authResult == SSH_OK) {
            char *b64 = nullptr;

            authResult = ssh_pki_export_privkey_base64(privateKey, nullptr, nullptr, nullptr, &b64);
            decryptedPrivateKey = QString(b64);

            if (authResult != SSH_OK) {
                qDebug() << "failed to export private key";
                errorCode = ErrorCode::InternalError;
            }
            else {
                ssh_string_free_char(b64);
            }
        } else {
            errorCode = ErrorCode::SshPrivateKeyError;
        }

        if (privateKey) {
            ssh_key_free(privateKey);
        }
        return errorCode;
    }
}
