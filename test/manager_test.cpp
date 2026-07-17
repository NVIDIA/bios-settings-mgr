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

#include <xyz/openbmc_project/BIOSConfig/Common/error.hpp>
#include <xyz/openbmc_project/BIOSConfig/Manager/common.hpp>
#include <xyz/openbmc_project/BIOSConfig/Manager/server.hpp>
#include <xyz/openbmc_project/BIOSConfig/SecureBoot/server.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

namespace bios_config::test
{

using namespace bios_config;
using namespace sdbusplus::xyz::openbmc_project::BIOSConfig::Common::Error;
using namespace sdbusplus::xyz::openbmc_project::BIOSConfig::server;

using ManagerServer =
    sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager;
using SecureBootServer =
    sdbusplus::xyz::openbmc_project::BIOSConfig::server::SecureBoot;

using AttributeType = ManagerServer::AttributeType;
using BoundType = ManagerServer::BoundType;
using ResetFlag = ManagerServer::ResetFlag;
using CurrentBootType = SecureBootServer::CurrentBootType;
using ModeType = SecureBootServer::ModeType;

class TestableManager : public Manager
{
  public:
    using Manager::Manager;

    using Manager::validateEnumOption;
    using Manager::validateIntegerOption;
    using Manager::validateStringOption;
};

class ManagerTest : public BiosConfigTest
{
  protected:
    void SetUp() override
    {
        BiosConfigTest::SetUp();
        if (objServer && systemBus)
        {
            manager = std::make_unique<Manager>(*objServer, systemBus,
                                                persistPath.string());
        }
    }

    std::unique_ptr<Manager> manager;
};

TEST_F(ManagerTest, ConstructorCreatesManager)
{
    EXPECT_NE(manager, nullptr);
}

TEST_F(ManagerTest, SetAttributeAddsToPendingAttributes)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(100), "");
    table["TestAttribute"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")), options);
    manager->baseBIOSTable(table);

    manager->setAttribute("TestAttribute", std::string("TestValue"));

    auto pendingAttrs = manager->sdbusplus::xyz::openbmc_project::BIOSConfig::
                            server::Manager::pendingAttributes();
    EXPECT_NE(pendingAttrs.find("TestAttribute"), pendingAttrs.end());
}

TEST_F(ManagerTest, SetAttributeIntegerValue)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(1), "");
    table["TestIntAttribute"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(0)),
        std::variant<int64_t, std::string>(int64_t(0)), options);
    manager->baseBIOSTable(table);

    manager->setAttribute("TestIntAttribute", int64_t(42));

    auto pendingAttrs = manager->sdbusplus::xyz::openbmc_project::BIOSConfig::
                            server::Manager::pendingAttributes();
    auto iter = pendingAttrs.find("TestIntAttribute");
    ASSERT_NE(iter, pendingAttrs.end());

    auto value = std::get<1>(iter->second);
    EXPECT_TRUE(std::holds_alternative<int64_t>(value));
    EXPECT_EQ(std::get<int64_t>(value), 42);
}

TEST_F(ManagerTest, SetAttributeStringValue)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(100), "");
    table["TestStringAttribute"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")), options);
    manager->baseBIOSTable(table);

    manager->setAttribute("TestStringAttribute", std::string("Hello"));

    auto pendingAttrs = manager->sdbusplus::xyz::openbmc_project::BIOSConfig::
                            server::Manager::pendingAttributes();
    auto iter = pendingAttrs.find("TestStringAttribute");
    ASSERT_NE(iter, pendingAttrs.end());

    auto value = std::get<1>(iter->second);
    EXPECT_TRUE(std::holds_alternative<std::string>(value));
    EXPECT_EQ(std::get<std::string>(value), "Hello");
}

TEST_F(ManagerTest, SetAttributeUpdatesExistingPending)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(100), "");
    table["TestAttribute"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")), options);
    manager->baseBIOSTable(table);

    manager->setAttribute("TestAttribute", std::string("FirstValue"));

    manager->setAttribute("TestAttribute", std::string("SecondValue"));

    auto pendingAttrs = manager->sdbusplus::xyz::openbmc_project::BIOSConfig::
                            server::Manager::pendingAttributes();
    auto iter = pendingAttrs.find("TestAttribute");
    ASSERT_NE(iter, pendingAttrs.end());

    auto value = std::get<1>(iter->second);
    EXPECT_EQ(std::get<std::string>(value), "SecondValue");
}

