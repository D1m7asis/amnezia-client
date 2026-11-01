#include "secureQSettings.h"
#include "version.h"
#include "settingsKeyMigration.h"
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QCryptographicHash>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#include <wincrypt.h>
#endif

#include "core/utils/utilities.h"
#include "cryptoUtils.h"
#include <QDataStream>
#include <QDebug>
#include <QEventLoop>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSharedPointer>
#include <QTimer>

using namespace QKeychain;

namespace {
    constexpr const char *settingsKeyTag = "settingsKeyTag";
    constexpr const char *settingsIvTag = "settingsIvTag";
#ifndef CLIENT_KEYCHAIN_NAME_STR
#define CLIENT_KEYCHAIN_NAME_STR "AmneziaVPN-Keychain"
#endif
    constexpr const char *keyChainName = CLIENT_KEYCHAIN_NAME_STR;
}

SecureQSettings::SecureQSettings(const QString &organization, const QString &application, QObject *parent, bool enableEncryption)
    : QObject { parent }, m_settings(organization, application, parent), encryptedKeys({ "Servers/serversList" }), m_encryptionEnabled(enableEncryption)
{
    bool encrypted = m_settings.value("Conf/encrypted").toBool();

    // convert settings to encrypted for if updated to >= 2.1.0
    if (encryptionRequired() && !encrypted) {
        for (const QString &key : m_settings.allKeys()) {
            if (encryptedKeys.contains(key)) {
                const QVariant &val = value(key);
                setValue(key, val);
            }
        }
        m_settings.setValue("Conf/encrypted", true);
    }
}

QVariant SecureQSettings::value(const QString &key, const QVariant &defaultValue) const
{
    QMutexLocker locker(&m_mutex);

    if (m_cache.contains(key)) {
        return m_cache.value(key);
    }

    if (!m_settings.contains(key))
        return defaultValue;

    QVariant retVal;

    // check if value is not encrypted, v. < 2.0.x
    retVal = m_settings.value(key);
    if (retVal.isValid()) {
        if (retVal.userType() == QMetaType::QByteArray && retVal.toByteArray().mid(0, magicString.size()) == magicString) {

            if (getEncKey().isEmpty() || getEncIv().isEmpty()) {
                qCritical() << "SecureQSettings::setValue Decryption requested, but key is empty";
                return {};
            }

            QByteArray encryptedValue = retVal.toByteArray().mid(magicString.size());

            QByteArray decryptedValue = decryptText(encryptedValue);
            QDataStream ds(&decryptedValue, QIODevice::ReadOnly);

            ds >> retVal;

            if (!retVal.isValid()) {
                qWarning() << "SecureQSettings::value settings decryption failed";
                retVal = QVariant();
            }
        }
    } else {
        qWarning() << "SecureQSettings::value invalid QVariant value";
        retVal = QVariant();
    }

    m_cache.insert(key, retVal);
    return retVal;
}

void SecureQSettings::setValue(const QString &key, const QVariant &value)
{
    QMutexLocker locker(&m_mutex);

    if (encryptionRequired() && encryptedKeys.contains(key)) {
        if (!getEncKey().isEmpty() && !getEncIv().isEmpty()) {
            QByteArray decryptedValue;
            {
                QDataStream ds(&decryptedValue, QIODevice::WriteOnly);
                ds << value;
            }

            QByteArray encryptedValue = encryptText(decryptedValue);
            if (encryptedValue.isEmpty()) {
                qCritical() << "Settings encryption failed; stored profile preserved";
                return;
            }
            m_settings.setValue(key, magicString + encryptedValue);
        } else {
            qCritical() << "SecureQSettings::setValue Encryption required, but key is empty";
            return;
        }

    } else {
        m_settings.setValue(key, value);
    }

    m_cache.insert(key, value);
}

void SecureQSettings::remove(const QString &key)
{
    QMutexLocker locker(&m_mutex);

    m_settings.remove(key);
    m_cache.remove(key);
}

QByteArray SecureQSettings::backupAppConfig() const
{
    QMutexLocker locker(&m_mutex);

    QJsonObject cfg;

    const auto needToBackup = [this](const auto &key) {
      for (const auto &item : m_fieldsToBackup)
      {
        if (key == "Conf/installationUuid")
        {
          return false;
        }

        if (key.startsWith(item))
        {
            return true;
        }
      }

      return false;
    };

    for (const QString &key : m_settings.allKeys()) {

        if (!needToBackup(key))
        {
            continue;
        }

        cfg.insert(key, QJsonValue::fromVariant(value(key)));
    }

    return QJsonDocument(cfg).toJson();
}

