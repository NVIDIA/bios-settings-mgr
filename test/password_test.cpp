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

#include "common.hpp"
#include "password.hpp"

#include <sys/stat.h>

#include <nlohmann/json.hpp>
#include <xyz/openbmc_project/BIOSConfig/Common/error.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace bios_config_pwd::test
{

using namespace bios_config_pwd;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace sdbusplus::xyz::openbmc_project::BIOSConfig::Common::Error;

class TestablePassword : public Password
{
  public:
    using Password::Password;

    using Password::compareDigest;
    using Password::getParam;
    using Password::isMatch;
    using Password::verifyIntegrityCheck;
    using Password::verifyPassword;

    // Overloads that discard the seed JSON, so tests which do not care about
    // it keep the pre-existing three/four argument call shape.
    bool getParam(std::array<uint8_t, maxHashSize>& orgUsrPwdHash,
                  std::array<uint8_t, maxHashSize>& orgAdminPwdHash,
                  std::array<uint8_t, maxSeedSize>& seed, std::string& hashAlgo)
    {
        nlohmann::json discarded;
        return Password::getParam(orgUsrPwdHash, orgAdminPwdHash, seed,
                                  hashAlgo, discarded);
    }

    void verifyPassword(std::string userName, std::string currentPassword,
                        std::string newPassword)
    {
        nlohmann::json discarded;
        Password::verifyPassword(userName, currentPassword, newPassword,
                                 discarded);
    }
};

class PasswordTest : public bios_config::test::BiosConfigTest
{
  protected:
    void SetUp() override
    {
        bios_config::test::BiosConfigTest::SetUp();
        if (objServer && systemBus)
        {
            password = std::make_unique<Password>(
                *objServer, systemBus, seedPath.parent_path().string());
        }
    }

    void createSeedFile(const std::string& content)
    {
        std::ofstream file(seedPath);
        file << content;
        file.close();
    }

    std::unique_ptr<Password> password;
};

TEST_F(PasswordTest, ConstructorCreatesPassword)
{
    EXPECT_NE(password, nullptr);
}

TEST_F(PasswordTest, ConstructorThrowsWhenPersistPathIsFile)
{
    password.reset();

    const auto filePath = tempDir / "persist_is_file";
    {
        std::ofstream file(filePath);
        ASSERT_TRUE(file.is_open());
    }

    EXPECT_THROW(Password(*objServer, systemBus, filePath.string()),
                 InternalFailure);
}

TEST_F(PasswordTest, ChangePasswordWithInvalidSeedFile)
{
    createSeedFile("invalid json");

    EXPECT_THROW(password->changePassword("user", "oldPassword", "newPassword"),
                 std::exception);
}

TEST_F(PasswordTest, ChangePasswordWithMissingSeedFile)
{
    EXPECT_ANY_THROW(
        password->changePassword("user", "oldPassword", "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithEmptySeedFile)
{
    createSeedFile("{}");
    EXPECT_ANY_THROW(
        password->changePassword("user", "oldPassword", "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithValidSeedFileStructure)
{
    nlohmann::json seedData;
    seedData["hashAlgo"] = "SHA256";
    seedData["seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["userPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["adminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(
        password->changePassword("user", "wrongPassword", "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithUserAndAdmin)
{
    nlohmann::json seedData;
    seedData["hashAlgo"] = "SHA256";
    seedData["seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["userPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["adminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(
        password->changePassword("user", "oldPassword", "newPassword"));
    EXPECT_ANY_THROW(
        password->changePassword("admin", "oldPassword", "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithInvalidUserName)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(
        password->changePassword("invalid", "oldPassword", "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithSHA384)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA384";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(48, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(48, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithInvalidHashAlgorithm)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "MD5";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithEmptyHashValues)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>();
    seedData["AdminPwdHash"] = std::vector<uint8_t>();

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithEmptySeed)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>();
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithEmptyHashAlgo)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithAdminPassword)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordWithUserPassword)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(
        password->changePassword("UserPassword", "oldPassword", "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordHandlesFileStreamOpenFailure)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    std::filesystem::remove(seedPath);
    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordHandlesJsonParseError)
{
    createSeedFile("{ invalid json }");

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordHandlesJsonDiscarded)
{
    createSeedFile("null");

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordHandlesVerifyIntegrityCheckFailureSHA256)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "wrongPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordHandlesVerifyIntegrityCheckFailureSHA384)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA384";
    seedData["Seed"] = std::vector<uint8_t>(48, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(48, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(48, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "wrongPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordHandlesGetParamFailure)
{
    createSeedFile("{}");

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordHandlesIsMatchReturnFalse)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0xFF);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0xFF);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "anyPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, ChangePasswordHandlesUnknownAlgorithm)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "UNKNOWN";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    std::ofstream file(seedPath);
    file << seedData.dump();
    file.close();

    EXPECT_ANY_THROW(password->changePassword("AdminPassword", "oldPassword",
                                              "newPassword"));
}

TEST_F(PasswordTest, CompareDigestReturnsTrueWhenHashesMatch)
{
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};
    std::string testPassword = "testPassword";

    std::array<uint8_t, 64> expected = {0};
    unsigned int hashLen = 32;
    std::vector<uint8_t> output(32);

    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(testPassword.c_str()),
                      testPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha256(), hashLen, output.data());

    std::copy(output.begin(), output.end(), expected.begin());

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result = testablePwd->compareDigest(EVP_sha256(), SHA256_DIGEST_LENGTH,
                                             expected, seed, testPassword);
    EXPECT_TRUE(result);
}

TEST_F(PasswordTest, CompareDigestReturnsFalseWhenHashesDontMatch)
{
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};
    std::string testPassword = "testPassword";
    std::array<uint8_t, 64> expected = {0xFF}; // Wrong hash

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result = testablePwd->compareDigest(EVP_sha256(), SHA256_DIGEST_LENGTH,
                                             expected, seed, testPassword);
    EXPECT_FALSE(result);
}

TEST_F(PasswordTest, IsMatchReturnsTrueForSHA256)
{
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};
    std::string testPassword = "testPassword";

    std::array<uint8_t, 64> expected = {0};
    unsigned int hashLen = 32;
    std::vector<uint8_t> output(32);

    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(testPassword.c_str()),
                      testPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha256(), hashLen, output.data());

    std::copy(output.begin(), output.end(), expected.begin());

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result = testablePwd->isMatch(expected, seed, testPassword, "SHA256");
    EXPECT_TRUE(result);
}

TEST_F(PasswordTest, IsMatchReturnsTrueForSHA384)
{
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};
    std::string testPassword = "testPassword";

    std::array<uint8_t, 64> expected = {0};
    unsigned int hashLen = 48;
    std::vector<uint8_t> output(48);

    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(testPassword.c_str()),
                      testPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha384(), hashLen, output.data());

    std::copy(output.begin(), output.end(), expected.begin());

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result = testablePwd->isMatch(expected, seed, testPassword, "SHA384");
    EXPECT_TRUE(result);
}

TEST_F(PasswordTest, IsMatchReturnsFalseForUnknownAlgorithm)
{
    std::array<uint8_t, 32> seed = {0xAA};
    std::array<uint8_t, 64> expected = {0xFF};
    std::string testPassword = "test";

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result = testablePwd->isMatch(expected, seed, testPassword, "UNKNOWN");
    EXPECT_FALSE(result);
}

TEST_F(PasswordTest, IsMatchReturnsFalseForNonMatchingHash)
{
    std::array<uint8_t, 32> seed = {0xAA};
    std::array<uint8_t, 64> expected = {0xFF};
    std::string testPassword = "testPassword";

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result = testablePwd->isMatch(expected, seed, testPassword, "SHA256");
    EXPECT_FALSE(result);
}

TEST_F(PasswordTest, VerifyIntegrityCheckReturnsTrueForValidPassword)
{
    std::string newPassword = "newPassword123";
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result =
        testablePwd->verifyIntegrityCheck(newPassword, seed, 32, EVP_sha256());
    EXPECT_TRUE(result);
}

TEST_F(PasswordTest, VerifyIntegrityCheckReturnsTrueForSHA384)
{
    std::string newPassword = "newPassword123";
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result =
        testablePwd->verifyIntegrityCheck(newPassword, seed, 48, EVP_sha384());
    EXPECT_TRUE(result);
}

TEST_F(PasswordTest, CompareDigestThrowsWhenOpenSslRejectsDigest)
{
    std::array<uint8_t, maxSeedSize> seed = {0xAA};
    std::array<uint8_t, maxHashSize> expected = {0};

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    EXPECT_THROW(testablePwd->compareDigest(nullptr, SHA256_DIGEST_LENGTH,
                                            expected, seed, "password"),
                 InternalFailure);
}

TEST_F(PasswordTest, VerifyIntegrityCheckReturnsFalseWhenOpenSslRejectsDigest)
{
    std::string newPassword = "newPassword123";
    std::array<uint8_t, maxSeedSize> seed = {0xAA};

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    EXPECT_FALSE(
        testablePwd->verifyIntegrityCheck(newPassword, seed, 32, nullptr));
}

TEST_F(PasswordTest, GetParamReturnsTrueForValidJsonFile)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(64, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(64, 0x11);

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    createSeedFile(seedData.dump());

    ASSERT_TRUE(std::filesystem::exists(seedPath)) << "Seed file should exist";

    std::array<uint8_t, 64> orgUsrPwdHash = {0};
    std::array<uint8_t, 64> orgAdminPwdHash = {0};
    std::array<uint8_t, 32> seed = {0};
    std::string hashAlgo;

    bool result =
        testablePwd->getParam(orgUsrPwdHash, orgAdminPwdHash, seed, hashAlgo);
    EXPECT_TRUE(result);
    ASSERT_FALSE(hashAlgo.empty())
        << "hashAlgo should be set if file exists and JSON is valid";
    EXPECT_EQ(hashAlgo, "SHA256");
}

TEST_F(PasswordTest, GetParamReturnsFalseForInvalidJson)
{
    createSeedFile("{ invalid json }");

    std::array<uint8_t, 64> orgUsrPwdHash;
    std::array<uint8_t, 64> orgAdminPwdHash;
    std::array<uint8_t, 32> seed;
    std::string hashAlgo;

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result =
        testablePwd->getParam(orgUsrPwdHash, orgAdminPwdHash, seed, hashAlgo);
    EXPECT_TRUE(!result || hashAlgo.empty())
        << "Invalid JSON: getParam should return false or leave hashAlgo empty";
}

TEST_F(PasswordTest, GetParamReturnsFalseForMissingFile)
{
    if (std::filesystem::exists(seedPath))
    {
        std::filesystem::remove(seedPath);
    }

    std::array<uint8_t, 64> orgUsrPwdHash;
    std::array<uint8_t, 64> orgAdminPwdHash;
    std::array<uint8_t, 32> seed;
    std::string hashAlgo;

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    // A missing seed file must fail closed: previously this returned true
    // with uninitialised hashes, letting callers proceed unverified.
    bool result =
        testablePwd->getParam(orgUsrPwdHash, orgAdminPwdHash, seed, hashAlgo);
    EXPECT_FALSE(result);
}

TEST_F(PasswordTest, GetParamHandlesDiscardedJson)
{
    std::ofstream file(seedPath);
    file << "null";
    file.close();

    std::array<uint8_t, 64> orgUsrPwdHash;
    std::array<uint8_t, 64> orgAdminPwdHash;
    std::array<uint8_t, 32> seed;
    std::string hashAlgo;

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result =
        testablePwd->getParam(orgUsrPwdHash, orgAdminPwdHash, seed, hashAlgo);
    (void)result;
}

TEST_F(PasswordTest, VerifyPasswordThrowsWhenSeedFileDoesNotExist)
{
    if (std::filesystem::exists(seedPath))
    {
        std::filesystem::remove(seedPath);
    }

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    EXPECT_THROW(testablePwd->verifyPassword("AdminPassword", "old", "new"),
                 InternalFailure);
}

TEST_F(PasswordTest, VerifyPasswordThrowsWhenGetParamFails)
{
    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    createSeedFile("{\"key\": [unclosed");

    try
    {
        testablePwd->verifyPassword("AdminPassword", "old", "new");
    }
    catch (...)
    {}
}

TEST_F(PasswordTest, VerifyPasswordReturnsEarlyWhenParamsAreEmpty)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "";
    seedData["Seed"] = std::vector<uint8_t>();
    seedData["UserPwdHash"] = std::vector<uint8_t>();
    seedData["AdminPwdHash"] = std::vector<uint8_t>();

    createSeedFile(seedData.dump());

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    try
    {
        testablePwd->verifyPassword("AdminPassword", "old", "new");
    }
    catch (const InternalFailure&)
    {}
}

TEST_F(PasswordTest, VerifyPasswordChecksAdminPasswordWithSHA256)
{
    std::string testPassword = "adminPass123";
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};

    std::array<uint8_t, 64> adminHash = {0};
    unsigned int hashLen = 32;
    std::vector<uint8_t> output(32);

    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(testPassword.c_str()),
                      testPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha256(), hashLen, output.data());

    std::copy(output.begin(), output.end(), adminHash.begin());

    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(seed.begin(), seed.end());
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] =
        std::vector<uint8_t>(adminHash.begin(), adminHash.begin() + 32);

    createSeedFile(seedData.dump());

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    auto threwInvalidCurrentPassword = [&]() {
        try
        {
            testablePwd->verifyPassword("AdminPassword", testPassword,
                                        "newPassword");
            return false;
        }
        catch (const InvalidCurrentPassword&)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }
    };
    EXPECT_FALSE(threwInvalidCurrentPassword());
}

TEST_F(PasswordTest, VerifyPasswordChecksUserPasswordWithSHA384)
{
    std::string testPassword = "userPass123";
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};

    std::array<uint8_t, 64> userHash = {0};
    unsigned int hashLen = 48;
    std::vector<uint8_t> output(48);

    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(testPassword.c_str()),
                      testPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha384(), hashLen, output.data());

    std::copy(output.begin(), output.end(), userHash.begin());

    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA384";
    seedData["Seed"] = std::vector<uint8_t>(seed.begin(), seed.end());
    seedData["UserPwdHash"] =
        std::vector<uint8_t>(userHash.begin(), userHash.begin() + 48);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);

    createSeedFile(seedData.dump());

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    auto threwInvalidCurrentPassword = [&]() {
        try
        {
            testablePwd->verifyPassword("UserPassword", testPassword,
                                        "newPassword");
            return false;
        }
        catch (const InvalidCurrentPassword&)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }
    };
    EXPECT_FALSE(threwInvalidCurrentPassword());
}

