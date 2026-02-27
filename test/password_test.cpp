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

#include <nlohmann/json.hpp>
#include <xyz/openbmc_project/BIOSConfig/Common/error.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <filesystem>
#include <fstream>

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
    bool result =
        testablePwd->getParam(orgUsrPwdHash, orgAdminPwdHash, seed, hashAlgo);
    EXPECT_TRUE(result);
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

} // namespace bios_config_pwd::test