bool SecureQSettings::restoreAppConfig(const QByteArray &json)
{
    QMutexLocker locker(&m_mutex);

    QJsonObject cfg = QJsonDocument::fromJson(json).object();
    if (cfg.isEmpty())
        return false;

    for (const QString &key : cfg.keys()) {
        if (key == "Conf/installationUuid") {
            continue;
        }

        setValue(key, cfg.value(key).toVariant());
    }

    return true;
}

void SecureQSettings::clearSettings()
{
    QMutexLocker locker(&m_mutex);
    m_settings.clear();
    m_cache.clear();
}

QByteArray SecureQSettings::encryptText(const QByteArray &value) const
{
    QByteArray result = CryptoUtils::encryptAes256Cbc(value, getEncKey(), getEncIv());
    if (result.isEmpty() && !value.isEmpty()) {
        qCritical() << "error when encrypting the settings value";
    }
    return result;
}

QByteArray SecureQSettings::decryptText(const QByteArray &ba) const
{
    QByteArray result = CryptoUtils::decryptAes256Cbc(ba, getEncKey(), getEncIv());
    if (result.isEmpty() && !ba.isEmpty()) {
        qCritical() << "error when decrypting the settings value";
    }
    return result;
}

bool SecureQSettings::encryptionRequired() const
{
    if (!m_encryptionEnabled) {
        return false;
    }
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // QtKeyChain failing on Linux
    return false;
#endif
    return true;
}

QByteArray SecureQSettings::getEncKey() const
{

    if (!m_key.isEmpty()) {
        return m_key;
    }
#if defined(Q_OS_WIN) && D1MISVPN_WINDOWS_FORK
    return loadForkKeys() ? m_key : QByteArray();
#endif
    // load keys from system key storage
    m_key = getSecTag(settingsKeyTag);

    if (m_key.isEmpty()) {
        // Create new key
        QByteArray key = CryptoUtils::generateRandomBytes(32);
        if (key.isEmpty()) {
            qCritical() << "SecureQSettings::getEncKey Unable to generate new enc key";
        }

        setSecTag(settingsKeyTag, key);

        // check
        m_key = getSecTag(settingsKeyTag);
        if (key != m_key) {
            qCritical() << "SecureQSettings::getEncKey Unable to store key in keychain" << key.size() << m_key.size();
            return {};
        }
    }

    return m_key;
}

QByteArray SecureQSettings::getEncIv() const
{
    if (!m_iv.isEmpty()) {
        return m_iv;
    }
#if defined(Q_OS_WIN) && D1MISVPN_WINDOWS_FORK
    return loadForkKeys() ? m_iv : QByteArray();
#endif
    // load keys from system key storage
    m_iv = getSecTag(settingsIvTag);

    if (m_iv.isEmpty()) {
        // Create new IV
        QByteArray iv = CryptoUtils::generateRandomBytes(32);
        if (iv.isEmpty()) {
            qCritical() << "SecureQSettings::getEncIv Unable to generate new enc IV";
        }
        setSecTag(settingsIvTag, iv);

        // check
        m_iv = getSecTag(settingsIvTag);
        if (iv != m_iv) {
            qCritical() << "SecureQSettings::getEncIv Unable to store IV in keychain" << iv.size() << m_iv.size();
            return {};
        }
    }

    return m_iv;
}

QByteArray SecureQSettings::getSecTag(const QString &tag)
{
    auto job = QSharedPointer<ReadPasswordJob>(new ReadPasswordJob(keyChainName), &QObject::deleteLater);
    job->setAutoDelete(false);
    job->setKey(tag);
    QEventLoop loop;
    job->connect(job.data(), &ReadPasswordJob::finished, job.data(), [&loop]() { loop.quit(); });
    job->start();
    loop.exec();

    if (job->error()) {
        qCritical() << "SecureQSettings::getSecTag Error:" << job->errorString();
    }

    return job->binaryData();
}

void SecureQSettings::setSecTag(const QString &tag, const QByteArray &data)
{
    auto job = QSharedPointer<WritePasswordJob>(new WritePasswordJob(keyChainName), &QObject::deleteLater);
    job->setAutoDelete(false);
    job->setKey(tag);
    job->setBinaryData(data);
    QEventLoop loop;
    QTimer::singleShot(1000, &loop, SLOT(quit()));
    job->connect(job.data(), &WritePasswordJob::finished, job.data(), [&loop]() { loop.quit(); });
    job->start();
    loop.exec();

    if (job->error()) {
        qCritical() << "SecureQSettings::setSecTag Error:" << job->errorString();
    }
}