TEST_F(PasswordTest, VerifyPasswordThrowsInvalidCurrentPasswordForWrongPassword)
{
    std::string correctPassword = "correctPass";
    std::string wrongPassword = "wrongPass";
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};

    std::array<uint8_t, 64> adminHash = {0};
    unsigned int hashLen = 32;
    std::vector<uint8_t> output(32);

    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(correctPassword.c_str()),
                      correctPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha256(), hashLen, output.data());

    std::copy(output.begin(), output.end(), adminHash.begin());

    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(seed.begin(), seed.end());
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] =
        std::vector<uint8_t>(adminHash.begin(), adminHash.begin() + 32);

    createSeedFile(seedData.dump());

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    EXPECT_ANY_THROW(
        testablePwd->verifyPassword("AdminPassword", wrongPassword, "new"));
}

TEST_F(PasswordTest,
       VerifyPasswordUserPathThrowsInvalidCurrentPasswordForWrongPassword)
{
    std::string testPassword = "userPass123";
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};
    std::array<uint8_t, 64> userHash = {0};
    unsigned int hashLen = 32;
    std::vector<uint8_t> output(32);
    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(testPassword.c_str()),
                      testPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha256(), hashLen, output.data());
    std::copy(output.begin(), output.end(), userHash.begin());

    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(seed.begin(), seed.end());
    seedData["UserPwdHash"] =
        std::vector<uint8_t>(userHash.begin(), userHash.begin() + 32);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0x00);
    createSeedFile(seedData.dump());

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    EXPECT_ANY_THROW(
        testablePwd->verifyPassword("UserPassword", "wrong", "newPassword"));
}

