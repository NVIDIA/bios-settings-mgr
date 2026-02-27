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

#include "boot_option.hpp"
#include "common.hpp"
#include "manager.hpp"

#include <filesystem>
#include <string>

namespace bios_config::test
{

using namespace bios_config;

class TestableManager : public Manager
{
  public:
    using Manager::Manager;

    using Manager::dbusBootOptions;
};

class BootOptionTest : public BiosConfigTest
{
  protected:
    void SetUp() override
    {
        BiosConfigTest::SetUp();
        if (objServer && systemBus)
        {
            manager = std::make_unique<Manager>(*objServer, systemBus,
                                                persistPath.string());

            bootOptionId = "Boot0001";
            manager->createBootOption(bootOptionId);

            bootOptionPath = std::string("/xyz/openbmc_project/bios_config/"
                                         "bootOptions/") +
                             bootOptionId;
        }
    }

    std::unique_ptr<Manager> manager;
    std::string bootOptionId;
    std::string bootOptionPath;
};

TEST_F(BootOptionTest, CreateBootOptionSucceeds)
{
    std::string newBootOptionId = "Boot0002";
    EXPECT_NO_THROW(manager->createBootOption(newBootOptionId));

    auto bootOptions = manager->getBootOptionValues();
    EXPECT_NE(bootOptions.find(newBootOptionId), bootOptions.end());
}

TEST_F(BootOptionTest, CreateBootOptionDuplicateIdThrows)
{
    EXPECT_THROW(manager->createBootOption(bootOptionId), std::exception);
}

TEST_F(BootOptionTest, DeleteBootOptionNonExistentKeyNoThrow)
{
    EXPECT_NO_THROW(manager->deleteBootOption("NonExistentBoot999"));
}

TEST_F(BootOptionTest, BootOptionValuesCanBeRetrieved)
{
    auto bootOptions = manager->getBootOptionValues();
    EXPECT_NE(bootOptions.find(bootOptionId), bootOptions.end());
}

TEST_F(BootOptionTest, BootOptionValuesCanBeSet)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["Enabled"] = true;
    bootOptionData["PendingEnabled"] = false;
    bootOptionData["Description"] = "Test Boot Option";
    bootOptionData["DisplayName"] = "Test";
    bootOptionData["UefiDevicePath"] = "HD(1,GPT,test-uuid,0x800,0x100000)";

    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    EXPECT_NE(retrieved.find(bootOptionId), retrieved.end());
}

TEST_F(BootOptionTest, DeleteBootOptionRemovesFromManager)
{
    std::string bootOptionId2 = "Boot0002";
    manager->createBootOption(bootOptionId2);

    auto bootOptions = manager->getBootOptionValues();
    EXPECT_NE(bootOptions.find(bootOptionId), bootOptions.end());
    EXPECT_NE(bootOptions.find(bootOptionId2), bootOptions.end());

    manager->deleteBootOption(bootOptionId2);

    bootOptions = manager->getBootOptionValues();
    EXPECT_NE(bootOptions.find(bootOptionId), bootOptions.end());
    EXPECT_EQ(bootOptions.find(bootOptionId2), bootOptions.end());
}

TEST_F(BootOptionTest, MultipleBootOptionsCanBeCreated)
{
    std::vector<std::string> bootOptionIds = {"Boot0002", "Boot0003",
                                              "Boot0004"};

    for (const auto& id : bootOptionIds)
    {
        EXPECT_NO_THROW(manager->createBootOption(id));
    }

    auto bootOptions = manager->getBootOptionValues();
    for (const auto& id : bootOptionIds)
    {
        EXPECT_NE(bootOptions.find(id), bootOptions.end());
    }
}

TEST_F(BootOptionTest, BootOrderIntegration)
{
    std::vector<std::string> bootOptionIds = {"Boot0002", "Boot0003",
                                              "Boot0004"};

    for (const auto& id : bootOptionIds)
    {
        manager->createBootOption(id);
    }

    Manager::BootOrderType bootOrder = {"Boot0002", "Boot0003", "Boot0004",
                                        bootOptionId};
    auto result = manager->bootOrder(bootOrder);
    EXPECT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], "Boot0002");
}

