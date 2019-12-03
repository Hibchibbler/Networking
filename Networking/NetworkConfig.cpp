#include "NetworkConfig.h"

namespace bali
{

NetworkConfig
LoadNetworkConfig(
    std::string filename
)
{
    NetworkConfig c;
    ConfigMap map = LoadConfig(filename);

    // Set from config file
    c.HEARTBEAT_MS = std::stoul(map["HEARTBEAT_MS"]);
    c.STALE_MS= std::stoul(map["STALE_MS"]);
    c.REMOVE_MS= std::stoul(map["REMOVE_MS"]);
    c.ACKTIMEOUT_MS= std::stoul(map["ACKTIMEOUT_MS"]);

    return c;
}

}
