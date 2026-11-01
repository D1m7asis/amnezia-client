#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <functional>
#include <QString>

// Inject the Windows calls so reconnect and timeout paths can be tested without
// assigning an address or changing the user's network.
struct WindowsTunAddressApi {
    std::function<DWORD(const wchar_t *, NET_LUID *)> resolve = ConvertInterfaceAliasToLuid;
    std::function<DWORD(MIB_UNICASTIPADDRESS_ROW *)> query = GetUnicastIpAddressEntry;
    std::function<DWORD(const MIB_UNICASTIPADDRESS_ROW *)> create = CreateUnicastIpAddressEntry;
    std::function<void(DWORD)> sleep = Sleep;
};

inline DWORD assignWindowsTunAddress(const QString &name, const QString &address,
                                     const WindowsTunAddressApi &api = {}, bool *created = nullptr)
{
    if (created) *created = false;
    MIB_UNICASTIPADDRESS_ROW requested {};
    InitializeUnicastIpAddressEntry(&requested);
    DWORD result = api.resolve(reinterpret_cast<const wchar_t *>(name.utf16()), &requested.InterfaceLuid);
    if (result != NO_ERROR)
        return result;
    requested.Address.si_family = AF_INET;
    if (inet_pton(AF_INET, address.toLatin1().constData(), &requested.Address.Ipv4.sin_addr) != 1)
        return ERROR_INVALID_PARAMETER;
    requested.OnLinkPrefixLength = 32;
    requested.ValidLifetime = requested.PreferredLifetime = 0xffffffff;
    auto preferred = [&]() {
        auto actual = requested;
        return api.query(&actual) == NO_ERROR
            && actual.InterfaceLuid.Value == requested.InterfaceLuid.Value
            && actual.Address.si_family == AF_INET
            && actual.Address.Ipv4.sin_addr.s_addr == requested.Address.Ipv4.sin_addr.s_addr
            && actual.OnLinkPrefixLength == 32 && actual.DadState == IpDadStatePreferred;
    };
    if (preferred())
        return NO_ERROR;
    result = api.create(&requested);
    if (result != NO_ERROR && result != ERROR_OBJECT_ALREADY_EXISTS)
        return result;
    if (created) *created = result == NO_ERROR;
    // Query actual state; an existing address or a lost notification must not
    // turn a successful assignment into a ten-second reconnect failure.
    for (int i = 0; i < 500; ++i) {
        if (preferred())
            return NO_ERROR;
        api.sleep(20);
    }
    return preferred() ? NO_ERROR : ERROR_TIMEOUT;
}