bool SecureQSettings::loadForkKeys() const
{
#if defined(Q_OS_WIN) && D1MISVPN_WINDOWS_FORK
    const QString target = QString("%1/%2/%3/settingsMaterial-v1")
        .arg(keyChainName, m_settings.organizationName(), m_settings.applicationName());
    auto read = [](const QString &tag) -> settingsKeys::Read {
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(reinterpret_cast<LPCWSTR>(tag.utf16()), CRED_TYPE_GENERIC, 0, &credential))
            return { GetLastError() == ERROR_NOT_FOUND ? settingsKeys::Status::Missing
                                                      : settingsKeys::Status::Error, {} };
        QByteArray data(reinterpret_cast<const char *>(credential->CredentialBlob), credential->CredentialBlobSize);
        CredFree(credential);
        return { settingsKeys::Status::Found, data };
    };
    const QByteArray stored = m_settings.value("Servers/serversList").toByteArray();
    const bool encrypted = stored.startsWith(magicString);
    if (m_settings.contains("Servers/serversList") && !encrypted
        && m_settings.value("Conf/encrypted").toBool()) {
        qCritical() << "Settings stage=encrypted-envelope invalid; refusing replacement keys";
        return false;
    }
    auto validate = [&](const QByteArray &material) {
        if (!encrypted)
            return true;
        const auto plain = CryptoUtils::decryptAes256Cbc(stored.mid(magicString.size()),
                                                       material.left(32), material.mid(32));
        QDataStream stream(plain);
        QVariant decoded;
        stream >> decoded;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(decoded.toByteArray(), &error);
        return stream.status() == QDataStream::Ok && decoded.metaType().id() == QMetaType::QByteArray
            && error.error == QJsonParseError::NoError && document.isArray();
    };
    auto backup = [&](const QByteArray &material) {
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream << QStringLiteral("D1m7asisVPN-settings-recovery-v1") << stored << material;
        DATA_BLOB input { static_cast<DWORD>(payload.size()), reinterpret_cast<BYTE *>(payload.data()) }, output {};
        if (!CryptProtectData(&input, L"D1m7asisVPN settings recovery", nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &output))
            return false;
        const auto protectedBytes = QByteArray(reinterpret_cast<char *>(output.pbData), output.cbData);
        LocalFree(output.pbData);
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
        if (!QDir().mkpath(directory))
            return false;
        const auto path = directory + "/settings-" + QCryptographicHash::hash(stored, QCryptographicHash::Sha256).toHex() + ".dpapi";
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(protectedBytes) != protectedBytes.size() || !file.commit())
            return false;
        QFile saved(path);
        if (!saved.open(QIODevice::ReadOnly))
            return false;
        auto bytes = saved.readAll();
        DATA_BLOB check { static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE *>(bytes.data()) }, decoded {};
        if (!CryptUnprotectData(&check, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &decoded))
            return false;
        const bool equal = QByteArray(reinterpret_cast<char *>(decoded.pbData), decoded.cbData) == payload;
        SecureZeroMemory(decoded.pbData, decoded.cbData);
        LocalFree(decoded.pbData);
        payload.fill(0);
        return equal;
    };
    auto write = [&](const QByteArray &material) {
        CREDENTIALW credential {};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
        credential.CredentialBlobSize = material.size();
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(material.constData()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        return CredWriteW(&credential, 0) != FALSE;
    };
    // Only the real legacy fork store may read the historical shared targets.
    const bool legacyStore = m_settings.organizationName() == "D1m7asis.VPN"
        && m_settings.applicationName() == "D1m7asisVPN";
    settingsKeys::Store store {
        [&]() { return read(target); },
        [&]() { return legacyStore ? read(settingsKeyTag) : settingsKeys::Read { settingsKeys::Status::Missing, {} }; },
        [&]() { return legacyStore ? read(settingsIvTag) : settingsKeys::Read { settingsKeys::Status::Missing, {} }; },
        validate, backup, write, []() { return CryptoUtils::generateRandomBytes(64); }
    };
    const auto material = settingsKeys::load(store, encrypted);
    if (material.size() != 64) {
        qCritical() << "Settings stage=credential-read-or-migration failed; profiles preserved";
        return false;
    }
    m_key = material.left(32);
    m_iv = material.mid(32);
    return true;
#else
    return false;
#endif
}

bool SecureQSettings::profileStoreReadable() const
{
    if (!m_settings.contains("Servers/serversList")) return true;
#if defined(Q_OS_WIN) && D1MISVPN_WINDOWS_FORK
    if (m_settings.value("Conf/encrypted").toBool()
        && !m_settings.value("Servers/serversList").toByteArray().startsWith(magicString))
        return false;
#endif
    const auto bytes = value("Servers/serversList");
    if (!bytes.isValid()) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes.toByteArray(), &error);
    return error.error == QJsonParseError::NoError && document.isArray();
}
