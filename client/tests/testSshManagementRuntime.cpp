#include "core/models/selfhosted/selfHostedAdminServerConfig.h"
#include "core/utils/selfhosted/sshClient.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QUuid>

using namespace amnezia;

class TestSshManagementRuntime : public QObject
{
    Q_OBJECT
    ServerCredentials m_credentials;
    QString m_helper;

private slots:
    void initTestCase()
    {
        const QString path = qEnvironmentVariable("D1MISVPN_TEST_ADMIN_CONFIG");
        if (path.isEmpty()) {
            QSKIP("Live management tests require an explicitly supplied protected local configuration");
        }
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), "Cannot read protected test configuration");
        m_credentials =
                SelfHostedAdminServerConfig::fromJson(QJsonDocument::fromJson(file.readAll()).object()).credentials();
        QVERIFY2(m_credentials.isValid(), "Invalid test credentials or management configuration");
        m_helper = qEnvironmentVariable("D1MISVPN_TEST_HELPER");
        QVERIFY2(QFile::exists(m_helper), "Native helper executable does not exist");
    }

    void realCommandThroughPinnedReality()
    {
        libssh::Client client(nullptr, m_helper);
        QCOMPARE(client.connectToHost(m_credentials), ErrorCode::NoError);
        QString output;
        QCOMPARE(client.executeCommand(QStringLiteral("id -u"),
                                       [&output](const QString &text, libssh::Client &) {
                                           output += text;
                                           return ErrorCode::NoError;
                                       },
                                       {}),
                 ErrorCode::NoError);
        QCOMPARE(output.trimmed(), QStringLiteral("0"));
    }

    void changedPinCannotReuseAuthenticatedSession()
    {
        libssh::Client client(nullptr, m_helper);
        QCOMPARE(client.connectToHost(m_credentials), ErrorCode::NoError);
        auto wrong = m_credentials;
        wrong.sshManagement.hostKeySha256 = QStringLiteral("SHA256:")
                + QString::fromLatin1(QByteArray(32, 'x').toBase64(QByteArray::OmitTrailingEquals));
        QCOMPARE(client.connectToHost(wrong), ErrorCode::SshHostKeyMismatchError);
    }

    void invalidConfigurationClosesPreviousSession()
    {
        libssh::Client client(nullptr, m_helper);
        QCOMPARE(client.connectToHost(m_credentials), ErrorCode::NoError);
        auto invalid = m_credentials;
        invalid.sshManagement.transport = QStringLiteral("invalid");
        QCOMPARE(client.connectToHost(invalid), ErrorCode::SshManagementConfigError);
        QCOMPARE(client.executeCommand(QStringLiteral("id -u"), {}, {}), ErrorCode::SshInterruptedError);
    }

    void scpUploadKeepsPrivatePermissionsAndContent()
    {
        libssh::Client client(nullptr, m_helper);
        QCOMPARE(client.connectToHost(m_credentials), ErrorCode::NoError);
        const QString remote = QStringLiteral("/tmp/d1mis-scp-test-") + QUuid::createUuid().toString(QUuid::Id128);
        const auto cleanup = qScopeGuard([&] {
            libssh::Client remover(nullptr, m_helper);
            if (remover.connectToHost(m_credentials) == ErrorCode::NoError) {
                remover.executeCommand(QStringLiteral("rm -f -- ") + remote, {}, {});
            }
        });
        QTemporaryFile file;
        QVERIFY(file.open());
        const QByteArray payload(16384 + 31, 'z');
        QCOMPARE(file.write(payload), payload.size());
        QVERIFY(file.flush());
        QCOMPARE(client.scpFileCopy(libssh::ScpOverwriteExisting, file.fileName(), remote, {}), ErrorCode::NoError);
        QString output;
        QCOMPARE(client.executeCommand(QStringLiteral("sha256sum ") + remote,
                                       [&output](const QString &text, libssh::Client &) {
                                           output += text;
                                           return ErrorCode::NoError;
                                       },
                                       {}),
                 ErrorCode::NoError);
        QVERIFY(output.startsWith(
                QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())));
        output.clear();
        QCOMPARE(client.executeCommand(QStringLiteral("stat -c %a ") + remote,
                                       [&output](const QString &text, libssh::Client &) {
                                           output += text;
                                           return ErrorCode::NoError;
                                       },
                                       {}),
                 ErrorCode::NoError);
        QCOMPARE(output.trimmed(), QStringLiteral("600"));
        QCOMPARE(client.executeCommand(QStringLiteral("rm -f -- ") + remote, {}, {}), ErrorCode::NoError);
        output.clear();
        QCOMPARE(client.executeCommand(QStringLiteral("find ") + remote + QStringLiteral(" -maxdepth 0 -print"),
                                       [&output](const QString &text, libssh::Client &) {
                                           output += text;
                                           return ErrorCode::NoError;
                                       },
                                       {}),
                 ErrorCode::NoError);
        QVERIFY(output.isEmpty());
    }

    void occupiedPortIsRejected()
    {
        const auto interfaces = SshManagementTransport::uplinkCandidates();
        QVERIFY(!interfaces.isEmpty());
        QTcpServer occupied;
        QVERIFY(occupied.listen(QHostAddress::LocalHost, 0));
        QProcess helper;
        helper.start(m_helper,
                     { QStringLiteral("--management-xray"), QString::number(QCoreApplication::applicationPid()),
                       QString::number(interfaces.first()) });
        QVERIFY(helper.waitForStarted(3000));
        helper.write(QJsonDocument(m_credentials.sshManagement.forwardingConfig(
                                           m_credentials.hostName, m_credentials.port, occupied.serverPort()))
                             .toJson(QJsonDocument::Compact));
        helper.closeWriteChannel();
        QVERIFY(helper.waitForFinished(5000));
        QCOMPARE(helper.exitCode(), 3);
    }

    void stoppingTransportClosesItsListener()
    {
        const auto interfaces = SshManagementTransport::uplinkCandidates();
        QVERIFY(!interfaces.isEmpty());
        SshManagementTransport transport(m_helper);
        QVERIFY(transport.start(m_credentials, interfaces.first()));
        const int port = transport.localPort();
        transport.stop();
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(!probe.waitForConnected(500));
    }

    void helperExitsWhenItsParentDies()
    {
        const auto interfaces = SshManagementTransport::uplinkCandidates();
        QVERIFY(!interfaces.isEmpty());
        QTcpServer reservation;
        QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
        const int port = reservation.serverPort();
        reservation.close();
        QProcess parent;
        parent.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--parent-fixture") });
        QVERIFY(parent.waitForStarted(3000));
        QProcess helper;
        helper.start(m_helper,
                     { QStringLiteral("--management-xray"), QString::number(parent.processId()),
                       QString::number(interfaces.first()) });
        QVERIFY(helper.waitForStarted(3000));
        helper.write(QJsonDocument(m_credentials.sshManagement.forwardingConfig(m_credentials.hostName,
                                                                                m_credentials.port, port))
                             .toJson(QJsonDocument::Compact));
        helper.closeWriteChannel();
        QByteArray status;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000 && !status.contains("\"event\":\"ready\"")) {
            helper.waitForReadyRead(100);
            status += helper.readAllStandardOutput();
        }
        const bool ready = status.contains("\"event\":\"ready\"");
        parent.kill();
        parent.waitForFinished(2000);
        QVERIFY2(ready, "Native helper did not become ready");
        QVERIFY2(helper.waitForFinished(3000), "Helper survived its parent process");
        QCOMPARE(helper.exitCode(), 0);
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(!probe.waitForConnected(500));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--parent-fixture")) {
        QTimer::singleShot(30000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (ssh_init() != SSH_OK) {
        return 2;
    }
    TestSshManagementRuntime test;
    const int result = QTest::qExec(&test, argc, argv);
    ssh_finalize();
    return result;
}

#include "testSshManagementRuntime.moc"
