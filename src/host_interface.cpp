// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
#include "host_interface.hpp"

#include <grp.h>

#include <phosphor-logging/lg2.hpp>

#include <map>
#include <variant>
#include <vector>

namespace bios_config
{

namespace
{
constexpr auto hostService = "xyz.openbmc_project.State.Host";
constexpr auto hostPath = "/xyz/openbmc_project/state/host0";
constexpr auto osStatusIntf =
    "xyz.openbmc_project.State.OperatingSystem.Status";
constexpr auto osStateProperty = "OperatingSystemState";
constexpr auto osPrefix =
    "xyz.openbmc_project.State.OperatingSystem.Status.OSStatus.";
constexpr auto hostInterfaceGroup = "redfish-hostiface";
constexpr auto userService = "xyz.openbmc_project.User.Manager";
constexpr auto userPath = "/xyz/openbmc_project/user/";
constexpr auto networkService = "xyz.openbmc_project.Network";
constexpr auto hostInterfaceNicPath = "/xyz/openbmc_project/network/hostusb0";
constexpr auto ethernetIntf = "xyz.openbmc_project.Network.EthernetInterface";
} // namespace

HostInterface::HostInterface(
    std::shared_ptr<sdbusplus::asio::connection> systemBus,
    std::function<void()> resetCredentialBootstrap,
    std::function<std::optional<std::string>()> hostInterfaceSetting) :
    systemBus(std::move(systemBus)),
    resetCredentialBootstrap(std::move(resetCredentialBootstrap)),
    hostInterfaceSetting(std::move(hostInterfaceSetting))
{
    osStateMatch = std::make_unique<sdbusplus::bus::match_t>(
        *this->systemBus,
        sdbusplus::bus::match::rules::propertiesChanged(hostPath, osStatusIntf),
        [this](sdbusplus::message_t& msg) {
            std::string intf;
            std::map<std::string, std::variant<std::string>> changed;
            try
            {
                msg.read(intf, changed);
            }
            catch (const std::exception& e)
            {
                lg2::error("Failed to read {PROPERTY} change: {ERROR}",
                           "PROPERTY", osStateProperty, "ERROR", e.what());
                return;
            }
            auto it = changed.find(osStateProperty);
            if (it != changed.end())
            {
                apply(std::get<std::string>(it->second));
            }
        });

    // x86-power-control claims its name once host0 is published; the state it
    // read at start-up is not signalled as a change.
    hostOwnerMatch = std::make_unique<sdbusplus::bus::match_t>(
        *this->systemBus,
        sdbusplus::bus::match::rules::nameOwnerChanged(hostService),
        [this](sdbusplus::message_t& msg) {
            std::string name;
            std::string oldOwner;
            std::string newOwner;
            try
            {
                msg.read(name, oldOwner, newOwner);
            }
            catch (const std::exception&)
            {
                return;
            }
            if (!newOwner.empty())
            {
                readOsState();
            }
        });

    readOsState();
}

void HostInterface::readOsState()
{
    systemBus->async_method_call(
        [this](const boost::system::error_code& ec,
               const std::variant<std::string>& state) {
            if (!ec)
            {
                apply(std::get<std::string>(state));
            }
        },
        hostService, hostPath, "org.freedesktop.DBus.Properties", "Get",
        osStatusIntf, osStateProperty);
}

void HostInterface::apply(const std::string& osState)
{
    if (osState == std::string(osPrefix) + "Inactive")
    {
        deleteUsers();
        resetCredentialBootstrap();
        setNicEnabled(true);
    }
    else if (osState == std::string(osPrefix) + "Standby")
    {
        if (const auto setting = hostInterfaceSetting())
        {
            setNicEnabled(*setting != "Disabled");
        }
    }
}

void HostInterface::deleteUsers()
{
    std::vector<std::string> users;
    if (const group* g = getgrnam(hostInterfaceGroup); g != nullptr)
    {
        for (char** member = g->gr_mem; *member != nullptr; ++member)
        {
            users.emplace_back(*member);
        }
    }
    for (const auto& user : users)
    {
        lg2::info("Deleting host interface user {USER}", "USER", user);
        systemBus->async_method_call(
            [user](const boost::system::error_code& ec) {
                if (ec)
                {
                    lg2::error("Failed to delete host interface user {USER}: "
                               "{ERROR}",
                               "USER", user, "ERROR", ec.message());
                }
            },
            userService, userPath + user, "xyz.openbmc_project.Object.Delete",
            "Delete");
    }
}

// phosphor-networkd persists NICEnabled, so it is set on every state, not
// only when it changes.
void HostInterface::setNicEnabled(bool enabled)
{
    lg2::info("Host interface NIC enabled: {ENABLED}", "ENABLED", enabled);
    systemBus->async_method_call(
        [](const boost::system::error_code& ec) {
            if (ec)
            {
                lg2::error("Failed to set host interface NICEnabled: {ERROR}",
                           "ERROR", ec.message());
            }
        },
        networkService, hostInterfaceNicPath, "org.freedesktop.DBus.Properties",
        "Set", ethernetIntf, "NICEnabled", std::variant<bool>(enabled));
}

std::optional<std::string> redfishHostInterfaceSetting(
    const Manager::BaseTable& table)
{
    const auto it = table.find("RedfishHostInterface");
    if (it == table.end())
    {
        return std::nullopt;
    }
    const auto* value = std::get_if<std::string>(&std::get<5>(it->second));
    return value != nullptr ? std::optional(*value) : std::nullopt;
}

std::optional<std::string> redfishHostInterfaceSettingOf(Manager& manager)
{
    return redfishHostInterfaceSetting(
        static_cast<Base&>(manager).baseBIOSTable());
}

void rearmCredentialBootstrap(Manager& manager)
{
    manager.credentialBootstrap(static_cast<Base&>(manager).enableAfterReset());
}

} // namespace bios_config
