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
#include "bootvalidflag.hpp"

constexpr auto settingService = "xyz.openbmc_project.Settings";
constexpr auto bootSettingsPath = "/xyz/openbmc_project/control/host0/boot";
constexpr auto bootEnableIntf = "xyz.openbmc_project.Object.Enable";
constexpr auto timeoutOverRideIntf =
    "xyz.openbmc_project.Control.Boot.BootSettingsExpiryOverride";
constexpr auto bootSettingsOneTimePath =
    "/xyz/openbmc_project/control/host0/boot/one_time";
constexpr auto propIntf = "org.freedesktop.DBus.Properties";
constexpr auto methodeGet = "Get";
constexpr auto methodSet = "Set";
constexpr auto enalbeProperty = "Enabled";
constexpr auto timeoutOverRideProperty = "BootValidTimeoutOverride";

constexpr auto TIMER_TIME = 60;

namespace bios_config_valid
{

void BootValidFlag::setDbusProperty(const std::string& service,
                                    const std::string& objPath,
                                    const std::string& interface,
                                    const std::string& property,
                                    bios_config_valid::Value& value)
{
    dbusConnectionPtr->async_method_call(
        [property, objPath, interface](boost::system::error_code ec) {
        if (ec)
        {
            lg2::error(
                "Failed to set property {PROPERTY} on {PATH} for interface {INTERFACE} ERROR: {ERROR}",
                "PROPERTY", property, "PATH", objPath, "INTERFACE", interface,
                "ERROR", ec.what());
            return;
        }
    },
        service, objPath, propIntf, methodSet, interface, property, value);
}

void BootValidFlag::setBootValidFlag()
{
    // check if  persistentFlag Flag is set,
    dbusConnectionPtr->async_method_call(
        [this](boost::system::error_code ec, sdbusplus::message_t reply) {
        if (ec)
        {
            lg2::error(
                "Failed to get property {PROPERTY} on {PATH} for interface {INTERFACE} ERROR: {ERROR}",
                "PROPERTY", enalbeProperty, "PATH", bootSettingsOneTimePath,
                "INTERFACE", bootEnableIntf, "ERROR", ec.what());
            return;
        }
        bios_config_valid::Value value;
        reply.read(value);
        // persistentFlag  value is inverted to one_time property value
        bool persistentFlag = !std::get<bool>(value);
        if (!persistentFlag)
        {
            // check if timeoutOverride Flag is set,
            this->dbusConnectionPtr->async_method_call(
                [this](boost::system::error_code ec,
                       sdbusplus::message_t reply) {
                if (ec)
                {
                    lg2::error(
                        "Failed to get property {PROPERTY} on {PATH} for interface {INTERFACE} ERROR: {ERROR}",
                        "PROPERTY", timeoutOverRideProperty, "PATH",
                        bootSettingsPath, "INTERFACE", timeoutOverRideIntf,
                        "ERROR", ec.what());
                    return;
                }
                bios_config_valid::Value value;
                reply.read(value);
                bool timeoutOverride = std::get<bool>(value);
                if (!timeoutOverride)
                {
                    // If both the Persistent Flag and Timeout Override Flags
                    // are not set, then set the Boot Valid Flag value to false.
                    bool setToFalse = false;
                    bios_config_valid::Value var(setToFalse);
                    this->setDbusProperty(settingService, bootSettingsPath,
                                          bootEnableIntf, enalbeProperty, var);
                }
            },
                settingService, bootSettingsPath, propIntf, methodeGet,
                timeoutOverRideIntf, timeoutOverRideProperty);
        }
    },
        settingService, bootSettingsOneTimePath, propIntf, methodeGet,
        bootEnableIntf, enalbeProperty);
    return;
}

void BootValidFlag::setTimer(sdbusplus::message::message& msg)
{
    std::string interfaceName;
    std::map<std::string, std::variant<bool>> changedProperties;
    std::vector<std::string> invalidatedProperties;
    msg.read(interfaceName, changedProperties, invalidatedProperties);
    // Verify if the property has been set to false, and if so, cancel the
    // timer.
    for (const auto& [property, value] : changedProperties)
    {
        if (property == enalbeProperty)
        {
            bool boootValidFlagValue = std::get<bool>(value);
            if (!boootValidFlagValue)
            {
                if (timer_60->expires_at() > std::chrono::steady_clock::now())
                {
                    // Timer is active, so cancel it
                    timer_60->cancel();
                }
                return;
            }
        }
    }
    // Set new expiry time and start new asynchronous wait for 60 sec.
    timer_60->expires_after(std::chrono::seconds(TIMER_TIME));
    timer_60->async_wait([this](const boost::system::error_code& error) {
        if (!error)
        {
            {
                this->setBootValidFlag();
            }
        }
        else
        {
            lg2::error("Timer error: {ERROR}", "ERROR", error.message());
        }
    });
}

void BootValidFlag::setupMatches(sdbusplus::bus_t& dbusConnection)
{
    validUpdatedMatch = std::make_unique<sdbusplus::bus::match::match>(
        dbusConnection,
        sdbusplus::bus::match::rules::type::signal() +
            sdbusplus::bus::match::rules::member(
                std::string{"PropertiesChanged"}) +
            sdbusplus::bus::match::rules::interface(
                std::string{"org.freedesktop.DBus.Properties"}) +
            sdbusplus::bus::match::rules::argN(0, bootEnableIntf) +
            sdbusplus::bus::match::rules::path(
                std::string{"/xyz/openbmc_project/control/host0/boot"}),
        [this](sdbusplus::message::message& msg) { this->setTimer(msg); });
}

void BootValidFlag::restartBootValidFlag()
{
    dbusConnectionPtr->async_method_call(
        [this](boost::system::error_code ec, sdbusplus::message_t reply) {
        if (ec)
        {
            lg2::error(
                "Failed to get property {PROPERTY} on {PATH} for interface {INTERFACE},ERROR: {ERROR} ",
                "PROPERTY", enalbeProperty, "PATH", bootSettingsOneTimePath,
                "INTERFACE", bootEnableIntf, "ERROR", ec.what());
            return;
        }
        // check if persistent Flag is set,
        // if no set the TimeoutOverRide Flag
        // and boot valid flag to false on BMC reboot
        bios_config_valid::Value value;
        reply.read(value);
        bool persistentFlag = !std::get<bool>(value);
        if (!persistentFlag)
        {
            bool setToFalse = false;
            bios_config_valid::Value var(setToFalse);
            this->setDbusProperty(settingService, bootSettingsPath,
                                  bootEnableIntf, enalbeProperty, var);
        }
    },
        settingService, bootSettingsOneTimePath, propIntf, methodeGet,
        bootEnableIntf, enalbeProperty);
}

BootValidFlag::BootValidFlag(
    std::shared_ptr<sdbusplus::asio::connection> systemBusPtr,
    boost::asio::io_service& io)
{
    dbusConnectionPtr = systemBusPtr;
    // Verify if the BootValidTimeoutOverride property exists in the BMC.
    // If it does, create a change on the property signal match and
    // restart the boot valid flag.
    // If it does not exist, take no action.
    dbusConnectionPtr->async_method_call(
        [this, &io](boost::system::error_code ec) {
        if (ec)
        {
            lg2::info(
                "Failed to find BootValidTimeoutOverride property boot valid flag won't reset after 60 seconds.ERROR: {ERROR}",
                "ERROR", ec.what());
            return;
        }
        timer_60 = std::make_unique<boost::asio::steady_timer>(io);
        this->restartBootValidFlag();
        this->setupMatches(*dbusConnectionPtr);
    },
        settingService, bootSettingsPath, propIntf, methodeGet,
        timeoutOverRideIntf, timeoutOverRideProperty);
}
} // namespace bios_config_valid