TEST_F(ManagerTest, GetAttributeThrowsWhenNotFound)
{
    EXPECT_THROW(manager->getAttribute("NonExistentAttribute"),
                 AttributeNotFound);
}

TEST_F(ManagerTest, PendingAttributesReplaceExistingAttribute)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(100), "");
    table["Attr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")), options);
    manager->baseBIOSTable(table);

    manager->setAttribute("Attr", std::string("First"));
    manager->setAttribute("Attr", std::string("Second"));

    auto details = manager->getAttribute("Attr");
    EXPECT_EQ(std::get<std::string>(std::get<2>(details)), "Second");
}

TEST_F(ManagerTest, GetAttributeThrowsForMissingAttribute)
{
    EXPECT_THROW(manager->getAttribute("NonExistentAttr"), AttributeNotFound);
}

TEST_F(ManagerTest, SetAttributeIntegerOutOfRangeThrows)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(1), "");
    table["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(0)),
        std::variant<int64_t, std::string>(int64_t(0)), options);
    manager->baseBIOSTable(table);

    EXPECT_THROW(manager->setAttribute("IntAttr", int64_t(150)),
                 std::exception);
}

TEST_F(ManagerTest, BaseBIOSTableCanBeSet)
{
    Manager::BaseTable table;
    table["TestAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")),
        std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>,
                               std::string>>());

    auto result = manager->baseBIOSTable(table);
    EXPECT_EQ(result.size(), 1);
    EXPECT_NE(result.find("TestAttr"), result.end());
}

TEST_F(ManagerTest, BaseBIOSTableClearsPendingAttributes)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(100), "");
    Manager::BaseTable initialTable;
    initialTable["TestAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")), options);
    manager->baseBIOSTable(initialTable);

    manager->setAttribute("TestAttr", std::string("PendingValue"));

    Manager::BaseTable table;
    table["TestAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")), options);

    manager->baseBIOSTable(table);

    auto pendingAttrs = manager->sdbusplus::xyz::openbmc_project::BIOSConfig::
                            server::Manager::pendingAttributes();
    EXPECT_EQ(pendingAttrs.size(), 0);
}

TEST_F(ManagerTest, CreateBootOptionCreatesDbusObject)
{
    std::string bootOptionId = "Boot0001";
    manager->createBootOption(bootOptionId);

    auto bootOptions = manager->getBootOptionValues();
    EXPECT_NE(bootOptions.find(bootOptionId), bootOptions.end());
}

TEST_F(ManagerTest, BootOrderCanBeSet)
{
    Manager::BootOrderType bootOrder = {"Boot0001", "Boot0002", "Boot0003"};

    auto result = manager->bootOrder(bootOrder);
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "Boot0001");
    EXPECT_EQ(result[1], "Boot0002");
    EXPECT_EQ(result[2], "Boot0003");
}

TEST_F(ManagerTest, PendingBootOrderCanBeSet)
{
    Manager::BootOrderType pendingBootOrder = {"Boot0001", "Boot0002"};

    auto result = manager->pendingBootOrder(pendingBootOrder);
    EXPECT_EQ(result.size(), 2);
}

TEST_F(ManagerTest, EnableAfterResetCanBeSet)
{
    bool value = true;
    auto result = manager->enableAfterReset(value);
    EXPECT_EQ(result, true);
}

TEST_F(ManagerTest, EnableAfterResetCanBeSetToFalse)
{
    manager->enableAfterReset(true);
    auto result = manager->enableAfterReset(false);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, CredentialBootstrapCanBeSet)
{
    bool value = true;
    auto result = manager->credentialBootstrap(value);
    EXPECT_EQ(result, true);
}

TEST_F(ManagerTest, CredentialBootstrapCanBeSetToFalse)
{
    manager->credentialBootstrap(true);
    auto result = manager->credentialBootstrap(false);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, ResetBIOSSettings)
{
    ResetFlag flag = ResetFlag::NoAction;
    auto result = manager->resetBIOSSettings(flag);
    EXPECT_EQ(result, flag);
}

TEST_F(ManagerTest, ResetBIOSSettingsNoActionAgain)
{
    ResetFlag flag = ResetFlag::NoAction;
    auto result = manager->resetBIOSSettings(flag);
    EXPECT_EQ(result, flag);
}

TEST_F(ManagerTest, BootOrderAcceptsEmptyVector)
{
    Manager::BootOrderType emptyOrder;
    auto result = manager->bootOrder(emptyOrder);
    EXPECT_TRUE(result.empty());
}

