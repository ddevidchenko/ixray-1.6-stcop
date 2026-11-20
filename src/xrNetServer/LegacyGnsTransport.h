#pragma once

#include "NET_Shared.h"

class SteamNetClient;
class SteamNetServer;

namespace LegacyGnsTransport
{
XRNETSERVER_API void Poll(SteamNetClient& Client, SteamNetServer* Server);
}