TEST_F(PasswordTest,
       VerifyPasswordAdminPathThrowsInvalidCurrentPasswordForWrongPassword)
{
    std::string testPassword = "adminPass123";
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};
    std::array<uint8_t, 64> adminHash = {0};
    unsigned int hashLen = 32;
    std::vector<uint8_t> output(32);
    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(testPassword.c_str()),
                      testPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha256(), hashLen, output.data());
    std::copy(output.begin(), output.end(), adminHash.begin());

    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(seed.begin(), seed.end());
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0x00);
    seedData["AdminPwdHash"] =
        std::vector<uint8_t>(adminHash.begin(), adminHash.begin() + 32);
    createSeedFile(seedData.dump());

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    EXPECT_ANY_THROW(
        testablePwd->verifyPassword("AdminPassword", "wrong", "newPassword"));
}

TEST_F(PasswordTest, GetParamHandlesNlohmannDetailException)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = "not-an-array";
    seedData["UserPwdHash"] = std::vector<uint8_t>(32, 0);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(32, 0);
    createSeedFile(seedData.dump());

    std::array<uint8_t, 64> orgUsrPwdHash;
    std::array<uint8_t, 64> orgAdminPwdHash;
    std::array<uint8_t, 32> seed;
    std::string hashAlgo;

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result =
        testablePwd->getParam(orgUsrPwdHash, orgAdminPwdHash, seed, hashAlgo);
    EXPECT_FALSE(result);
}

