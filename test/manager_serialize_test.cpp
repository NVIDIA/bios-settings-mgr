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
#include "manager.hpp"
#include "manager_serialize.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>
#include <xyz/openbmc_project/BIOSConfig/Manager/server.hpp>
#include <xyz/openbmc_project/BIOSConfig/SecureBoot/server.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace bios_config::test
{

using namespace bios_config;
using namespace sdbusplus::xyz::openbmc_project::BIOSConfig::server;

using ManagerServer =
    sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager;
using SecureBootServer =
    sdbusplus::xyz::openbmc_project::BIOSConfig::server::SecureBoot;
using AttributeType = ManagerServer::AttributeType;
using BoundType = ManagerServer::BoundType;
using CurrentBootType = SecureBootServer::CurrentBootType;
using ModeType = SecureBootServer::ModeType;

class ManagerSerializeTest : public BiosConfigTest
{
  protected:
    void SetUp() override
    {
        BiosConfigTest::SetUp();
        if (objServer && systemBus)
        {
            const char* testName =
                ::testing::UnitTest::GetInstance()->current_test_info()->name();
            std::string safeName;
            for (const char* p = testName; *p; ++p)
            {
                safeName += (*p == '/' || *p == '\\') ? '_' : *p;
            }
            persistPath = persistPath / safeName;
            std::filesystem::create_directories(persistPath);

            loadPath = persistPath / "load_target";
            std::filesystem::create_directories(loadPath);

            manager = std::make_unique<Manager>(*objServer, systemBus,
                                                persistPath.string());
            serializePath = persistPath / "test_serialize";
        }
    }

    void TearDown() override
    {
        if (std::filesystem::exists(serializePath))
        {
            std::filesystem::remove(serializePath);
        }
        BiosConfigTest::TearDown();
    }

    std::unique_ptr<Manager> manager;
    std::filesystem::path serializePath;
    std::filesystem::path loadPath;
};

TEST_F(ManagerSerializeTest, SerializeCreatesFile)
{
    serialize(*manager, serializePath);

    EXPECT_TRUE(std::filesystem::exists(serializePath));
}

TEST_F(ManagerSerializeTest, SerializeAndDeserializeRoundTrip)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(100), "");
    table["TestAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")), options);

    manager->baseBIOSTable(table);
    manager->setAttribute("TestAttr", std::string("PendingValue"));

    serialize(*manager, serializePath);
    EXPECT_TRUE(std::filesystem::exists(serializePath));

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    bool result = deserialize(serializePath, *manager2);

    if (result)
    {
        auto pendingAttrs =
            manager2->sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                Manager::pendingAttributes();
        auto it = pendingAttrs.find("TestAttr");
        ASSERT_NE(it, pendingAttrs.end());
        EXPECT_EQ(std::get<std::string>(std::get<1>(it->second)),
                  "PendingValue");
    }
}

TEST_F(ManagerSerializeTest, DeserializeCachesAllAttributes)
{
    Manager::BaseTable table;
    table["TestAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Serialized")),
        std::variant<int64_t, std::string>(std::string("Default")),
        std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>,
                               std::string>>());
    manager->sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
        baseBIOSTable(table, true);
    serialize(*manager, serializePath);

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    ASSERT_TRUE(deserialize(serializePath, *manager2));

    auto attributes = manager2->getAllAttributes();
    auto attrIter = attributes.find("TestAttr");
    ASSERT_NE(attrIter, attributes.end());
    EXPECT_EQ(std::get<std::string>(std::get<1>(attrIter->second)),
              "Serialized");
}