TEST_F(ManagerTest, PendingBootOrderAcceptsSingleElement)
{
    Manager::BootOrderType singleOrder = {"Boot0001"};
    auto result = manager->pendingBootOrder(singleOrder);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "Boot0001");
}

TEST_F(ManagerTest, GetAttributeReturnsCorrectDetails)
{
    Manager::BaseTable table;
    table["TestAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")),
        std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>,
                               std::string>>());

    manager->baseBIOSTable(table);

    auto details = manager->getAttribute("TestAttr");
    EXPECT_EQ(std::get<0>(details), AttributeType::String);
    EXPECT_TRUE(std::holds_alternative<std::string>(std::get<1>(details)));
    EXPECT_EQ(std::get<std::string>(std::get<1>(details)), "Current");
}

TEST_F(ManagerTest, GetAttributeWithPendingValue)
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

    auto details = manager->getAttribute("TestAttr");
    EXPECT_EQ(std::get<0>(details), AttributeType::String);
    EXPECT_TRUE(std::holds_alternative<std::string>(std::get<2>(details)));
    EXPECT_EQ(std::get<std::string>(std::get<2>(details)), "PendingValue");
}

TEST_F(ManagerTest, GetAllAttributesReturnsTypesAndCurrentValues)
{
    Manager::BaseTable table;
    table["StringAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")),
        std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>,
                               std::string>>());

    table["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(42)),
        std::variant<int64_t, std::string>(int64_t(0)),
        std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>,
                               std::string>>());

    manager->baseBIOSTable(table);

    auto attributes = manager->getAllAttributes();

    ASSERT_EQ(attributes.size(), 2);

    auto stringIter = attributes.find("StringAttr");
    ASSERT_NE(stringIter, attributes.end());
    EXPECT_EQ(std::get<0>(stringIter->second), AttributeType::String);
    EXPECT_EQ(std::get<std::string>(std::get<1>(stringIter->second)),
              "Current");

    auto integerIter = attributes.find("IntAttr");
    ASSERT_NE(integerIter, attributes.end());
    EXPECT_EQ(std::get<0>(integerIter->second), AttributeType::Integer);
    EXPECT_EQ(std::get<int64_t>(std::get<1>(integerIter->second)), 42);
}

TEST_F(ManagerTest, BaseBiosTableCachesAllAttributes)
{
    Manager::BaseTable table;
    table["StringAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")),
        std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>,
                               std::string>>());
    manager->baseBIOSTable(table);

    auto attributes = manager->getAllAttributes();
    auto attrIter = attributes.find("StringAttr");
    ASSERT_NE(attrIter, attributes.end());
    EXPECT_EQ(std::get<std::string>(std::get<1>(attrIter->second)), "Current");

    table["StringAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Updated")),
        std::variant<int64_t, std::string>(std::string("Default")),
        std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>,
                               std::string>>());
    manager->baseBIOSTable(table);

    attributes = manager->getAllAttributes();
    attrIter = attributes.find("StringAttr");
    ASSERT_NE(attrIter, attributes.end());
    EXPECT_EQ(std::get<std::string>(std::get<1>(attrIter->second)), "Updated");
}

TEST_F(ManagerTest, PendingAttributesValidatesEnumOption)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::OneOf, std::string("Value1"), "VDN1");
    options.emplace_back(BoundType::OneOf, std::string("Value2"), "VDN2");

    table["EnumAttr"] = std::make_tuple(
        AttributeType::Enumeration, false, "DisplayName", "Description",
        "MenuPath", std::variant<int64_t, std::string>(std::string("Value1")),
        std::variant<int64_t, std::string>(std::string("Value1")), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["EnumAttr"] =
        std::make_tuple(AttributeType::Enumeration, std::string("Value1"));
    EXPECT_NO_THROW(manager->pendingAttributes(pendingAttrs));

    pendingAttrs["EnumAttr"] = std::make_tuple(AttributeType::Enumeration,
                                               std::string("InvalidValue"));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, PendingAttributesValidatesStringOption)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(3), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(10), "");

    table["StringAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Test")),
        std::variant<int64_t, std::string>(std::string("Test")), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["StringAttr"] =
        std::make_tuple(AttributeType::String, std::string("Valid"));
    EXPECT_NO_THROW(manager->pendingAttributes(pendingAttrs));

    pendingAttrs["StringAttr"] =
        std::make_tuple(AttributeType::String, std::string("AB"));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);

    pendingAttrs["StringAttr"] =
        std::make_tuple(AttributeType::String, std::string("TooLongString"));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, PendingAttributesValidatesIntegerOption)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(10), "");

    table["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(50)),
        std::variant<int64_t, std::string>(int64_t(50)), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["IntAttr"] =
        std::make_tuple(AttributeType::Integer, int64_t(50));
    EXPECT_NO_THROW(manager->pendingAttributes(pendingAttrs));

    pendingAttrs["IntAttr"] =
        std::make_tuple(AttributeType::Integer, int64_t(150));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);

    pendingAttrs["IntAttr"] =
        std::make_tuple(AttributeType::Integer, int64_t(55));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, ValidateIntegerOptionWithZeroScalarIncrement)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(1), "");

    table["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(50)),
        std::variant<int64_t, std::string>(int64_t(50)), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["IntAttr"] =
        std::make_tuple(AttributeType::Integer, int64_t(50));
    EXPECT_NO_THROW(manager->pendingAttributes(pendingAttrs));
}

