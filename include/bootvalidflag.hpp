/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved. SPDX-License-Identifier: Apache-2.0
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
#pragma once
#include "asio_connection.hpp"

#include <boost/asio.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/server.hpp>

#include <chrono>
#include <string>
namespace bios_config_valid
{
using Value = std::variant<bool, uint8_t, int16_t, uint16_t, int32_t, uint32_t,
                           int64_t, uint64_t, double, std::string>;

/*
The Boot Valid Flag is a Boolean indicator that determines whether the boot
settings under the “/xyz/openbmc_project/control/host0/boot” path are valid for
the next host boot. This flag is represented by the
"xyz.openbmc_project.Object.Enable" interface within this path. This class
implements the behavior of this flag according to IPMI specification V2.0,
section 28.12.
*/
class BootValidFlag
{
  public:
    BootValidFlag(std::shared_ptr<sdbusplus::asio::connection> systemBusPtr,
                  boost::asio::io_service& io);

  private:
    std::unique_ptr<sdbusplus::bus::match::match> validUpdatedMatch;
    std::unique_ptr<sdbusplus::bus::match::match> softResetMatch;
    std::unique_ptr<boost::asio::steady_timer> timer_60;
    std::shared_ptr<sdbusplus::asio::connection> dbusConnectionPtr;

    /**
    *  @brief The function creates a match to signal property
    changes on the xyz.openbmc_project.Object.Enable interface
    located at the path "/xyz/openbmc_project/control/host0/boot".
    */
    void setupMatches(sdbusplus::bus_t& systemBus);

    /** @brief Sets the property value of the given object.
     *  @param[in] bus - DBUS Bus Object.
     *  @param[in] service - Dbus service name.
     *  @param[in] objPath - Dbus object path.
     *  @param[in] interface - Dbus interface.
     *  @param[in] property - name of the property.
     *  @param[in] value - value which needs to be set.
     */
    void setDbusProperty(const std::string& service, const std::string& objPath,
                         const std::string& interface,
                         const std::string& property,
                         bios_config_valid::Value& value);

    /**
     @brief check the values of the persistent flag and timeout override flag,
    then change the boot valid flag accordingly.  .
    **/
    void setBootValidFlag();

    /** @brief Set a timer for 60 seconds each time a property
     *  change signal with the value "true" is received.
     *  @param[in] msg - DBUS Bus Object.
     */
    void setTimer(sdbusplus::message::message& msg);

    /** @brief Cancel the timer if the property
     *  we get property change signal of last boot time..
     *  @param[in] msg - DBUS Bus Object.
     */
    void cancelTimer(sdbusplus::message::message& msg);
};

} // namespace bios_config_valid
