#pragma once
#include "ClientConfig.hpp"
#include <BitStream.h>

namespace mta
{
    void BuildJoinData(const ClientConfig& config, RakNet::BitStream& stream);
}