TEST_F(PasswordTest, GetParamWithFullSizeArrays)
{
    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(32, 0xAA);
    seedData["UserPwdHash"] = std::vector<uint8_t>(64, 0x00);
    seedData["AdminPwdHash"] = std::vector<uint8_t>(64, 0x11);
    createSeedFile(seedData.dump());

    std::array<uint8_t, 64> orgUsrPwdHash;
    std::array<uint8_t, 64> orgAdminPwdHash;
    std::array<uint8_t, 32> seed;
    std::string hashAlgo;

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());
    bool result =
        testablePwd->getParam(orgUsrPwdHash, orgAdminPwdHash, seed, hashAlgo);
    EXPECT_TRUE(result);
    EXPECT_EQ(hashAlgo, "SHA256");
    EXPECT_EQ(orgAdminPwdHash[0], 0x11);
    EXPECT_EQ(seed[0], 0xAA);
}

TEST_F(PasswordTest, ChangePasswordSucceedsAndUpdatesSeedFile)
{
    const std::string currentPassword = "old";
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};
    std::array<uint8_t, 64> adminHash = {0};
    unsigned int hashLen = 32;
    std::vector<uint8_t> output(32);
    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(currentPassword.c_str()),
                      currentPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha256(), hashLen, output.data());
    std::copy(output.begin(), output.end(), adminHash.begin());

    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(seed.begin(), seed.end());
    seedData["UserPwdHash"] = std::vector<uint8_t>(64, 0x00);
    seedData["AdminPwdHash"] =
        std::vector<uint8_t>(adminHash.begin(), adminHash.end());
    createSeedFile(seedData.dump());

    password.reset();
    auto pwd = std::make_unique<Password>(*objServer, systemBus,
                                          seedPath.parent_path().string());
    EXPECT_NO_THROW(
        pwd->changePassword("AdminPassword", currentPassword, "new"));

    std::ifstream ifs(seedPath);
    nlohmann::json readBack = nlohmann::json::parse(ifs);
    EXPECT_TRUE(readBack.contains("IsAdminPwdChanged"));
    EXPECT_TRUE(readBack["IsAdminPwdChanged"].get<bool>());
    EXPECT_TRUE(readBack.contains("AdminPwdHash"));
    std::vector<uint8_t> newHash = readBack["AdminPwdHash"];
    EXPECT_EQ(newHash.size(), 64u);
}

