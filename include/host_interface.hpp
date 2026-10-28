// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
#pragma once

#include "manager.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus/match.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace bios_config
{

/** @class HostInterface
 *
 *  @brief Applies the Redfish host interface policy to the host OS state
 *  (OperatingSystemState on host0, which x86-power-control sets from
 *  CPU_BOOT_DONE), on every change and once the state can first be read:
 *  - Inactive (host reset or off): delete the users the BMC created for the
 *    host interface, call <resetCredentialBootstrap> to re-arm
 *    CredentialBootstrap from EnableAfterReset (DSP0270), and enable the host
 *    interface NIC so UEFI can reach the BMC.
 *  - Standby (host OS running): enable the NIC unless the BIOS attribute
 *    RedfishHostInterface is Disabled.
 *  The NIC is left alone where the BIOS has no RedfishHostInterface
 *  attribute (<hostInterfaceSetting> returns nothing).
 */
class HostInterface
{
  public:
    HostInterface(
        std::shared_ptr<sdbusplus::asio::connection> systemBus,
        std::function<void()> resetCredentialBootstrap,
        std::function<std::optional<std::string>()> hostInterfaceSetting);

  private:
    void readOsState();
    void apply(const std::string& osState);
    void deleteUsers();
    void setNicEnabled(bool enabled);

    std::shared_ptr<sdbusplus::asio::connection> systemBus;
    std::function<void()> resetCredentialBootstrap;
    std::function<std::optional<std::string>()> hostInterfaceSetting;
    std::unique_ptr<sdbusplus::bus::match_t> osStateMatch;
    std::unique_ptr<sdbusplus::bus::match_t> hostOwnerMatch;
};

/** @brief The RedfishHostInterface BIOS attribute's current value, or
 *  nothing when the table has no such string attribute.
 */
std::optional<std::string> redfishHostInterfaceSetting(
    const Manager::BaseTable& table);

/** @brief redfishHostInterfaceSetting() of the manager's current BIOS table.
 */
std::optional<std::string> redfishHostInterfaceSettingOf(Manager& manager);

/** @brief Re-arm CredentialBootstrap from EnableAfterReset (DSP0270). */
void rearmCredentialBootstrap(Manager& manager);

} // namespace bios_config