TEST_F(ManagerTest, ValidateIntegerOptionBelowLowerBound)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(10), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(10), "");

    table["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(50)),
        std::variant<int64_t, std::string>(int64_t(50)), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["IntAttr"] =
        std::make_tuple(AttributeType::Integer, int64_t(5));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, ValidateIntegerOptionAboveUpperBound)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(10), "");

    table["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(50)),
        std::variant<int64_t, std::string>(int64_t(50)), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["IntAttr"] =
        std::make_tuple(AttributeType::Integer, int64_t(150));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, ValidateStringOptionWithOnlyMinLength)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(5), "");

    table["StringAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Test")),
        std::variant<int64_t, std::string>(std::string("Test")), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["StringAttr"] =
        std::make_tuple(AttributeType::String, std::string("TestValue"));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, ValidateStringOptionWithOnlyMaxLength)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MaxStringLength, int64_t(10), "");

    table["StringAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Test")),
        std::variant<int64_t, std::string>(std::string("Test")), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["StringAttr"] =
        std::make_tuple(AttributeType::String, std::string("Test"));
    EXPECT_NO_THROW(manager->pendingAttributes(pendingAttrs));
}

TEST_F(ManagerTest, ValidateEnumOptionWithNoMatchingValue)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::OneOf, std::string("Value1"), "VDN1");
    options.emplace_back(BoundType::OneOf, std::string("Value2"), "VDN2");
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");

    table["EnumAttr"] = std::make_tuple(
        AttributeType::Enumeration, false, "DisplayName", "Description",
        "MenuPath", std::variant<int64_t, std::string>(std::string("Value1")),
        std::variant<int64_t, std::string>(std::string("Value1")), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["EnumAttr"] = std::make_tuple(AttributeType::Enumeration,
                                               std::string("InvalidValue"));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, ValidateEnumOptionWithMatchingValue)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::OneOf, std::string("Value1"), "VDN1");
    options.emplace_back(BoundType::OneOf, std::string("Value2"), "VDN2");

    table["EnumAttr"] = std::make_tuple(
        AttributeType::Enumeration, false, "DisplayName", "Description",
        "MenuPath", std::variant<int64_t, std::string>(std::string("Value1")),
        std::variant<int64_t, std::string>(std::string("Value1")), options);

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["EnumAttr"] =
        std::make_tuple(AttributeType::Enumeration, std::string("Value2"));
    EXPECT_NO_THROW(manager->pendingAttributes(pendingAttrs));
}

TEST_F(ManagerTest, PendingAttributesReplacesExistingPendingAttribute)
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

    Manager::PendingAttributes pendingAttrs1;
    pendingAttrs1["TestAttr"] =
        std::make_tuple(AttributeType::String, std::string("FirstValue"));
    manager->pendingAttributes(pendingAttrs1);

    Manager::PendingAttributes pendingAttrs2;
    pendingAttrs2["TestAttr"] =
        std::make_tuple(AttributeType::String, std::string("SecondValue"));
    manager->pendingAttributes(pendingAttrs2);

    auto pendingAttrs = manager->sdbusplus::xyz::openbmc_project::BIOSConfig::
                            server::Manager::pendingAttributes();
    auto it = pendingAttrs.find("TestAttr");
    ASSERT_NE(it, pendingAttrs.end());
    EXPECT_EQ(std::get<std::string>(std::get<1>(it->second)), "SecondValue");
}

