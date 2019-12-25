#ifndef NETWORKCONFIG_H_
#define NETWORKCONFIG_H_

#include "ConfigLoader.h"

namespace bali
{

struct NetworkConfig
{
    uint32_t HEART_BEAT_MS;
    uint32_t TIMEOUT_WARNING_MS;
    uint32_t TIMEOUT_MS;
    uint32_t ACK_TIMEOUT_MS;
};

NetworkConfig
LoadNetworkConfig(
    std::string filename
);

}


#endif // PHYSICSCONFIG_H_