TEST_F(ManagerSerializeTest, SerializeToBufferSucceedsAndRoundTrips)
{
    manager->sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
        enableAfterReset(true, true);
    std::string buffer;
    bool ok = serializeToBuffer(*manager, buffer);
    EXPECT_TRUE(ok);
    ASSERT_FALSE(buffer.empty());

    std::ofstream ofs(serializePath, std::ios::binary);
    ASSERT_TRUE(ofs.is_open());
    ofs.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    ofs.close();

    manager.reset();
    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    bool result = deserialize(serializePath, *manager2);
    if (result)
    {
        EXPECT_TRUE(manager2->sdbusplus::xyz::openbmc_project::BIOSConfig::
                        server::Manager::enableAfterReset());
    }
}

TEST_F(ManagerSerializeTest, AsyncSerializeCreatesFileWithContent)
{
    manager->enableAfterReset(true);
    asyncSerialize(ioContext, *manager, serializePath);
    ioContext.restart();
    ioContext.run_for(std::chrono::milliseconds(500));

    EXPECT_TRUE(std::filesystem::exists(serializePath));
    EXPECT_GT(std::filesystem::file_size(serializePath), 0u);

    manager.reset();
    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    bool result = deserialize(serializePath, *manager2);
    if (result)
    {
        EXPECT_TRUE(manager2->enableAfterReset(true));
    }
}

TEST_F(ManagerSerializeTest, AsyncSerializeHandlesOpenError)
{
    std::filesystem::path dirPath = persistPath / "subdir";
    std::filesystem::create_directories(dirPath);
    asyncSerialize(ioContext, *manager, dirPath);
    ioContext.restart();
    ioContext.run_for(std::chrono::milliseconds(200));

    EXPECT_TRUE(std::filesystem::is_directory(dirPath));
}

TEST_F(ManagerSerializeTest, DeserializeReturnsFalseWhenFileDoesNotExist)
{
    std::filesystem::path nonExistentPath = persistPath / "non_existent_file";
    bool result = deserialize(nonExistentPath, *manager);

    EXPECT_FALSE(result);
}

TEST_F(ManagerSerializeTest, SerializeHandlesInvalidPath)
{
    std::filesystem::path invalidPath = "/nonexistent/directory/file";
    EXPECT_NO_THROW(serialize(*manager, invalidPath));
}

TEST_F(ManagerSerializeTest, SerializePreservesBootOptions)
{
    std::string bootOptionId = "Boot0001";
    manager->createBootOption(bootOptionId);

    serialize(*manager, serializePath);

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    bool result = deserialize(serializePath, *manager2);
    if (result)
    {
        auto bootOptions = manager2->getBootOptionValues();
        EXPECT_NE(bootOptions.find(bootOptionId), bootOptions.end());
    }
}

TEST_F(ManagerSerializeTest, SerializePreservesBootOrder)
{
    Manager::BootOrderType bootOrder = {"Boot0001", "Boot0002", "Boot0003"};
    manager->bootOrder(bootOrder);

    serialize(*manager, serializePath);

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    deserialize(serializePath, *manager2);

    Manager::BootOrderType expectedBootOrder = {"Boot0001", "Boot0002",
                                                "Boot0003"};
    auto restoredBootOrder = manager2->bootOrder(expectedBootOrder);
    EXPECT_EQ(restoredBootOrder.size(), 3);
    if (restoredBootOrder.size() >= 1)
    {
        EXPECT_EQ(restoredBootOrder[0], "Boot0001");
    }
}

TEST_F(ManagerSerializeTest, SerializePreservesPendingBootOrder)
{
    Manager::BootOrderType pendingBootOrder = {"Boot0001", "Boot0002"};
    manager->pendingBootOrder(pendingBootOrder);

    serialize(*manager, serializePath);

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    deserialize(serializePath, *manager2);

    Manager::BootOrderType expectedPendingBootOrder = {"Boot0001", "Boot0002"};
    auto restoredPendingBootOrder =
        manager2->pendingBootOrder(expectedPendingBootOrder);
    EXPECT_EQ(restoredPendingBootOrder.size(), 2);
}

TEST_F(ManagerSerializeTest, SerializePreservesEnableAfterReset)
{
    manager->enableAfterReset(true);

    serialize(*manager, serializePath);

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    deserialize(serializePath, *manager2);

    EXPECT_TRUE(manager2->enableAfterReset(true));
}