TEST_F(BootOptionTest, PendingBootOrderIntegration)
{
    std::vector<std::string> bootOptionIds = {"Boot0002", "Boot0003"};

    for (const auto& id : bootOptionIds)
    {
        manager->createBootOption(id);
    }

    Manager::BootOrderType pendingBootOrder = {"Boot0002", "Boot0003"};
    auto result = manager->pendingBootOrder(pendingBootOrder);
    EXPECT_EQ(result.size(), 2);
}

TEST_F(BootOptionTest, BootOptionDescriptionCanBeSet)
{
    auto bootOptions = manager->getBootOptionValues();
    ASSERT_NE(bootOptions.find(bootOptionId), bootOptions.end());

    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["Description"] = "Test Description";
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    auto descIt = it->second.find("Description");
    ASSERT_NE(descIt, it->second.end());
    EXPECT_EQ(std::get<std::string>(descIt->second), "Test Description");
}

TEST_F(BootOptionTest, BootOptionDisplayNameCanBeSet)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["DisplayName"] = "Test Display Name";
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    auto nameIt = it->second.find("DisplayName");
    ASSERT_NE(nameIt, it->second.end());
    EXPECT_EQ(std::get<std::string>(nameIt->second), "Test Display Name");
}

TEST_F(BootOptionTest, BootOptionUefiDevicePathCanBeSet)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["UefiDevicePath"] = "HD(1,GPT,test-uuid,0x800,0x100000)";
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    auto pathIt = it->second.find("UefiDevicePath");
    ASSERT_NE(pathIt, it->second.end());
    EXPECT_EQ(std::get<std::string>(pathIt->second),
              "HD(1,GPT,test-uuid,0x800,0x100000)");
}

TEST_F(BootOptionTest, BootOptionPendingEnabledCanBeSet)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["PendingEnabled"] = false;
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    auto enabledIt = it->second.find("PendingEnabled");
    ASSERT_NE(enabledIt, it->second.end());
    EXPECT_EQ(std::get<bool>(enabledIt->second), false);
}

TEST_F(BootOptionTest, BootOptionEnabledCanBeSetToTrue)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["Enabled"] = true;
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    auto enabledIt = it->second.find("Enabled");
    ASSERT_NE(enabledIt, it->second.end());
    EXPECT_EQ(std::get<bool>(enabledIt->second), true);
}

TEST_F(BootOptionTest, BootOptionEnabledCanBeSetToFalse)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["Enabled"] = false;
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    auto enabledIt = it->second.find("Enabled");
    ASSERT_NE(it, retrieved.end());
    EXPECT_EQ(std::get<bool>(enabledIt->second), false);
}

TEST_F(BootOptionTest, BootOptionPendingEnabledCanBeSetToTrue)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["PendingEnabled"] = true;
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    auto enabledIt = it->second.find("PendingEnabled");
    ASSERT_NE(enabledIt, it->second.end());
    EXPECT_EQ(std::get<bool>(enabledIt->second), true);
}

TEST_F(BootOptionTest, BootOptionAllPropertiesCanBeSetTogether)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["Enabled"] = true;
    bootOptionData["PendingEnabled"] = true;
    bootOptionData["Description"] = "Full Test Description";
    bootOptionData["DisplayName"] = "Full Test Display Name";
    bootOptionData["UefiDevicePath"] =
        "HD(1,GPT,full-test-uuid,0x800,0x200000)";
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());

    EXPECT_EQ(std::get<bool>(it->second.find("Enabled")->second), true);
    EXPECT_EQ(std::get<bool>(it->second.find("PendingEnabled")->second), true);
    EXPECT_EQ(std::get<std::string>(it->second.find("Description")->second),
              "Full Test Description");
    EXPECT_EQ(std::get<std::string>(it->second.find("DisplayName")->second),
              "Full Test Display Name");
    EXPECT_EQ(std::get<std::string>(it->second.find("UefiDevicePath")->second),
              "HD(1,GPT,full-test-uuid,0x800,0x200000)");
}

