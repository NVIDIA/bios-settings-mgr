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
#include "password.hpp"

#include "config.hpp"
#include "rfutility.hpp"
#include "xyz/openbmc_project/BIOSConfig/Common/error.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <fcntl.h>
#include <openssl/crypto.h>
#include <unistd.h>

#include <boost/algorithm/hex.hpp>
#include <boost/asio.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <fstream>
#include <iostream>

namespace bios_config_pwd
{
using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace sdbusplus::xyz::openbmc_project::BIOSConfig::Common::Error;

bool Password::compareDigest(const EVP_MD* digestFunc, size_t digestLen,
                             const std::array<uint8_t, maxHashSize>& expected,
                             const std::array<uint8_t, maxSeedSize>& seed,
                             const std::string& rawData)
{
    std::vector<uint8_t> output(digestLen);
    unsigned int hashLen = digestLen;

    if (!PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(rawData.c_str()),
                           rawData.length() + 1,
                           reinterpret_cast<const unsigned char*>(seed.data()),
                           seed.size(), iterValue, digestFunc, hashLen,
                           output.data()))
    {
        lg2::error("Generate PKCS5_PBKDF2_HMAC Integrity Check Value failed");
        throw InternalFailure();
    }

    if (CRYPTO_memcmp(output.data(), expected.data(),
                      output.size() * sizeof(uint8_t)) == 0)
    {
        return true;
    }

    return false;
}

bool Password::isMatch(const std::array<uint8_t, maxHashSize>& expected,
                       const std::array<uint8_t, maxSeedSize>& seed,
                       const std::string& rawData, const std::string& algo)
{
    lg2::debug("isMatch");

    if (algo == "SHA256")
    {
        return compareDigest(EVP_sha256(), SHA256_DIGEST_LENGTH, expected, seed,
                             rawData);
    }

    if (algo == "SHA384")
    {
        return compareDigest(EVP_sha384(), SHA384_DIGEST_LENGTH, expected, seed,
                             rawData);
    }

    return false;
}

bool Password::getParam(std::array<uint8_t, maxHashSize>& orgUsrPwdHash,
                        std::array<uint8_t, maxHashSize>& orgAdminPwdHash,
                        std::array<uint8_t, maxSeedSize>& seed,
                        std::string& hashAlgo, nlohmann::json& outJson)
{
    try
    {
        nlohmann::json json = nullptr;
        std::ifstream ifs(seedFile.c_str());
        if (!ifs.is_open())
        {
            lg2::error("getParam: unable to open seedFile");
            return false;
        }
        {
            try
            {
                json = nlohmann::json::parse(ifs, nullptr, false);
            }
            catch (const nlohmann::json::parse_error& e)
            {
                lg2::error("Failed to parse JSON file: {ERROR}", "ERROR", e);
                return false;
            }

            if (json.is_discarded())
            {
                lg2::error("getParam: seedFile JSON is discarded/invalid");
                return false;
            }
            orgUsrPwdHash = json["UserPwdHash"];
            orgAdminPwdHash = json["AdminPwdHash"];
            seed = json["Seed"];
            hashAlgo = json["HashAlgo"];
            outJson = json;
        }
    }
    catch (nlohmann::detail::exception& e)
    {
        lg2::error("Failed to parse JSON file: {ERROR}", "ERROR", e);
        return false;
    }

    return true;
}

bool Password::verifyIntegrityCheck(
    std::string& newPassword, std::array<uint8_t, maxSeedSize>& seed,
    unsigned int mdLen, const EVP_MD* digestFunc)
{
    mNewPwdHash.fill(0);

    if (!PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(newPassword.c_str()),
                           newPassword.length() + 1,
                           reinterpret_cast<const unsigned char*>(seed.data()),
                           seed.size(), iterValue, digestFunc, mdLen,
                           mNewPwdHash.data()))
    {
        lg2::error("Verify PKCS5_PBKDF2_HMAC Integrity Check failed");
        return false;
    }

    return true;
}