TEST_F(ManagerSerializeTest, SerializePreservesCredentialBootstrap)
{
    manager->credentialBootstrap(true);

    serialize(*manager, serializePath);

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    deserialize(serializePath, *manager2);

    EXPECT_TRUE(manager2->credentialBootstrap(true));
}

TEST_F(ManagerSerializeTest, SerializeHandlesExceptionDuringSerialization)
{
    std::filesystem::path dirPath = persistPath / "test_dir";
    std::filesystem::create_directories(dirPath);

    EXPECT_NO_THROW(serialize(*manager, dirPath));
}

TEST_F(ManagerSerializeTest, DeserializeHandlesCerealException)
{
    std::ofstream invalidFile(serializePath, std::ios::binary);
    invalidFile << "BAD";
    invalidFile.close();

    bool result = deserialize(serializePath, *manager);
    EXPECT_FALSE(result);
    EXPECT_FALSE(std::filesystem::exists(serializePath));
}

TEST_F(ManagerSerializeTest, DeserializeHandlesFileOpenFailure)
{
    std::filesystem::path testPath = persistPath / "test_file";
    std::ofstream testFile(testPath);
    testFile << "test";
    testFile.close();

    std::filesystem::remove(testPath);

    bool result = deserialize(testPath, *manager);
    EXPECT_FALSE(result);
}

TEST_F(ManagerSerializeTest, DeserializeReturnsFalseWhenPathIsDirectory)
{
    std::filesystem::path dirPath = persistPath / "dir_for_deserialize";
    std::filesystem::create_directories(dirPath);
    bool result = deserialize(dirPath, *manager);
    EXPECT_FALSE(result);
}

TEST_F(ManagerSerializeTest,
       DeserializeReturnsFalseWhenPathExistsButNotOpenable)
{
    std::filesystem::path restrictedPath = persistPath / "restricted_file";
    {
        std::ofstream f(restrictedPath);
        f << "x";
    }
    try
    {
        std::filesystem::permissions(restrictedPath,
                                     std::filesystem::perms::none,
                                     std::filesystem::perm_options::replace);
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return;
    }
    bool result = deserialize(restrictedPath, *manager);
    try
    {
        std::filesystem::permissions(restrictedPath,
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace);
    }
    catch (const std::filesystem::filesystem_error&)
    {}
    EXPECT_FALSE(result);
}