// Helper: write a seed file with 64-byte hashes (the correct size for
// maxHashSize=64)
static void createSeedFile64(
    const std::filesystem::path& path, const std::string& algo,
    const std::vector<uint8_t>& userHash64,
    const std::vector<uint8_t>& adminHash64, const std::vector<uint8_t>& seed32)
{
    nlohmann::json j;
    j["HashAlgo"] = algo;
    j["Seed"] = seed32;
    j["UserPwdHash"] = userHash64;
    j["AdminPwdHash"] = adminHash64;
    std::ofstream f(path);
    f << j.dump();
}

static std::vector<uint8_t> computePbkdf2Sha256(
    const std::string& password, const std::vector<uint8_t>& seed)
{
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    int rc = PKCS5_PBKDF2_HMAC(
        password.c_str(), static_cast<int>(password.size() + 1), seed.data(),
        static_cast<int>(seed.size()), 1000, EVP_sha256(), SHA256_DIGEST_LENGTH,
        out.data());
    EXPECT_EQ(rc, 1) << "PKCS5_PBKDF2_HMAC(SHA256) failed";
    return out;
}

static std::vector<uint8_t> computePbkdf2Sha384(
    const std::string& password, const std::vector<uint8_t>& seed)
{
    std::vector<uint8_t> out(SHA384_DIGEST_LENGTH);
    int rc = PKCS5_PBKDF2_HMAC(
        password.c_str(), static_cast<int>(password.size() + 1), seed.data(),
        static_cast<int>(seed.size()), 1000, EVP_sha384(), SHA384_DIGEST_LENGTH,
        out.data());
    EXPECT_EQ(rc, 1) << "PKCS5_PBKDF2_HMAC(SHA384) failed";
    return out;
}

// Pads a hash vector to 64 bytes (maxHashSize).
static std::vector<uint8_t> pad64(const std::vector<uint8_t>& v)
{
    std::vector<uint8_t> out(64, 0);
    std::copy_n(v.begin(), std::min(v.size(), out.size()), out.begin());
    return out;
}

// --- Tests that use correct 64-byte hash arrays so getParam succeeds ---

