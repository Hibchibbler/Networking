#include "NetworkConfig.h"
#include <iostream>

namespace bali
{

NetworkConfig
LoadNetworkConfig(
    std::string filename
)
{
    NetworkConfig c;
    ConfigMap map = LoadConfig(filename);
    try
    {
        // Set from config file
        c.HEART_BEAT_MS = std::stoul(map["HEART_BEAT_MS"]);
        c.TIMEOUT_WARNING_MS= std::stoul(map["TIMEOUT_WARNING_MS"]);
        c.TIMEOUT_MS= std::stoul(map["TIMEOUT_MS"]);
        c.ACK_TIMEOUT_MS= std::stoul(map["ACK_TIMEOUT_MS"]);
        c.RETRY_COUNT = std::stoul(map["RETRY_COUNT"]);
    }
    catch (...)
    {
        std::cout << "Exception While Loading Config File.\n";
    }
    return c;
}

}
