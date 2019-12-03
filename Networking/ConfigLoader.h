#ifndef CONFIGLOADER_H_
#define CONFIGLOADER_H_

#include <string>
#include <map>

namespace bali
{

typedef std::map<std::string, std::string> ConfigMap;

ConfigMap
LoadConfig(
    std::string fn
);

}


#endif // CONFIGLOADER_H_