TEST_F(BootOptionTest, BootOptionSetBootOptionValuesWithMultipleBootOptions)
{
    std::vector<std::string> bootOptionIds = {"Boot0002", "Boot0003",
                                              "Boot0004"};
    for (const auto& id : bootOptionIds)
    {
        manager->createBootOption(id);
    }

    Manager::BootOptionsType bootOptionValues;
    for (const auto& id : bootOptionIds)
    {
        Manager::BootOptionDataType bootOptionData;
        bootOptionData["Enabled"] = true;
        bootOptionData["Description"] = "Description for " + id;
        bootOptionValues[id] = bootOptionData;
    }

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    for (const auto& id : bootOptionIds)
    {
        auto it = retrieved.find(id);
        ASSERT_NE(it, retrieved.end());
        EXPECT_EQ(std::get<bool>(it->second.find("Enabled")->second), true);
        EXPECT_EQ(std::get<std::string>(it->second.find("Description")->second),
                  "Description for " + id);
    }
}

TEST_F(BootOptionTest, BootOptionSetBootOptionValuesClearsExistingOptions)
{
    std::string bootOptionId2 = "Boot0002";
    manager->createBootOption(bootOptionId2);

    auto bootOptions = manager->getBootOptionValues();
    EXPECT_NE(bootOptions.find(bootOptionId), bootOptions.end());
    EXPECT_NE(bootOptions.find(bootOptionId2), bootOptions.end());

    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["Enabled"] = true;
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    bootOptions = manager->getBootOptionValues();
    EXPECT_NE(bootOptions.find(bootOptionId), bootOptions.end());
    EXPECT_EQ(bootOptions.find(bootOptionId2), bootOptions.end());
}

TEST_F(BootOptionTest,
       BootOptionSetBootOptionValuesWithOldVersionMissingPendingEnabled)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["Enabled"] = true;
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    auto enabledIt = it->second.find("Enabled");
    auto pendingEnabledIt = it->second.find("PendingEnabled");
    ASSERT_NE(enabledIt, it->second.end());
    ASSERT_NE(pendingEnabledIt, it->second.end());
    EXPECT_EQ(std::get<bool>(enabledIt->second),
              std::get<bool>(pendingEnabledIt->second));
}

TEST_F(BootOptionTest, BootOptionSetBootOptionValuesWithEmptyPropertyMap)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionValues[bootOptionId] = bootOptionData;

    EXPECT_NO_THROW(manager->setBootOptionValues(bootOptionValues));

    auto retrieved = manager->getBootOptionValues();
    EXPECT_NE(retrieved.find(bootOptionId), retrieved.end());
    EXPECT_TRUE(retrieved.at(bootOptionId).empty());
}

TEST_F(BootOptionTest, BootOptionSetBootOptionValuesWithOnlyDescription)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["Description"] = "DescriptionOnly";
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    EXPECT_EQ(std::get<std::string>(it->second.find("Description")->second),
              "DescriptionOnly");
}

TEST_F(BootOptionTest, BootOptionSetBootOptionValuesWithOnlyDisplayName)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["DisplayName"] = "DisplayOnly";
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    EXPECT_EQ(std::get<std::string>(it->second.find("DisplayName")->second),
              "DisplayOnly");
}