TEST_F(PasswordTest, VerifyPasswordUserPathReachesIsMatchWithCorrectPassword)
{
    // Tests the else (non-AdminPassword) branch in verifyPassword with 64-byte
    // hashes.
    const std::string pwd = "userPass42";
    const std::vector<uint8_t> seed32(32, 0xAB);
    auto hash = computePbkdf2Sha256(pwd, seed32);

    createSeedFile64(seedPath, "SHA256", pad64(hash),
                     std::vector<uint8_t>(64, 0x00), seed32);

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    // Correct UserPassword: should not throw InvalidCurrentPassword
    EXPECT_NO_THROW(
        testablePwd->verifyPassword("UserPassword", pwd, "newPass"));
}

TEST_F(PasswordTest,
       VerifyPasswordUserPathThrowsOnWrongPasswordWith64ByteHashes)
{
    // Tests that wrong password on the user path throws InvalidCurrentPassword.
    const std::vector<uint8_t> seed32(32, 0xCD);
    // Hash is all-zeros which won't match any real password
    createSeedFile64(seedPath, "SHA256",
                     std::vector<uint8_t>(64, 0x01), // userHash: non-matching
                     std::vector<uint8_t>(64, 0x00), // adminHash
                     seed32);

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    EXPECT_THROW(
        testablePwd->verifyPassword("UserPassword", "wrongPass", "newPass"),
        InvalidCurrentPassword);
}

TEST_F(PasswordTest,
       VerifyPasswordAdminPathReachesIsMatchWithCorrectPasswordSHA384)
{
    // Tests the AdminPassword path with SHA384 so line 188 is covered.
    const std::string pwd = "adminPass99";
    const std::vector<uint8_t> seed32(32, 0xEF);
    auto hash = computePbkdf2Sha384(pwd, seed32);

    createSeedFile64(seedPath, "SHA384",
                     std::vector<uint8_t>(64, 0x00), // userHash
                     pad64(hash),                    // adminHash
                     seed32);

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    // Correct AdminPassword with SHA384: should not throw
    // InvalidCurrentPassword
    EXPECT_NO_THROW(
        testablePwd->verifyPassword("AdminPassword", pwd, "newPass"));
}

TEST_F(PasswordTest, VerifyPasswordUserPathSHA384WithCorrectPassword)
{
    // Tests user path + SHA384 algorithm with correct 64-byte hashes.
    const std::string pwd = "userPassSHA384";
    const std::vector<uint8_t> seed32(32, 0x12);
    auto hash = computePbkdf2Sha384(pwd, seed32);

    createSeedFile64(seedPath, "SHA384",
                     pad64(hash),                    // userHash
                     std::vector<uint8_t>(64, 0x00), // adminHash
                     seed32);

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    EXPECT_NO_THROW(
        testablePwd->verifyPassword("UserPassword", pwd, "newPass"));
}

TEST_F(PasswordTest, ChangePasswordUserPathSucceedsWithCorrect64ByteHashes)
{
    // Tests changePassword succeeds on user path with 64-byte hashes.
    const std::string currentPwd = "currentU";
    const std::vector<uint8_t> seed32(32, 0x34);
    auto hash = computePbkdf2Sha256(currentPwd, seed32);

    createSeedFile64(seedPath, "SHA256", pad64(hash),
                     std::vector<uint8_t>(64, 0x00), seed32);

    password.reset();
    auto pwd = std::make_unique<Password>(*objServer, systemBus,
                                          seedPath.parent_path().string());

    EXPECT_NO_THROW(
        pwd->changePassword("UserPassword", currentPwd, "newPass123"));
}

TEST_F(PasswordTest, VerifyPasswordThrowsWhenHashAlgoEmptyWith64ByteArrays)
{
    // HashAlgo="" + 64-byte arrays. This previously returned early WITHOUT
    // verifying the password, which is the fail-open the hashAlgo allowlist
    // closes: only SHA256 and SHA384 are accepted, anything else throws.
    const std::vector<uint8_t> seed32(32, 0x56);
    createSeedFile64(seedPath, "",                   // empty HashAlgo
                     std::vector<uint8_t>(64, 0x00), // UserPwdHash
                     std::vector<uint8_t>(64, 0x00), // AdminPwdHash
                     seed32);

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    // Must fail closed: an unrecognised hashAlgo cannot silently skip
    // verification.
    EXPECT_THROW(
        testablePwd->verifyPassword("AdminPassword", "anyPwd", "newPwd"),
        InternalFailure);
}

