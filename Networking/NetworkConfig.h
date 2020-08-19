#ifndef NETWORKCONFIG_H_
#define NETWORKCONFIG_H_

#include <string>
#include <map>

namespace bali
{

using NetworkConfigMap = std::map<std::string, std::string>;

NetworkConfigMap
LoadNetworkConfigBase(
    std::string fn
);

struct NetworkConfig
{
    uint32_t HEART_BEAT_MS;
    uint32_t TIMEOUT_WARNING_MS;
    uint32_t TIMEOUT_MS;
    uint32_t ACK_TIMEOUT_MS;
    uint32_t RETRY_COUNT;
};

NetworkConfig
LoadNetworkConfig(
    std::string filename
);

}


#endif // PHYSICSCONFIG_H_
