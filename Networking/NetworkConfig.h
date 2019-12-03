#ifndef NETWORKCONFIG_H_
#define NETWORKCONFIG_H_

#include "ConfigLoader.h"

namespace bali
{

struct NetworkConfig
{
    uint32_t HEARTBEAT_MS;
    uint32_t STALE_MS;
    uint32_t REMOVE_MS;
    uint32_t ACKTIMEOUT_MS;
};

NetworkConfig
LoadNetworkConfig(
    std::string filename
);

}


#endif // PHYSICSCONFIG_H_
