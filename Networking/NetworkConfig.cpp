#include "NetworkConfig.h"

#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#include <iostream>

namespace bali
{
NetworkConfigMap
LoadNetworkConfigBase(
    std::string fn
)
{
    NetworkConfigMap configValues;
    std::ifstream configIn(fn);
    if (configIn.is_open())
    {
        std::string line;
        while (std::getline(configIn, line))
        {
            std::istringstream is_line(line);
            std::string key;
            if (std::getline(is_line, key, '='))
            {
                std::string value;
                if (key[0] == '#')
                    continue;

                if (std::getline(is_line, value))
                {

                    // convert key to upper case
                    std::for_each(key.begin(), key.end(), [](char & c) {
                        c = ::toupper(c);
                    });
                    configValues[key] = value;
                    //cout << "config[\"" << key << "\"] = " << value << endl;
                }
            }
        }
    }
    else
    {
        std::cout << "Error: unable to open: " << fn << std::endl;
    }

    return configValues;
}

NetworkConfig
LoadNetworkConfig(
    std::string filename
)
{
    NetworkConfig c;
    NetworkConfigMap map = LoadNetworkConfigBase(filename);
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
