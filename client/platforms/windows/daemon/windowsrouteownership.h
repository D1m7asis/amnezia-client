#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <functional>

inline DWORD acquireWindowsRoute(MIB_IPFORWARD_ROW2 *row, bool &owned,
    const std::function<DWORD(const MIB_IPFORWARD_ROW2 *)> &create = CreateIpForwardEntry2,
    const std::function<DWORD(MIB_IPFORWARD_ROW2 *)> &query = GetIpForwardEntry2)
{
    owned = false;
    const DWORD result = create(row);
    if (result == NO_ERROR) {
        owned = true;
        return NO_ERROR;
    }
    if (result != ERROR_OBJECT_ALREADY_EXISTS) return result;
    auto existing = *row;
    const DWORD found = query(&existing);
    if (found != NO_ERROR) return found;
    if (existing.InterfaceLuid.Value != row->InterfaceLuid.Value
        || existing.DestinationPrefix.PrefixLength != row->DestinationPrefix.PrefixLength
        || memcmp(&existing.DestinationPrefix.Prefix, &row->DestinationPrefix.Prefix, sizeof(SOCKADDR_INET))
        || memcmp(&existing.NextHop, &row->NextHop, sizeof(SOCKADDR_INET)))
        return ERROR_INVALID_DATA;
    return NO_ERROR;
}

inline DWORD releaseWindowsRoute(const MIB_IPFORWARD_ROW2 *row, bool owned,
    const std::function<DWORD(const MIB_IPFORWARD_ROW2 *)> &remove = DeleteIpForwardEntry2)
{
    return owned ? remove(row) : NO_ERROR;
}
