#include "ConfigLoader.h"

#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>

namespace bali
{
ConfigMap
LoadConfig(
    std::string fn
)
{
    ConfigMap configValues;
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

    return configValues;
}

}