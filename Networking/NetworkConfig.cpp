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
    c.HEART_BEAT_MS = std::stoul(map["HEART_BEAT_MS"]);
    c.TIMEOUT_WARNING_MS= std::stoul(map["TIMEOUT_WARNING_MS"]);
    c.TIMEOUT_MS= std::stoul(map["TIMEOUT_MS"]);
    c.ACK_TIMEOUT_MS= std::stoul(map["ACK_TIMEOUT_MS"]);
    c.RETRY_COUNT = std::stoul(map["RETRY_COUNT"]);
    return c;
}

}