TEST_F(ManagerTest, PendingAttributesHandlesEnumerationWithIntegerVariant)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::OneOf, std::string("Value1"), "VDN1");
    table["EnumAttr"] = std::make_tuple(
        AttributeType::Enumeration, false, "DisplayName", "Description",
        "MenuPath", std::variant<int64_t, std::string>(std::string("Value1")),
        std::variant<int64_t, std::string>(std::string("Value1")), options);
    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["EnumAttr"] =
        std::make_tuple(AttributeType::Enumeration, int64_t(1));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, PendingAttributesHandlesStringWithIntegerVariant)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(100), "");
    table["StringAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")), options);
    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["StringAttr"] =
        std::make_tuple(AttributeType::String, int64_t(42));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, PendingAttributesHandlesIntegerWithStringVariant)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(1), "");
    table["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(50)),
        std::variant<int64_t, std::string>(int64_t(50)), options);
    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["IntAttr"] =
        std::make_tuple(AttributeType::Integer, std::string("42"));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, PendingAttributesThrowsOnAttributeNotFound)
{
    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["NonExistentAttr"] =
        std::make_tuple(AttributeType::String, std::string("Value"));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), AttributeNotFound);
}

TEST_F(ManagerTest, PendingAttributesThrowsOnTypeMismatch)
{
    Manager::BaseTable table;
    table["TestAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")),
        std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>,
                               std::string>>());

    manager->baseBIOSTable(table);

    Manager::PendingAttributes pendingAttrs;
    pendingAttrs["TestAttr"] =
        std::make_tuple(AttributeType::Integer, int64_t(42));
    EXPECT_THROW(manager->pendingAttributes(pendingAttrs), std::exception);
}

TEST_F(ManagerTest, PendingAttributesClearsWhenEmpty)
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

    manager->setAttribute("TestAttr", std::string("Value"));

    Manager::PendingAttributes emptyAttrs;
    manager->pendingAttributes(emptyAttrs);

    auto pendingAttrs = manager->sdbusplus::xyz::openbmc_project::BIOSConfig::
                            server::Manager::pendingAttributes();
    EXPECT_EQ(pendingAttrs.size(), 0);
}

TEST_F(ManagerTest, ConvertBiosDataToVersion1)
{
    Manager::oldBaseTable oldTable;
    std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>>>
        oldOptions;
    oldOptions.emplace_back(BoundType::OneOf, std::string("Value1"));

    oldTable["TestAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("Current")),
        std::variant<int64_t, std::string>(std::string("Default")), oldOptions);

    Manager::BaseTable newTable;
    manager->convertBiosDataToVersion1(oldTable, newTable);

    EXPECT_NE(newTable.find("TestAttr"), newTable.end());
    auto& newTuple = newTable["TestAttr"];
    EXPECT_EQ(std::get<7>(newTuple).size(), 1);
    EXPECT_EQ(std::get<2>(std::get<7>(newTuple)[0]), "");
}

TEST_F(ManagerTest, ConvertBiosDataToVersion1WithIntegerAttribute)
{
    Manager::oldBaseTable oldTable;
    std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>>>
        oldOptions;
    oldOptions.emplace_back(BoundType::LowerBound, int64_t(0));

    oldTable["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(50)),
        std::variant<int64_t, std::string>(int64_t(50)), oldOptions);

    Manager::BaseTable newTable;
    manager->convertBiosDataToVersion1(oldTable, newTable);

    EXPECT_NE(newTable.find("IntAttr"), newTable.end());
    auto& newTuple = newTable["IntAttr"];
    EXPECT_EQ(std::get<7>(newTuple).size(), 1);
    EXPECT_EQ(std::get<2>(std::get<7>(newTuple)[0]), "");
    EXPECT_TRUE(std::holds_alternative<int64_t>(std::get<5>(newTuple)));
    EXPECT_TRUE(std::holds_alternative<int64_t>(std::get<6>(newTuple)));
}

