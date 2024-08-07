/*
* Copyright (c) 2020-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
* SPDX-License-Identifier: Apache-2.0
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "rfutility.hpp"

#include "asio_connection.hpp"

#include <phosphor-logging/redfish_event_log.hpp>

namespace bios_config
{

void parsePropertyValueAndSendEvent(const std::string propertyName,
                                    const std::string dbusPropertyValue,
                                    const std::string objectPath)
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

void sendRedfishEvent(const std::string propertyName,
                      const std::string propertyValue,
                      const std::string objectPath)
{
    using namespace phosphor::logging;
    // send event.
    std::vector<std::string> messageArgs = {propertyName, propertyValue};
    auto& conn = AsioConnection::getAsioConnection();
    sendEvent(conn, MESSAGE_TYPE::PROPERTY_VALUE_MODIFIED,
              Entry::Level::Informational, messageArgs, objectPath);
}

} // namespace bios_config