void Password::verifyPassword(std::string userName, std::string currentPassword,
                              std::string newPassword,
                              nlohmann::json& outSeedJson)
{
    if (currentPassword.empty() || newPassword.empty() ||
        newPassword.length() > maxPasswordLen)
    {
        throw InvalidCurrentPassword();
    }
    if (fs::exists(seedFile.c_str()))
    {
        std::array<uint8_t, maxHashSize> orgUsrPwdHash;
        std::array<uint8_t, maxHashSize> orgAdminPwdHash;
        std::array<uint8_t, maxSeedSize> seed;
        std::string hashAlgo = "";

        if (getParam(orgUsrPwdHash, orgAdminPwdHash, seed, hashAlgo,
                     outSeedJson))
        {
            if (hashAlgo != "SHA256" && hashAlgo != "SHA384")
            {
                lg2::error("Invalid or missing hashAlgo in seedData");
                throw InternalFailure();
            }
        }
        else
        {
            throw InternalFailure();
        }

        if (userName == "AdminPassword")
        {
            if (!isMatch(orgAdminPwdHash, seed, currentPassword, hashAlgo))
            {
                throw InvalidCurrentPassword();
            }
        }
        else
        {
            if (!isMatch(orgUsrPwdHash, seed, currentPassword, hashAlgo))
            {
                throw InvalidCurrentPassword();
            }
        }
        if (hashAlgo == "SHA256")
        {
            if (!verifyIntegrityCheck(newPassword, seed, 32, EVP_sha256()))
            {
                throw InternalFailure();
            }
        }
        if (hashAlgo == "SHA384")
        {
            if (!verifyIntegrityCheck(newPassword, seed, 48, EVP_sha384()))
            {
                throw InternalFailure();
            }
        }
        return;
    }
    throw InternalFailure();
}
void Password::changePassword(std::string userName, std::string currentPassword,
                              std::string newPassword)
{
    lg2::debug("BIOS config changePassword");

    if (failedAttempts >= maxFailedAttempts)
    {
        if (std::chrono::steady_clock::now() - lockoutStart <
            failedAttemptWindow)
        {
            lg2::error(
                "changePassword: locked after {MAX} failed attempts; retry later",
                "MAX", maxFailedAttempts);
            throw InternalFailure();
        }
        failedAttempts = 0;
    }

    if (currentPassword.empty() || newPassword.empty() ||
        newPassword.length() > maxPasswordLen)
    {
        throw InvalidCurrentPassword();
    }

    // Reuse the JSON verified above instead of re-reading the seed file, which
    // would leave a TOCTOU window between verification and write.
    nlohmann::json seedJson;
    try
    {
        verifyPassword(userName, currentPassword, newPassword, seedJson);
    }
    catch (const InvalidCurrentPassword&)
    {
        if (++failedAttempts >= maxFailedAttempts)
        {
            lockoutStart = std::chrono::steady_clock::now();
        }
        throw;
    }
    failedAttempts = 0;

    if (seedJson.is_null() || seedJson.is_discarded())
    {
        throw InternalFailure();
    }

    if (userName == "AdminPassword")
    {
        seedJson["AdminPwdHash"] = mNewPwdHash;
        seedJson["IsAdminPwdChanged"] = true;
    }
    else
    {
        seedJson["UserPwdHash"] = mNewPwdHash;
        seedJson["IsUserPwdChanged"] = true;
    }

    // Write to a temporary file and rename, so a crash mid-write cannot leave a
    // truncated seed file and lock the BIOS password path out permanently.
    fs::path tmpSeed = seedFile;
    tmpSeed += ".tmp";
    {
        std::ofstream ofs(tmpSeed.c_str(), std::ios::out);
        if (!ofs.is_open())
        {
            lg2::error("Cannot open temporary seed file for write");
            throw InternalFailure();
        }
        ofs << seedJson.dump(4);
    }
    {
        int fd = ::open(tmpSeed.c_str(), O_RDONLY);
        if (fd >= 0)
        {
            ::fsync(fd);
            ::close(fd);
        }
    }
    fs::rename(tmpSeed, seedFile);
    {
        int dfd =
            ::open(seedFile.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
        if (dfd >= 0)
        {
            ::fsync(dfd);
            ::close(dfd);
        }
    }

    bios_config::sendRedfishEvent("BiosPassword", "****", objectPathPwd);
}
Password::Password(sdbusplus::asio::object_server& objectServer,
                   std::shared_ptr<sdbusplus::asio::connection>& systemBus,
                   std::string persistPath) :
    sdbusplus::xyz::openbmc_project::BIOSConfig::server::Password(
        *systemBus, objectPathPwd),
    objServer(objectServer), systemBus(systemBus)
{
    lg2::debug("BIOS config password is running");
    try
    {
        fs::path biosDir(persistPath);
        fs::create_directories(biosDir);
        seedFile = biosDir / biosSeedFile;
    }
    catch (const fs::filesystem_error& e)
    {
        lg2::error("Failed to parse JSON file: {ERROR}", "ERROR", e);
        throw InternalFailure();
    }
}

} // namespace bios_config_pwd
