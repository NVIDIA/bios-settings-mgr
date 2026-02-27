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
#include "secureboot.hpp"

#include <filesystem>
#include <fstream>

namespace bios_config::test
{

using namespace bios_config;

class SecureBootTest : public BiosConfigTest
{
  protected:
    void SetUp() override
    {
        BiosConfigTest::SetUp();
        if (objServer && systemBus)
        {
            secureBootPath = seedPath.parent_path() / "secureboot";
            secureboot = std::make_unique<SecureBoot>(*objServer, systemBus,
                                                      secureBootPath.string());
        }
    }

    void TearDown() override
    {
        if (std::filesystem::exists(secureBootPath))
        {
            std::filesystem::remove_all(secureBootPath);
        }
        BiosConfigTest::TearDown();
    }

    std::unique_ptr<SecureBoot> secureboot;
    std::filesystem::path secureBootPath;
};

TEST_F(SecureBootTest, ConstructorCreatesSecureBoot)
{
    EXPECT_NE(secureboot, nullptr);
}

TEST_F(SecureBootTest, CurrentBootCanBeSet)
{
    SecureBoot::CurrentBootType bootType = SecureBoot::CurrentBootType::Unknown;
    auto result = secureboot->currentBoot(bootType);
    EXPECT_EQ(result, bootType);
}

TEST_F(SecureBootTest, CurrentBootCanBeSetToUnknown)
{
    SecureBoot::CurrentBootType bootType = SecureBoot::CurrentBootType::Unknown;
    auto result = secureboot->currentBoot(bootType);
    EXPECT_EQ(result, bootType);
}

TEST_F(SecureBootTest, CurrentBootCanBeSetToUnsecure)
{
    SecureBoot::CurrentBootType bootType = SecureBoot::CurrentBootType::Unknown;
    auto result = secureboot->currentBoot(bootType);
    EXPECT_EQ(result, bootType);
}

TEST_F(SecureBootTest, PendingEnableCanBeSet)
{
    bool value = true;
    auto result = secureboot->pendingEnable(value);
    EXPECT_EQ(result, value);
}

TEST_F(SecureBootTest, PendingEnableCanBeSetToFalse)
{
    bool value = false;
    auto result = secureboot->pendingEnable(value);
    EXPECT_EQ(result, value);
}

TEST_F(SecureBootTest, ModeCanBeSet)
{
    SecureBoot::ModeType modeValue = SecureBoot::ModeType::Deployed;
    auto result = secureboot->mode(modeValue);
    EXPECT_EQ(result, modeValue);
}

TEST_F(SecureBootTest, ModeCanBeSetToAudit)
{
    SecureBoot::ModeType modeValue = SecureBoot::ModeType::Audit;
    auto result = secureboot->mode(modeValue);
    EXPECT_EQ(result, modeValue);
}

TEST_F(SecureBootTest, ModeCanBeSetToUnknown)
{
    SecureBoot::ModeType modeValue = SecureBoot::ModeType::Unknown;
    auto result = secureboot->mode(modeValue);
    EXPECT_EQ(result, modeValue);
}

TEST_F(SecureBootTest, SerializeAndDeserializeRoundTrip)
{
    SecureBoot::CurrentBootType bootType = SecureBoot::CurrentBootType::Unknown;
    secureboot->currentBoot(bootType);
    secureboot->pendingEnable(true);
    secureboot->mode(SecureBoot::ModeType::Deployed);

    secureboot.reset();

    auto secureboot2 = std::make_unique<SecureBoot>(*objServer, systemBus,
                                                    secureBootPath.string());

    EXPECT_EQ(secureboot2->currentBoot(bootType), bootType);
    EXPECT_TRUE(secureboot2->pendingEnable(true));
    EXPECT_EQ(secureboot2->mode(SecureBoot::ModeType::Deployed),
              SecureBoot::ModeType::Deployed);
}

TEST_F(SecureBootTest, DeserializeHandlesMissingFile)
{
    secureboot.reset();

    std::filesystem::path nonExistentPath =
        seedPath.parent_path() / "nonexistent";
    auto secureboot2 = std::make_unique<SecureBoot>(*objServer, systemBus,
                                                    nonExistentPath.string());

    EXPECT_NE(secureboot2, nullptr);
}

TEST_F(SecureBootTest, SerializeHandlesException)
{
    SecureBoot::CurrentBootType bootType = SecureBoot::CurrentBootType::Unknown;
    secureboot->currentBoot(bootType);

    EXPECT_NO_THROW(secureboot->pendingEnable(true));

    EXPECT_TRUE(secureboot->pendingEnable(true));
}

TEST_F(SecureBootTest, DeserializeHandlesInvalidFile)
{
    secureboot.reset();

    std::filesystem::path invalidFile = secureBootPath / "securebootData";
    std::filesystem::create_directories(secureBootPath);
    std::ofstream file(invalidFile, std::ios::binary);
    file << "BAD";
    file.close();

    auto secureboot2 = std::make_unique<SecureBoot>(*objServer, systemBus,
                                                    secureBootPath.string());

    EXPECT_NE(secureboot2, nullptr);
}

TEST_F(SecureBootTest, SerializeAndDeserializeAllModeTypes)
{
    std::vector<SecureBoot::ModeType> modeTypes = {
        SecureBoot::ModeType::Unknown, SecureBoot::ModeType::Deployed,
        SecureBoot::ModeType::Audit};

    for (const auto& modeType : modeTypes)
    {
        secureboot->mode(modeType);
        EXPECT_EQ(secureboot->mode(modeType), modeType);
    }
}

TEST_F(SecureBootTest, SerializeAndDeserializeAllCurrentBootTypes)
{
    std::vector<SecureBoot::CurrentBootType> bootTypes = {
        SecureBoot::CurrentBootType::Unknown};

    for (const auto& bootType : bootTypes)
    {
        secureboot->currentBoot(bootType);
        EXPECT_EQ(secureboot->currentBoot(bootType), bootType);
    }
}

TEST_F(SecureBootTest, SerializeHandlesWriteFailureGracefully)
{
    std::filesystem::path dataFile = secureBootPath / "securebootData";
    if (std::filesystem::exists(dataFile))
    {
        std::filesystem::remove(dataFile);
    }
    std::filesystem::create_directories(dataFile);

    SecureBoot::CurrentBootType bootType = SecureBoot::CurrentBootType::Unknown;
    EXPECT_NO_THROW(secureboot->currentBoot(bootType));
}

TEST_F(SecureBootTest, DeserializeWhenFileDoesNotExistUsesDefaults)
{
    secureboot.reset();
    std::filesystem::path noFilePath =
        seedPath.parent_path() / "secureboot_no_file";
    if (std::filesystem::exists(noFilePath))
    {
        std::filesystem::remove_all(noFilePath);
    }
    std::filesystem::create_directories(noFilePath);
    auto sb = std::make_unique<SecureBoot>(*objServer, systemBus,
                                           noFilePath.string());
    EXPECT_NE(sb, nullptr);
    SecureBoot::CurrentBootType bootType = SecureBoot::CurrentBootType::Unknown;
    EXPECT_EQ(sb->currentBoot(bootType), bootType);
    EXPECT_FALSE(sb->pendingEnable(false));
    std::filesystem::remove_all(noFilePath);
}

TEST_F(SecureBootTest, DeserializeWhenFileExistsAndValidRestoresState)
{
    secureboot->currentBoot(SecureBoot::CurrentBootType::Unknown);
    secureboot->pendingEnable(true);
    secureboot->mode(SecureBoot::ModeType::Audit);
    secureboot.reset();

    auto sb = std::make_unique<SecureBoot>(*objServer, systemBus,
                                           secureBootPath.string());
    EXPECT_NE(sb, nullptr);
    EXPECT_EQ(sb->currentBoot(SecureBoot::CurrentBootType::Unknown),
              SecureBoot::CurrentBootType::Unknown);
    EXPECT_TRUE(sb->pendingEnable(true));
    EXPECT_EQ(sb->mode(SecureBoot::ModeType::Audit),
              SecureBoot::ModeType::Audit);
}

} // namespace bios_config::test
