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
#include "password_pam.hpp"

#include "xyz/openbmc_project/BIOSConfig/Common/error.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

namespace bios_config_pwd
{
using namespace sdbusplus::xyz::openbmc_project::BIOSConfig::Common::Error;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

namespace
{
// Fixed prefix so we never mistake legacy data (raw two bools) for
// magic/version uint32s. Value must match what cereal::BinaryOutputArchive
// writes for uint32_t.
constexpr uint32_t kPasswordStateMagic =
    (static_cast<uint32_t>('B') << 0) | (static_cast<uint32_t>('C') << 8) |
    (static_cast<uint32_t>('P') << 16) | (static_cast<uint32_t>('W') << 24);

// Pre-versioning format: exactly cereal( bool, bool ), one byte each.
constexpr size_t kLegacyStateSize = 2;
// Versioned format: [magic u32][fileVer u32] followed by the two bools.
constexpr size_t kHeaderSize = 2 * sizeof(uint32_t);
constexpr size_t kV1StateSize = kHeaderSize + 2;
// Anything larger than this cannot be a password_state file.
constexpr uintmax_t kMaxStateFileSize = 64;

sdbusplus::asio::connection& derefBusOrThrow(
    std::shared_ptr<sdbusplus::asio::connection>& bus)
{
    if (!bus)
    {
        lg2::error("systemBus shared_ptr is null");
        throw InternalFailure();
    }
    return *bus;
}

/** @brief Replace path with data, so a crash never leaves a truncated file.
 *
 *  Writes a temporary file next to path, fsyncs it, renames it over path and
 *  then fsyncs the directory. Every step up to the rename is checked and the
 *  temporary file is removed on failure. Once the rename succeeds the new data
 *  is in place, so a failure to open or sync the directory is only logged and
 *  does not make the write fail; it only means the rename may not survive a
 *  crash.
 *
 *  @return true if the data was written, synced and renamed into place.
 */
bool writeFileDurably(const std::filesystem::path& path,
                      const std::string& data)
{
    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";

    auto fail = [&tmpPath](const char* what, int err) {
        lg2::error("password_state: {WHAT} failed: {ERROR}", "WHAT", what,
                   "ERROR", std::strerror(err));
        ::unlink(tmpPath.c_str());
        return false;
    };

    int fd =
        ::open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
    {
        return fail("open", errno);
    }

    size_t written = 0;
    while (written < data.size())
    {
        ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            int err = errno;
            ::close(fd);
            return fail("write", err);
        }
        written += static_cast<size_t>(n);
    }

    if (::fsync(fd) != 0)
    {
        int err = errno;
        ::close(fd);
        return fail("fsync", err);
    }
    if (::close(fd) != 0)
    {
        return fail("close", errno);
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec)
    {
        return fail("rename", ec.value());
    }

    // The rename has replaced path, so the new data is what a reload sees and
    // the write succeeded. Syncing the directory only makes the rename
    // crash-durable; if it fails, log it instead of reporting a failure for
    // a change that already took effect.
    int dfd =
        ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0)
    {
        lg2::error("password_state: open directory failed: {ERROR}", "ERROR",
                   std::strerror(errno));
        return true;
    }
    if (::fsync(dfd) != 0)
    {
        lg2::error("password_state: fsync directory failed: {ERROR}", "ERROR",
                   std::strerror(errno));
    }
    ::close(dfd);
    return true;
}

/** @brief Reject a property Set because the state could not be persisted.
 *
 *  The generated property setter callback only turns sdbusplus internal
 *  exceptions into an error reply. Any other exception, including the
 *  generated InternalFailure, is stored on the bus and rethrown from the
 *  event loop, which terminates the daemon after a success reply was already
 *  sent. SdBusError is an internal exception, so the caller (for example
 *  bmcweb) receives a D-Bus error and the daemon keeps running.
 */
[[noreturn]] void failPropertySet()
{
    lg2::error("Failed to persist password_state, rejecting the property Set");
    throw sdbusplus::exception::SdBusError(EIO,
                                           "Failed to persist password_state");
}

/** Keeps the setters from rewriting the file while it is being loaded. */
class SkipSerializeGuard
{
  public:
    explicit SkipSerializeGuard(Password& target) : obj(target)
    {
        obj.setSkipSerialize(true);
    }
    ~SkipSerializeGuard()
    {
        obj.setSkipSerialize(false);
    }
    SkipSerializeGuard(const SkipSerializeGuard&) = delete;
    SkipSerializeGuard& operator=(const SkipSerializeGuard&) = delete;
    SkipSerializeGuard(SkipSerializeGuard&&) = delete;
    SkipSerializeGuard& operator=(SkipSerializeGuard&&) = delete;

  private:
    Password& obj;
};

bool isStrictBool(char byte)
{
    return static_cast<unsigned char>(byte) <= 1;
}
} // namespace

// On disk: [magic u32][fileVer u32][cereal binary: PasswordInitialized,
// PasswordSync]
bool serializePasswordState(bool initialized, bool sync,
                            const std::filesystem::path& path)
{
    try
    {
        std::ostringstream os(std::ios::out | std::ios::binary);
        {
            cereal::BinaryOutputArchive archive(os);
            archive(kPasswordStateMagic, passwordStateSerializeVersion,
                    initialized, sync);
        }
        return writeFileDurably(path, os.str());
    }
    catch (const std::exception& e)
    {
        const char* const msg = e.what();
        lg2::error("Failed to save password state: {ERROR}", "ERROR",
                   msg != nullptr ? msg : "");
        return false;
    }
}

