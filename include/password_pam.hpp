// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2020 Intel Corporation
/*
 Copyright (c) 2020 Intel Corporation

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http:www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#pragma once
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/BIOSConfig/Password/server.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace bios_config_pwd
{
class Password;

static constexpr auto objectPathPwd =
    "/xyz/openbmc_project/bios_config/password";
constexpr auto passwordStateFile = "password_state";
// Max on-disk schema this build writes and can decode (stored as fileVer in the
// file).
constexpr uint32_t passwordStateSerializeVersion = 1u;

using Base = sdbusplus::xyz::openbmc_project::BIOSConfig::server::Password;
namespace fs = std::filesystem;

/** @brief Persist the Password D-Bus property values (PasswordInitialized,
 * PasswordSync). Binary layout: magic, passwordStateSerializeVersion, then
 * fields (cereal). Written to a temporary file, fsynced and renamed over path,
 * so a crash never leaves a truncated file. The directory is then fsynced to
 * make the rename crash-durable.
 *  @return true once the new file has been renamed into place; false if
 * writing, syncing or renaming the temporary file failed (path is then left
 * unchanged). Failing to open or fsync the directory is only logged and still
 * returns true.
 */
bool serializePasswordState(bool initialized, bool sync,
                            const std::filesystem::path& path);

/** @brief Restore Password properties from file. If file missing, sets both to
 * false. New files must have file version <= passwordStateSerializeVersion;
 * newer versions log error and fail. The pre-versioning format is accepted only
 * as exactly two bytes, each 0 or 1. Anything else (wrong size, torn write,
 * non-boolean byte) fails closed.
 *  @return true if file was loaded, false if missing or invalid (then props set
 * to false).
 */
bool deserializePasswordState(const std::filesystem::path& path, Password& obj);

/** @class Password
 *
 *  @brief D-Bus object for BIOS Password properties (PasswordInitialized,
 *         PasswordSync). Persists/restores state in this module.
 *         Password verification and change are handled by bmcweb (PAM) and
 *         nvidia-ipmi-oem; this daemon only owns the properties.
 *         Built instead of password.hpp when bios-password-pam is enabled.
 */
class Password : public Base
{
  public:
    Password() = delete;
    ~Password() = default;
    Password(const Password&) = delete;
    Password& operator=(const Password&) = delete;
    Password(Password&&) = delete;
    Password& operator=(Password&&) = delete;

    Password(sdbusplus::asio::object_server& objectServer,
             std::shared_ptr<sdbusplus::asio::connection>& systemBus,
             std::string persistPath);

    /** @brief The ChangePassword D-Bus method is kept by the Password
     *         interface but is not served: password changes go through
     *         bmcweb (Redfish, PAM) and nvidia-ipmi-oem (IPMI).
     *  @throws PasswordNotSettable always.
     */
    void changePassword(std::string userName, std::string currentPassword,
                        std::string newPassword) override;

    /** @brief Property setters. The new value is persisted before the property
     *         changes or a signal is emitted. A successful Set means the new
     *         state file was renamed into place; a directory fsync failure is
     *         only logged and does not fail the Set.
     *  @throws sdbusplus::exception::SdBusError (EIO) if the state file cannot
     *          be written; the property is then left unchanged and the D-Bus
     *          caller gets an error reply. */
    bool passwordInitialized(bool value) override;
    bool passwordSync(bool value) override;

    bool getPasswordInitialized() const;
    bool getPasswordSync() const;

    /** Used by deserializePasswordState to set properties without triggering
     * save. */
    void setSkipSerialize(bool skip)
    {
        skipSerialize_ = skip;
    }

  private:
    sdbusplus::asio::object_server& objServer;
    std::shared_ptr<sdbusplus::asio::connection>& systemBus;
    std::filesystem::path statePath;
    bool skipSerialize_ = false;
};

} // namespace bios_config_pwd
