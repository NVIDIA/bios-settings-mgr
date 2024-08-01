#pragma once
#include <string>
namespace bios_config
{

void parsePropertyValueAndSendEvent(const std::string propertyName, const std::string dbusPropertyValue
                                    , const std::string objectPath);
void sendRedfishEvent(const std::string propertyName, const std::string propertyValue
                                    , const std::string objectPath);
} // namespace bios_config