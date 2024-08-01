#include "utility.hpp"
#include "asio_connection.hpp"
#include <phosphor-logging/redfish_event_log.hpp>

namespace bios_config
{

void parsePropertyValueAndSendEvent(const std::string propertyName, const std::string dbusPropertyValue
                                    , const std::string objectPath)
{
    // Find the position of the last dot
    size_t lastDotPos = dbusPropertyValue.rfind('.');

    // Check if a dot was found
    if (lastDotPos != std::string::npos)
    {
        // Extract the substring after the last dot
        std::string propertyValue = dbusPropertyValue.substr(lastDotPos + 1);
        // Send the redfish event
        sendRedfishEvent(propertyName, propertyValue, objectPath);
    }
}

void sendRedfishEvent(const std::string propertyName, const std::string propertyValue
                                    , const std::string objectPath)
{
    using namespace phosphor::logging;
    // send event.
    std::vector<std::string> messageArgs = {propertyName, propertyValue};
    auto& conn = AsioConnection::getAsioConnection();
    sendEvent(conn, MESSAGE_TYPE::PROPERTY_VALUE_MODIFIED,
                Entry::Level::Informational, messageArgs, objectPath);
}

} // namespace bios_config