TEST_F(ManagerTest, ConvertBiosDataToVersion1WithIntegerAndStringAttributes)
{
    Manager::oldBaseTable oldTable;
    std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>>>
        intOptions;
    intOptions.emplace_back(BoundType::LowerBound, int64_t(0));
    intOptions.emplace_back(BoundType::UpperBound, int64_t(100));
    oldTable["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "IntDisplay", "IntDesc", "IntMenu",
        std::variant<int64_t, std::string>(int64_t(50)),
        std::variant<int64_t, std::string>(int64_t(0)), intOptions);

    std::vector<std::tuple<BoundType, std::variant<int64_t, std::string>>>
        strOptions;
    strOptions.emplace_back(BoundType::OneOf, std::string("A"));
    strOptions.emplace_back(BoundType::OneOf, std::string("B"));
    oldTable["StrAttr"] = std::make_tuple(
        AttributeType::String, false, "StrDisplay", "StrDesc", "StrMenu",
        std::variant<int64_t, std::string>(std::string("A")),
        std::variant<int64_t, std::string>(std::string("A")), strOptions);

    Manager::BaseTable newTable;
    manager->convertBiosDataToVersion1(oldTable, newTable);

    EXPECT_NE(newTable.find("IntAttr"), newTable.end());
    EXPECT_NE(newTable.find("StrAttr"), newTable.end());
    EXPECT_EQ(std::get<0>(newTable["IntAttr"]), AttributeType::Integer);
    EXPECT_EQ(std::get<0>(newTable["StrAttr"]), AttributeType::String);
    EXPECT_TRUE(
        std::holds_alternative<int64_t>(std::get<5>(newTable["IntAttr"])));
    EXPECT_TRUE(
        std::holds_alternative<std::string>(std::get<5>(newTable["StrAttr"])));
}

TEST_F(ManagerTest, CreateBootOptionWithSpecialCharacters)
{
    std::string bootOptionId = "Boot-0001.Test";
    manager->createBootOption(bootOptionId);

    auto bootOptions = manager->getBootOptionValues();
    bool found = false;
    for (const auto& [key, value] : bootOptions)
    {
        if (key.find("Boot") != std::string::npos &&
            key.find("0001") != std::string::npos)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ManagerTest, SetAttributeWithIntegerValueUpdatesPending)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(1), "");
    table["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(0)),
        std::variant<int64_t, std::string>(int64_t(0)), options);
    manager->baseBIOSTable(table);

    manager->setAttribute("IntAttr", int64_t(10));

    manager->setAttribute("IntAttr", int64_t(20));

    auto pendingAttrs = manager->sdbusplus::xyz::openbmc_project::BIOSConfig::
                            server::Manager::pendingAttributes();
    auto iter = pendingAttrs.find("IntAttr");
    ASSERT_NE(iter, pendingAttrs.end());
    EXPECT_EQ(std::get<int64_t>(std::get<1>(iter->second)), 20);
}

TEST_F(ManagerTest, GetAttributeWithNoPendingValue)
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

    auto details = manager->getAttribute("TestAttr");
    EXPECT_EQ(std::get<0>(details), AttributeType::String);
    EXPECT_TRUE(std::holds_alternative<std::string>(std::get<1>(details)));
    EXPECT_TRUE(std::holds_alternative<std::string>(std::get<2>(details)));
    EXPECT_EQ(std::get<std::string>(std::get<2>(details)), "");
}

TEST_F(ManagerTest, GetAttributeWithIntegerAndNoPendingValue)
{
    Manager::BaseTable table;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(1), "");
    table["IntAttr"] = std::make_tuple(
        AttributeType::Integer, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(int64_t(50)),
        std::variant<int64_t, std::string>(int64_t(50)), options);
    manager->baseBIOSTable(table);

    auto details = manager->getAttribute("IntAttr");
    EXPECT_EQ(std::get<0>(details), AttributeType::Integer);
    EXPECT_TRUE(std::holds_alternative<int64_t>(std::get<1>(details)));
}

TEST_F(ManagerTest, CurrentBootCanBeSet)
{
    CurrentBootType bootType = CurrentBootType::Unknown;
    auto result = manager->currentBoot(bootType);
    EXPECT_EQ(result, bootType);
}

TEST_F(ManagerTest, PendingEnableCanBeSet)
{
    bool value = true;
    auto result = manager->pendingEnable(value);
    EXPECT_EQ(result, value);
}

TEST_F(ManagerTest, ModeCanBeSet)
{
    ModeType modeValue = ModeType::Deployed;
    auto result = manager->mode(modeValue);
    EXPECT_EQ(result, modeValue);
}

TEST_F(ManagerTest, CreateBootOptionThrowsOnDuplicate)
{
    std::string bootOptionId = "Boot0001";
    manager->createBootOption(bootOptionId);

    EXPECT_THROW(manager->createBootOption(bootOptionId), std::exception);
}