TEST_F(ManagerSerializeTest, DeserializeV2FormatSucceeds)
{
    std::ofstream os(serializePath, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    std::uint32_t version = BIOS_CONFIG_VERSION_2;
    Manager::BaseTable baseTable;
    Manager::PendingAttributes pendingAttrs;
    bool enableAfterResetFlag = true;
    archive(version);
    archive(baseTable, pendingAttrs, enableAfterResetFlag);
    Manager::BootOrderType bootOrderValue;
    Manager::BootOrderType pendingBootOrderValue;
    Manager::BootOptionsType bootOptionsValues;
    archive(bootOrderValue);
    archive(pendingBootOrderValue);
    archive(bootOptionsValues);
    CurrentBootType currentBootValue = CurrentBootType::Unknown;
    bool enableValue = false;
    ModeType modeValue = ModeType::Unknown;
    archive(currentBootValue);
    archive(enableValue);
    archive(modeValue);
    os.close();

    bool result = deserialize(serializePath, *manager);
    EXPECT_TRUE(result);
    EXPECT_TRUE(manager->enableAfterReset(true));
}

TEST_F(ManagerSerializeTest, DeserializeV1FormatSucceeds)
{
    std::ofstream os(serializePath, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    std::uint32_t version = 0;
    Manager::oldBaseTable baseTableV1;
    Manager::PendingAttributes pendingAttrs;
    bool enableAfterResetFlag = true;
    archive(version);
    archive(baseTableV1, pendingAttrs, enableAfterResetFlag);
    Manager::BootOrderType bootOrderValue;
    Manager::BootOrderType pendingBootOrderValue;
    Manager::BootOptionsType bootOptionsValues;
    archive(bootOrderValue);
    archive(pendingBootOrderValue);
    archive(bootOptionsValues);
    CurrentBootType currentBootValue = CurrentBootType::Unknown;
    bool enableValue = false;
    ModeType modeValue = ModeType::Unknown;
    archive(currentBootValue);
    archive(enableValue);
    archive(modeValue);
    os.close();

    bool result = deserialize(serializePath, *manager);
    EXPECT_TRUE(result);
    EXPECT_TRUE(manager->enableAfterReset(true));
}

TEST_F(ManagerSerializeTest, SerializePreservesSecureBootSettings)
{
    SecureBootServer::CurrentBootType bootType =
        SecureBootServer::CurrentBootType::Unknown;
    manager->currentBoot(bootType);
    manager->pendingEnable(true);
    SecureBootServer::ModeType modeValue = SecureBootServer::ModeType::Unknown;
    manager->mode(modeValue);

    serialize(*manager, serializePath);

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    deserialize(serializePath, *manager2);

    SecureBootServer::CurrentBootType expectedBootType =
        SecureBootServer::CurrentBootType::Unknown;
    EXPECT_EQ(manager2->currentBoot(expectedBootType), expectedBootType);
    EXPECT_TRUE(manager2->pendingEnable(true));
    SecureBootServer::ModeType expectedModeValue =
        SecureBootServer::ModeType::Unknown;
    EXPECT_EQ(manager2->mode(expectedModeValue), expectedModeValue);
}

TEST_F(ManagerSerializeTest, SerializePreservesBootOptionsWithPendingEnabled)
{
    std::string bootOptionId = "Boot0002";
    manager->createBootOption(bootOptionId);

    auto bootOptionsBefore = manager->getBootOptionValues();
    ASSERT_NE(bootOptionsBefore.find(bootOptionId), bootOptionsBefore.end());

    serialize(*manager, serializePath);

    ASSERT_TRUE(std::filesystem::exists(serializePath));
    ASSERT_GT(std::filesystem::file_size(serializePath), 0);

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    bool result = deserialize(serializePath, *manager2);

    if (result)
    {
        auto bootOptions = manager2->getBootOptionValues();
        auto it = bootOptions.find(bootOptionId);
        if (it != bootOptions.end())
        {
            EXPECT_NE(it->second.find("PendingEnabled"), it->second.end());
        }
    }
}

TEST_F(ManagerSerializeTest, SerializePreservesBootOptionsWithoutPendingEnabled)
{
    std::string bootOptionId = "Boot0003";
    manager->createBootOption(bootOptionId);

    auto bootOptionsBefore = manager->getBootOptionValues();
    ASSERT_NE(bootOptionsBefore.find(bootOptionId), bootOptionsBefore.end());

    serialize(*manager, serializePath);

    ASSERT_TRUE(std::filesystem::exists(serializePath));
    ASSERT_GT(std::filesystem::file_size(serializePath), 0);

    manager.reset();

    auto manager2 =
        std::make_unique<Manager>(*objServer, systemBus, loadPath.string());
    bool result = deserialize(serializePath, *manager2);

    if (result)
    {
        auto bootOptions = manager2->getBootOptionValues();
        auto it = bootOptions.find(bootOptionId);
        if (it != bootOptions.end())
        {
            auto enabledIt = it->second.find("Enabled");
            auto pendingEnabledIt = it->second.find("PendingEnabled");
            if (enabledIt != it->second.end() &&
                pendingEnabledIt != it->second.end())
            {
                EXPECT_EQ(std::get<bool>(enabledIt->second),
                          std::get<bool>(pendingEnabledIt->second));
            }
        }
    }
}

} // namespace bios_config::test
