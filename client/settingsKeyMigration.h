#pragma once
#include <QByteArray>
#include <functional>

namespace settingsKeys {
enum class Status { Found, Missing, Error };
struct Read { Status status; QByteArray data; };
struct Store {
    std::function<Read()> scoped;
    std::function<Read()> legacyKey;
    std::function<Read()> legacyIv;
    std::function<bool(const QByteArray &)> validate;
    std::function<bool(const QByteArray &)> backup;
    std::function<bool(const QByteArray &)> write;
    std::function<QByteArray()> generate;
};

inline QByteArray load(const Store &store, bool encryptedDataExists)
{
    auto scoped = store.scoped();
    if (scoped.status == Status::Error)
        return {};
    if (scoped.status == Status::Found)
        return scoped.data.size() == 64 && store.validate(scoped.data) ? scoped.data : QByteArray();
    QByteArray material;
    if (encryptedDataExists) {
        const auto key = store.legacyKey();
        const auto iv = store.legacyIv();
        if (key.status != Status::Found || iv.status != Status::Found
            || key.data.size() != 32 || iv.data.size() != 32)
            return {};
        material = key.data + iv.data;
        if (!store.validate(material) || !store.backup(material))
            return {};
    } else {
        material = store.generate();
    }
    if (material.size() != 64 || !store.write(material))
        return {};
    const auto verified = store.scoped();
    return verified.status == Status::Found && verified.data == material ? material : QByteArray();
}
}