bool deserializePasswordState(const std::filesystem::path& path, Password& obj)
{
    // Setting properties during load must not rewrite the file from setters.
    SkipSerializeGuard skipGuard(obj);

    auto failClosed = [&obj]() {
        obj.passwordInitialized(false);
        obj.passwordSync(false);
        return false;
    };

    try
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            return failClosed();
        }

        const auto fileSize = std::filesystem::file_size(path);
        if (fileSize > kMaxStateFileSize)
        {
            lg2::error("password_state is too large ({SIZE} bytes)", "SIZE",
                       fileSize);
            return failClosed();
        }

        std::ifstream is(path.c_str(), std::ios::in | std::ios::binary);
        if (!is)
        {
            lg2::error("Failed to open password_state for read");
            return failClosed();
        }
        std::string data(fileSize, '\0');
        is.read(data.data(), static_cast<std::streamsize>(data.size()));
        if (!is || static_cast<size_t>(is.gcount()) != data.size())
        {
            lg2::error("Failed to read password_state");
            return failClosed();
        }

        std::string_view payload;
        uint32_t magic = 0;
        if (data.size() >= sizeof(magic))
        {
            std::memcpy(&magic, data.data(), sizeof(magic));
        }

        if (data.size() == kLegacyStateSize)
        {
            // Pre-versioning format: exactly two bytes, each 0 or 1.
            payload = data;
        }
        else if (data.size() >= kHeaderSize && magic == kPasswordStateMagic)
        {
            uint32_t fileVer = 0;
            std::memcpy(&fileVer, data.data() + sizeof(magic), sizeof(fileVer));
            // fileVer: schema stamped when the file was written.
            // passwordStateSerializeVersion: max schema this binary
            // understands (see password_pam.hpp).
            if (fileVer > passwordStateSerializeVersion)
            {
                lg2::error(
                    "password_state file version {FILE_VER} is newer than supported {MAX_VER}",
                    "FILE_VER", fileVer, "MAX_VER",
                    passwordStateSerializeVersion);
                return failClosed();
            }
            if (fileVer < 1 || data.size() != kV1StateSize)
            {
                lg2::error(
                    "Invalid password_state header (version {FILE_VER}, {SIZE} bytes)",
                    "FILE_VER", fileVer, "SIZE", data.size());
                return failClosed();
            }
            payload = std::string_view(data).substr(kHeaderSize);
        }
        else
        {
            // Not a legacy file and no valid header (corrupt or a torn write).
            lg2::error("Unrecognized password_state ({SIZE} bytes)", "SIZE",
                       data.size());
            return failClosed();
        }

        if (!isStrictBool(payload[0]) || !isStrictBool(payload[1]))
        {
            lg2::error("password_state holds a non-boolean value");
            return failClosed();
        }
        obj.passwordInitialized(payload[0] != 0);
        obj.passwordSync(payload[1] != 0);
        return true;
    }
    catch (const std::exception& e)
    {
        const char* const msg = e.what();
        lg2::error("Failed to load password state: {ERROR}", "ERROR",
                   msg != nullptr ? msg : "");
        return failClosed();
    }
}

bool Password::getPasswordInitialized() const
{
    return Base::passwordInitialized();
}

bool Password::getPasswordSync() const
{
    return Base::passwordSync();
}

void Password::changePassword(std::string /*userName*/,
                              std::string /*currentPassword*/,
                              std::string /*newPassword*/)
{
    lg2::error("ChangePassword D-Bus method is not supported");
    // Must be an error the ChangePassword method declares; sdbusplus only
    // catches those, anything else terminates the daemon.
    throw PasswordNotSettable();
}

// The new value is persisted before the property changes, so nothing is
// published for a failed write. A successful Properties.Set means the new file
// was renamed into place; a directory fsync failure is only logged and does
// not fail the Set.
bool Password::passwordInitialized(bool value)
{
    if (!skipSerialize_ &&
        !serializePasswordState(value, Base::passwordSync(), statePath))
    {
        failPropertySet();
    }
    return Base::passwordInitialized(value);
}

bool Password::passwordSync(bool value)
{
    if (!skipSerialize_ &&
        !serializePasswordState(Base::passwordInitialized(), value, statePath))
    {
        failPropertySet();
    }
    return Base::passwordSync(value);
}

Password::Password(sdbusplus::asio::object_server& objectServer,
                   std::shared_ptr<sdbusplus::asio::connection>& systemBus,
                   std::string persistPath) :
    sdbusplus::xyz::openbmc_project::BIOSConfig::server::Password(
        derefBusOrThrow(systemBus), objectPathPwd),
    objServer(objectServer), systemBus(systemBus)
{
    lg2::debug("BIOS config password is running");
    try
    {
        fs::path biosDir(persistPath);
        fs::create_directories(biosDir);
        statePath = biosDir / passwordStateFile;
    }
    catch (const fs::filesystem_error& e)
    {
        lg2::error("Failed to create bios dir: {ERROR}", "ERROR", e);
        throw InternalFailure();
    }
}

} // namespace bios_config_pwd