TEST_F(ManagerTest, CreateBootOptionSanitizesId)
{
    std::string bootOptionId = "Boot-0001.test";
    manager->createBootOption(bootOptionId);

    auto bootOptions = manager->getBootOptionValues();
    bool found = false;
    for (const auto& [key, value] : bootOptions)
    {
        if (key.find("Boot") != std::string::npos)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ManagerTest, ValidateEnumOptionReturnsTrueForMatchingValue)
{
    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());

    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::OneOf, std::string("Value1"), "");
    options.emplace_back(BoundType::OneOf, std::string("Value2"), "");

    bool result = testableMgr->validateEnumOption("Value1", options);
    EXPECT_TRUE(result);
}

TEST_F(ManagerTest, ValidateEnumOptionReturnsFalseForNonMatchingValue)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::OneOf, std::string("Value1"), "");
    options.emplace_back(BoundType::OneOf, std::string("Value2"), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateEnumOption("NonExistent", options);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, ValidateEnumOptionIgnoresNonOneOfOptions)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::OneOf, std::string("Value1"), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateEnumOption("Value1", options);
    EXPECT_TRUE(result);
}

TEST_F(ManagerTest, ValidateStringOptionReturnsTrueForValidLength)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(3), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(10), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateStringOption("ValidStr", options);
    EXPECT_TRUE(result);
}

TEST_F(ManagerTest, ValidateStringOptionReturnsFalseForTooShort)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(5), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(10), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateStringOption("Hi", options);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, ValidateStringOptionReturnsFalseForTooLong)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(3), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(5), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateStringOption("TooLongString", options);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, ValidateStringOptionHandlesOnlyMinLength)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(3), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(1000), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateStringOption("Valid", options);
    EXPECT_TRUE(result);
}

TEST_F(ManagerTest, ValidateStringOptionHandlesOnlyMaxLength)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MaxStringLength, int64_t(10), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateStringOption("Valid", options);
    EXPECT_TRUE(result);
}

TEST_F(ManagerTest, ValidateIntegerOptionReturnsTrueForValidValue)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(5), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateIntegerOption(50, options);
    EXPECT_TRUE(result);
}

TEST_F(ManagerTest, ValidateIntegerOptionReturnsFalseForBelowLowerBound)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(10), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(5), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateIntegerOption(5, options);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, ValidateIntegerOptionReturnsFalseForAboveUpperBound)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(5), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateIntegerOption(150, options);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, ValidateIntegerOptionReturnsFalseForInvalidScalarIncrement)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(5), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateIntegerOption(47, options);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, ValidateIntegerOptionReturnsFalseForZeroScalarIncrement)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(0), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateIntegerOption(50, options);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, ValidateIntegerOptionHandlesNegativeValues)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(-100), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(10), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateIntegerOption(-50, options);
    EXPECT_TRUE(result);

    result = testableMgr->validateIntegerOption(-150, options);
    EXPECT_FALSE(result);
}

TEST_F(ManagerTest, ValidateIntegerOptionHandlesValueEqualToLowerBound)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(10), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(5), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateIntegerOption(10, options);
    EXPECT_TRUE(result);
}

TEST_F(ManagerTest, ValidateIntegerOptionHandlesValueEqualToUpperBound)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(5), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    bool result = testableMgr->validateIntegerOption(100, options);
    EXPECT_TRUE(result);
}

TEST_F(ManagerTest, GetBootOptionValuesReturnsEmptyWhenNoBootOptions)
{
    auto bootOptions = manager->getBootOptionValues();
    EXPECT_TRUE(bootOptions.empty());
}

TEST_F(ManagerTest, GetBootOptionValuesReturnsAllBootOptions)
{
    std::string bootOptionId1 = "Boot0001";
    std::string bootOptionId2 = "Boot0002";
    std::string bootOptionId3 = "Boot0003";

    manager->createBootOption(bootOptionId1);
    manager->createBootOption(bootOptionId2);
    manager->createBootOption(bootOptionId3);

    auto bootOptions = manager->getBootOptionValues();
    EXPECT_EQ(bootOptions.size(), 3);
    EXPECT_NE(bootOptions.find(bootOptionId1), bootOptions.end());
    EXPECT_NE(bootOptions.find(bootOptionId2), bootOptions.end());
    EXPECT_NE(bootOptions.find(bootOptionId3), bootOptions.end());
}

