#include "cryptoUtils.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

// Read only the UUID-scoped synthetic registry fixture supplied by the PS test.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2 || !app.arguments()[1].startsWith("D1m7asis.CodecTest."))
        return 2;
    QSettings settings(app.arguments()[1], "Fixture");
    const auto encoded = settings.value("Servers/serversList");
    if (encoded.metaType().id() != QMetaType::QByteArray)
        return 3;
    const auto data = encoded.toByteArray();
    if (!data.startsWith("EncData"))
        return 4;
    QByteArray material;
    for (int i = 1; i <= 64; ++i)
        material.append(char(i));
    const auto plain = CryptoUtils::decryptAes256Cbc(data.mid(7), material.left(32), material.mid(32));
    QDataStream stream(plain);
    QVariant value;
    stream >> value;
    if (stream.status() != QDataStream::Ok || value.metaType().id() != QMetaType::QByteArray)
        return 5;
    const auto document = QJsonDocument::fromJson(value.toByteArray());
    if (!document.isArray() || document.array().size() != 2)
        return 6;
    if (document.array()[0].toObject().value("description").toString() != "codec-one"
        || document.array()[1].toObject().value("description").toString() != QStringLiteral("codec-two\n\"quoted\"\\path"))
        return 7;
    return 0;
}