TEST_F(PasswordTest,
       VerifyPasswordAdminPathThrowsInvalidCurrentPasswordWith64ByteHashes)
{
    // Wrong admin password with 64-byte hashes: covers the
    // isMatch-returns-false throw branch (line 169 ft=True) in verifyPassword's
    // admin path.
    const std::vector<uint8_t> seed32(32, 0x78);
    // AdminPwdHash set to all-0xFF so no real password will match.
    createSeedFile64(seedPath, "SHA256",
                     std::vector<uint8_t>(64, 0x00), // UserPwdHash
                     std::vector<uint8_t>(64, 0xFF), // AdminPwdHash: mismatch
                     seed32);

    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    EXPECT_THROW(
        testablePwd->verifyPassword("AdminPassword", "wrongAdminPwd", "newPwd"),
        InvalidCurrentPassword);
}

TEST_F(PasswordTest, ChangePasswordSurvivesSeedFileRemovalAfterVerify)
{
    const std::string currentPwd = "fifoCurrent";
    const std::vector<uint8_t> seed32(32, 0x9A);
    const auto hash = computePbkdf2Sha256(currentPwd, seed32);

    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = seed32;
    seedData["UserPwdHash"] = std::vector<uint8_t>(64, 0x00);
    seedData["AdminPwdHash"] = pad64(hash);

    if (std::filesystem::exists(seedPath))
    {
        std::filesystem::remove(seedPath);
    }
    ASSERT_EQ(mkfifo(seedPath.c_str(), 0600), 0) << std::strerror(errno);

    std::thread writer([this, payload = seedData.dump()]() {
        std::ofstream fifo(seedPath);
        std::filesystem::remove(seedPath);
        fifo << payload;
    });

    password.reset();
    auto pwd = std::make_unique<Password>(*objServer, systemBus,
                                          seedPath.parent_path().string());
    // changePassword reuses the JSON verified by verifyPassword instead of
    // re-reading the seed file, so the file vanishing in between no longer
    // aborts the change. This is the TOCTOU window being closed.
    EXPECT_NO_THROW(pwd->changePassword("AdminPassword", currentPwd, "newPwd"));

    writer.join();
}

TEST_F(PasswordTest, ChangePasswordRoutesUserAccountToUserHash)
{
    const std::string currentPassword = "old";
    std::array<uint8_t, 32> seed = {0xAA, 0xBB, 0xCC, 0xDD};
    std::array<uint8_t, 64> userHash = {0};
    unsigned int hashLen = 32;
    std::vector<uint8_t> output(32);
    PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(currentPassword.c_str()),
                      currentPassword.length() + 1,
                      reinterpret_cast<const unsigned char*>(seed.data()),
                      seed.size(), 1000, EVP_sha256(), hashLen, output.data());
    std::copy(output.begin(), output.end(), userHash.begin());

    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(seed.begin(), seed.end());
    seedData["UserPwdHash"] =
        std::vector<uint8_t>(userHash.begin(), userHash.end());
    seedData["AdminPwdHash"] = std::vector<uint8_t>(64, 0x00);
    createSeedFile(seedData.dump());

    password.reset();
    auto pwd = std::make_unique<Password>(*objServer, systemBus,
                                          seedPath.parent_path().string());
    // Any non-"AdminPassword" userName routes to the user account.
    EXPECT_NO_THROW(
        pwd->changePassword("UserPassword", currentPassword, "new"));

    std::ifstream ifs(seedPath);
    nlohmann::json readBack = nlohmann::json::parse(ifs);
    EXPECT_TRUE(readBack.contains("IsUserPwdChanged"));
    EXPECT_TRUE(readBack["IsUserPwdChanged"].get<bool>());

    // The user hash must actually be rewritten, not just the flag set.
    std::vector<uint8_t> userAfter = readBack["UserPwdHash"];
    EXPECT_NE(userAfter, std::vector<uint8_t>(64, 0x00));
    // The admin hash must be left untouched (all-zero).
    std::vector<uint8_t> adminAfter = readBack["AdminPwdHash"];
    EXPECT_EQ(adminAfter, std::vector<uint8_t>(64, 0x00));
}

// --- Coverage for the input-validation and lockout guards ---

TEST_F(PasswordTest, ChangePasswordRejectsEmptyCurrentWhenConfigured)
{
    const std::string configured = "alreadySet";
    const std::vector<uint8_t> seed32(32, 0x11);
    const auto hash = computePbkdf2Sha256(configured, seed32);
    createSeedFile64(seedPath, "SHA256", std::vector<uint8_t>(64, 0x00),
                     pad64(hash), seed32);
    password.reset();
    auto pwd = std::make_unique<Password>(*objServer, systemBus,
                                          seedPath.parent_path().string());
    EXPECT_THROW(pwd->changePassword("AdminPassword", "", "newPwd"),
                 InvalidCurrentPassword);
}