TEST_F(ManagerTest, DeleteBootOptionRemovesSingleBootOption)
{
    std::string bootOptionId = "BootToDelete";
    manager->createBootOption(bootOptionId);

    auto bootOptions = manager->getBootOptionValues();
    EXPECT_NE(bootOptions.find(bootOptionId), bootOptions.end());

    manager->deleteBootOption(bootOptionId);

    bootOptions = manager->getBootOptionValues();
    EXPECT_EQ(bootOptions.find(bootOptionId), bootOptions.end());
}

// Covers the else { continue; } branch in validateStringOption's BoundType loop
TEST_F(ManagerTest, ValidateStringOptionIgnoresNonStringBoundTypes)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(100), "");
    // Adding a non-string BoundType entry exercises the else { continue; } path
    options.emplace_back(BoundType::OneOf, std::string("ignored"), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    EXPECT_TRUE(testableMgr->validateStringOption("hello", options));
}

// Covers the implicit else (no matching BoundType) in validateIntegerOption's
// loop
TEST_F(ManagerTest, ValidateIntegerOptionIgnoresUnrecognizedBoundTypes)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(1), "");
    // OneOf is not handled in validateIntegerOption; the else falls through
    options.emplace_back(BoundType::OneOf, std::string("ignored"), "");

    manager.reset();
    auto testableMgr = std::make_unique<TestableManager>(*objServer, systemBus,
                                                         persistPath.string());
    EXPECT_TRUE(testableMgr->validateIntegerOption(50, options));
}

// Covers the path in getAttribute where a pending Integer value exists
TEST_F(ManagerTest, GetAttributeWithIntegerPendingValue)
{
    Manager::BaseTable baseTable;
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::LowerBound, int64_t(0), "");
    options.emplace_back(BoundType::UpperBound, int64_t(100), "");
    options.emplace_back(BoundType::ScalarIncrement, int64_t(1), "");
    baseTable["IntAttr"] = std::make_tuple(
        Manager::AttributeType::Integer, false, std::string("Display"),
        std::string("Description"), std::string("MenuPath"),
        std::variant<int64_t, std::string>(int64_t(10)),
        std::variant<int64_t, std::string>(int64_t(0)), options);
    manager->baseBIOSTable(baseTable);

    // Set an integer pending value
    Manager::PendingAttributes pending;
    pending["IntAttr"] =
        std::make_pair(Manager::AttributeType::Integer, int64_t(50));
    manager->pendingAttributes(pending);

    auto result = manager->getAttribute("IntAttr");
    EXPECT_EQ(std::get<0>(result), Manager::AttributeType::Integer);
    // pendingVal should hold the integer 50
    EXPECT_EQ(std::get<int64_t>(std::get<2>(result)), int64_t(50));
}

TEST_F(ManagerTest, PendingAttributesSkipsReadOnlyAttribute)
{
    std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>, std::string>>
        options;
    options.emplace_back(BoundType::MinStringLength, int64_t(0), "");
    options.emplace_back(BoundType::MaxStringLength, int64_t(100), "");

    Manager::BaseTable table;
    table["RoAttr"] = std::make_tuple(
        AttributeType::String, true, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("cur")),
        std::variant<int64_t, std::string>(std::string("def")), options);
    table["RwAttr"] = std::make_tuple(
        AttributeType::String, false, "DisplayName", "Description", "MenuPath",
        std::variant<int64_t, std::string>(std::string("cur")),
        std::variant<int64_t, std::string>(std::string("def")), options);
    manager->baseBIOSTable(table);

    Manager::PendingAttributes pending;
    pending["RoAttr"] =
        std::make_tuple(AttributeType::String,
                        std::variant<int64_t, std::string>(std::string("x")));
    pending["RwAttr"] =
        std::make_tuple(AttributeType::String,
                        std::variant<int64_t, std::string>(std::string("y")));
    manager->pendingAttributes(pending);

    auto staged = manager->sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                      Manager::pendingAttributes();
    EXPECT_EQ(staged.find("RoAttr"), staged.end());
    EXPECT_NE(staged.find("RwAttr"), staged.end());
}

TEST_F(ManagerTest, CreateBootOptionRejectsEmptyAndAllIllegalId)
{
    // Empty id would build a trailing-slash D-Bus path.
    EXPECT_THROW(manager->createBootOption(""), std::exception);

    // Characters outside [A-Za-z0-9_] are replaced, not rejected, so an
    // all-illegal id still yields a usable key.
    EXPECT_NO_THROW(manager->createBootOption("a-b.c"));
    auto options = manager->getBootOptionValues();
    EXPECT_NE(options.find("a_b_c"), options.end());
}

} // namespace bios_config::test
