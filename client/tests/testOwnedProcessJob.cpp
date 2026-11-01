#include "../../ipc/ownedprocessjob.h"
#include <QProcess>
#include <QtTest>

class TestOwnedProcessJob : public QObject {
    Q_OBJECT
private slots:
    void terminatesOnlyAttachedHelper()
    {
        QProcess owned, other;
        const QString shell = qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0/powershell.exe";
        const QStringList args { "-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Seconds 30" };
        other.start(shell, args);
        QVERIFY(other.waitForStarted());
        {
            OwnedProcessJob job;
            QVERIFY(job.ready());
            job.bind(owned);
            owned.start(shell, args);
            QVERIFY(owned.waitForStarted());
        }
        const bool ownedStopped = owned.waitForFinished(3000);
        const bool foreignRunning = !other.waitForFinished(100);
        other.kill();
        other.waitForFinished();
        QVERIFY(ownedStopped);
        QVERIFY(foreignRunning);
    }
};
QTEST_GUILESS_MAIN(TestOwnedProcessJob)
#include "testOwnedProcessJob.moc"