TEST_F(PasswordTest, ChangePasswordAllowsEmptyCurrentWhenUnprovisioned)
{
    const std::vector<uint8_t> seed32(32, 0x21);
    const auto emptyHash = computePbkdf2Sha256("", seed32);
    createSeedFile64(seedPath, "SHA256", std::vector<uint8_t>(64, 0x00),
                     pad64(emptyHash), seed32);
    password.reset();
    auto pwd = std::make_unique<Password>(*objServer, systemBus,
                                          seedPath.parent_path().string());

    EXPECT_NO_THROW(pwd->changePassword("AdminPassword", "", "newPwd"));

    std::ifstream ifs(seedPath);
    nlohmann::json readBack = nlohmann::json::parse(ifs);
    EXPECT_TRUE(readBack["IsAdminPwdChanged"].get<bool>());
    std::vector<uint8_t> adminAfter = readBack["AdminPwdHash"];
    EXPECT_NE(adminAfter, pad64(emptyHash));
}

TEST_F(PasswordTest, ChangePasswordRejectsEmptyNewPassword)
{
    const std::vector<uint8_t> seed32(32, 0x12);
    createSeedFile64(seedPath, "SHA256", std::vector<uint8_t>(64, 0x00),
                     std::vector<uint8_t>(64, 0x00), seed32);
    password.reset();
    auto pwd = std::make_unique<Password>(*objServer, systemBus,
                                          seedPath.parent_path().string());
    EXPECT_THROW(pwd->changePassword("AdminPassword", "current", ""),
                 InvalidCurrentPassword);
}

TEST_F(PasswordTest, ChangePasswordRejectsOversizedNewPassword)
{
    const std::vector<uint8_t> seed32(32, 0x13);
    createSeedFile64(seedPath, "SHA256", std::vector<uint8_t>(64, 0x00),
                     std::vector<uint8_t>(64, 0x00), seed32);
    password.reset();
    auto pwd = std::make_unique<Password>(*objServer, systemBus,
                                          seedPath.parent_path().string());
    const std::string tooLong(maxPasswordLen + 1, 'x');
    EXPECT_THROW(pwd->changePassword("AdminPassword", "current", tooLong),
                 InvalidCurrentPassword);
}

TEST_F(PasswordTest, VerifyPasswordRejectsEmptyAndOversizedPasswords)
{
    const std::vector<uint8_t> seed32(32, 0x14);
    createSeedFile64(seedPath, "SHA256", std::vector<uint8_t>(64, 0x00),
                     std::vector<uint8_t>(64, 0x00), seed32);
    password.reset();
    auto testablePwd = std::make_unique<TestablePassword>(
        *objServer, systemBus, seedPath.parent_path().string());

    EXPECT_THROW(testablePwd->verifyPassword("AdminPassword", "", "newPwd"),
                 InvalidCurrentPassword);
    EXPECT_THROW(testablePwd->verifyPassword("AdminPassword", "current", ""),
                 InvalidCurrentPassword);
    const std::string tooLong(maxPasswordLen + 1, 'y');
    EXPECT_THROW(
        testablePwd->verifyPassword("AdminPassword", "current", tooLong),
        InvalidCurrentPassword);
}

TEST_F(PasswordTest, ChangePasswordLocksOutAfterRepeatedFailures)
{
    const std::string correct = "rightPwd";
    const std::vector<uint8_t> seed32(32, 0x15);
    const auto hash = computePbkdf2Sha256(correct, seed32);
    createSeedFile64(seedPath, "SHA256", std::vector<uint8_t>(64, 0x00),
                     pad64(hash), seed32);

    password.reset();
    auto pwd = std::make_unique<Password>(*objServer, systemBus,
                                          seedPath.parent_path().string());

    // Wrong password rejected with InvalidCurrentPassword up to the limit.
    for (uint8_t i = 0; i < maxFailedAttempts; ++i)
    {
        EXPECT_THROW(pwd->changePassword("AdminPassword", "wrongPwd", "newPwd"),
                     InvalidCurrentPassword);
    }

    // Once the limit is reached the caller is locked out, and even the
    // correct password is refused with InternalFailure.
    EXPECT_THROW(pwd->changePassword("AdminPassword", correct, "newPwd"),
                 InternalFailure);
}

} // namespace bios_config_pwd::test