TEST_F(BootOptionTest, BootOptionSetBootOptionValuesWithOnlyUefiDevicePath)
{
    Manager::BootOptionsType bootOptionValues;
    Manager::BootOptionDataType bootOptionData;
    bootOptionData["UefiDevicePath"] =
        "HD(1,GPT,path-only-uuid,0x800,0x100000)";
    bootOptionValues[bootOptionId] = bootOptionData;

    manager->setBootOptionValues(bootOptionValues);

    auto retrieved = manager->getBootOptionValues();
    auto it = retrieved.find(bootOptionId);
    ASSERT_NE(it, retrieved.end());
    EXPECT_EQ(std::get<std::string>(it->second.find("UefiDevicePath")->second),
              "HD(1,GPT,path-only-uuid,0x800,0x100000)");
}

TEST_F(BootOptionTest, BootOptionEnabledMethodCanBeCalled)
{
    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    std::string testBootOptionId = "BootTestEnabled";
    testableMgr->createBootOption(testBootOptionId);

    auto it = testableMgr->dbusBootOptions.find(testBootOptionId);
    ASSERT_NE(it, testableMgr->dbusBootOptions.end());

    bool result = it->second->enabled(false);
    (void)result;

    auto retrieved = testableMgr->getBootOptionValues();
    auto bootIt = retrieved.find(testBootOptionId);
    ASSERT_NE(bootIt, retrieved.end());
    auto enabledIt = bootIt->second.find("Enabled");
    ASSERT_NE(enabledIt, bootIt->second.end());
    EXPECT_EQ(std::get<bool>(enabledIt->second), false);
}

TEST_F(BootOptionTest, BootOptionEnabledMethodSetsPendingEnabled)
{
    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    std::string testBootOptionId = "BootTestPending";
    testableMgr->createBootOption(testBootOptionId);

    auto it = testableMgr->dbusBootOptions.find(testBootOptionId);
    ASSERT_NE(it, testableMgr->dbusBootOptions.end());

    bool result = it->second->enabled(true);
    (void)result;

    auto retrieved = testableMgr->getBootOptionValues();
    auto bootIt = retrieved.find(testBootOptionId);
    ASSERT_NE(bootIt, retrieved.end());
    auto enabledIt = bootIt->second.find("Enabled");
    auto pendingIt = bootIt->second.find("PendingEnabled");
    ASSERT_NE(enabledIt, bootIt->second.end());
    ASSERT_NE(pendingIt, bootIt->second.end());
    EXPECT_EQ(std::get<bool>(enabledIt->second), true);
    EXPECT_EQ(std::get<bool>(pendingIt->second), true);
}

TEST_F(BootOptionTest, BootOptionDescriptionMethodCanBeCalled)
{
    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    std::string testBootOptionId = "BootTestDesc";
    testableMgr->createBootOption(testBootOptionId);

    auto it = testableMgr->dbusBootOptions.find(testBootOptionId);
    ASSERT_NE(it, testableMgr->dbusBootOptions.end());

    std::string testDesc = "Test Description for Boot Option";
    std::string result = it->second->description(testDesc);
    (void)result;

    auto retrieved = testableMgr->getBootOptionValues();
    auto bootIt = retrieved.find(testBootOptionId);
    ASSERT_NE(bootIt, retrieved.end());
    auto descIt = bootIt->second.find("Description");
    ASSERT_NE(descIt, bootIt->second.end());
    EXPECT_EQ(std::get<std::string>(descIt->second), testDesc);
}

TEST_F(BootOptionTest, BootOptionDisplayNameMethodCanBeCalled)
{
    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    std::string testBootOptionId = "BootTestDisplay";
    testableMgr->createBootOption(testBootOptionId);

    auto it = testableMgr->dbusBootOptions.find(testBootOptionId);
    ASSERT_NE(it, testableMgr->dbusBootOptions.end());

    std::string testName = "Test Display Name";
    std::string result = it->second->displayName(testName);
    (void)result;

    auto retrieved = testableMgr->getBootOptionValues();
    auto bootIt = retrieved.find(testBootOptionId);
    ASSERT_NE(bootIt, retrieved.end());
    auto nameIt = bootIt->second.find("DisplayName");
    ASSERT_NE(nameIt, bootIt->second.end());
    EXPECT_EQ(std::get<std::string>(nameIt->second), testName);
}

TEST_F(BootOptionTest, BootOptionUefiDevicePathMethodCanBeCalled)
{
    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    std::string testBootOptionId = "BootTestUefi";
    testableMgr->createBootOption(testBootOptionId);

    auto it = testableMgr->dbusBootOptions.find(testBootOptionId);
    ASSERT_NE(it, testableMgr->dbusBootOptions.end());

    std::string testPath = "HD(1,GPT,test-uuid-123,0x800,0x100000)";
    std::string result = it->second->uefiDevicePath(testPath);
    (void)result;

    auto retrieved = testableMgr->getBootOptionValues();
    auto bootIt = retrieved.find(testBootOptionId);
    ASSERT_NE(bootIt, retrieved.end());
    auto pathIt = bootIt->second.find("UefiDevicePath");
    ASSERT_NE(pathIt, bootIt->second.end());
    EXPECT_EQ(std::get<std::string>(pathIt->second), testPath);
}

TEST_F(BootOptionTest, BootOptionDeleteMethodCanBeCalled)
{
    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    std::string testBootOptionId = "BootTestDelete";
    testableMgr->createBootOption(testBootOptionId);

    auto it = testableMgr->dbusBootOptions.find(testBootOptionId);
    ASSERT_NE(it, testableMgr->dbusBootOptions.end());

    it->second->delete_();

    auto retrieved = testableMgr->getBootOptionValues();
    auto bootIt = retrieved.find(testBootOptionId);
    EXPECT_EQ(bootIt, retrieved.end());

    auto dbusIt = testableMgr->dbusBootOptions.find(testBootOptionId);
    EXPECT_EQ(dbusIt, testableMgr->dbusBootOptions.end());
}

TEST_F(BootOptionTest, BootOptionAllMethodsCanBeCalledTogether)
{
    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    std::string newBootOptionId = "BootTestAll";
    testableMgr->createBootOption(newBootOptionId);

    auto it = testableMgr->dbusBootOptions.find(newBootOptionId);
    ASSERT_NE(it, testableMgr->dbusBootOptions.end());

    it->second->enabled(true);
    it->second->description("Combined Test Description");
    it->second->displayName("Combined Display Name");
    it->second->uefiDevicePath("HD(1,GPT,combined-uuid,0x800,0x100000)");

    auto retrieved = testableMgr->getBootOptionValues();
    auto bootIt = retrieved.find(newBootOptionId);
    ASSERT_NE(bootIt, retrieved.end());

    EXPECT_EQ(std::get<bool>(bootIt->second.find("Enabled")->second), true);
    EXPECT_EQ(std::get<std::string>(bootIt->second.find("Description")->second),
              "Combined Test Description");
    EXPECT_EQ(std::get<std::string>(bootIt->second.find("DisplayName")->second),
              "Combined Display Name");
    EXPECT_EQ(
        std::get<std::string>(bootIt->second.find("UefiDevicePath")->second),
        "HD(1,GPT,combined-uuid,0x800,0x100000)");
}

TEST_F(BootOptionTest, BootOptionPendingEnabledMethodCanBeCalledDirectly)
{
    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    std::string testBootOptionId = "BootTestPendingEnabled";
    testableMgr->createBootOption(testBootOptionId);

    auto it = testableMgr->dbusBootOptions.find(testBootOptionId);
    ASSERT_NE(it, testableMgr->dbusBootOptions.end());

    bool result = it->second->pendingEnabled(true);
    (void)result;

    auto retrieved = testableMgr->getBootOptionValues();
    auto bootIt = retrieved.find(testBootOptionId);
    ASSERT_NE(bootIt, retrieved.end());
    auto pendingIt = bootIt->second.find("PendingEnabled");
    ASSERT_NE(pendingIt, bootIt->second.end());
    EXPECT_EQ(std::get<bool>(pendingIt->second), true);
}

} // namespace bios_config::test
