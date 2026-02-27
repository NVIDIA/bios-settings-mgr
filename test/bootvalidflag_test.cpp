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

#include "bootvalidflag.hpp"
#include "common.hpp"

#include <boost/asio.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>

#include <chrono>

#include <gmock/gmock.h>

namespace bios_config_valid::test
{

using namespace bios_config_valid;

class TestableBootValidFlag : public BootValidFlag
{
  public:
    using BootValidFlag::BootValidFlag;

    using BootValidFlag::cancelTimer;
    using BootValidFlag::setBootValidFlag;
    using BootValidFlag::setDbusProperty;
    using BootValidFlag::setTimer;
    using BootValidFlag::setupMatches;
};

class BootValidFlagTest : public bios_config::test::BiosConfigTest
{
  protected:
    void SetUp() override
    {
        bios_config::test::BiosConfigTest::SetUp();
        if (systemBus)
        {}
    }

    boost::asio::io_context io;
};

TEST_F(BootValidFlagTest, ConstructorAndRunNoThrow)
{
    EXPECT_NO_THROW(([&]() {
        BootValidFlag bootValidFlag(systemBus, io);
        io.run_for(std::chrono::milliseconds(200));
    }()));
}

TEST_F(BootValidFlagTest, MultipleInstancesNoThrow)
{
    EXPECT_NO_THROW(([&]() {
        BootValidFlag bootValidFlag1(systemBus, io);
        io.run_for(std::chrono::milliseconds(100));
        BootValidFlag bootValidFlag2(systemBus, io);
        io.run_for(std::chrono::milliseconds(100));
    }()));
}

TEST_F(BootValidFlagTest, SetDbusPropertyAndSetBootValidFlagNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));

    EXPECT_NO_THROW(([&]() {
        bios_config_valid::Value value = bool(false);
        bootValidFlag.setDbusProperty(
            "xyz.openbmc_project.Settings",
            "/xyz/openbmc_project/control/host0/boot",
            "xyz.openbmc_project.Object.Enable", "Enabled", value);
        io.run_for(std::chrono::milliseconds(100));
    }()));

    EXPECT_NO_THROW(([&]() {
        bootValidFlag.setBootValidFlag();
        io.run_for(std::chrono::milliseconds(200));
    }()));
}

TEST_F(BootValidFlagTest, SetupMatchesNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));
    EXPECT_NO_THROW(([&]() {
        bootValidFlag.setupMatches(*systemBus);
        io.run_for(std::chrono::milliseconds(100));
    }()));
}

TEST_F(BootValidFlagTest, SetTimerWithEnabledTrueNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));

    auto msg = systemBus->new_signal("/xyz/openbmc_project/control/host0/boot",
                                     "org.freedesktop.DBus.Properties",
                                     "PropertiesChanged");
    std::string interfaceName = "xyz.openbmc_project.Object.Enable";
    std::map<std::string, std::variant<bool>> changedProperties;
    changedProperties["Enabled"] = bool(true);
    std::vector<std::string> invalidatedProperties;
    msg.append(interfaceName, changedProperties, invalidatedProperties);

    try
    {
        bootValidFlag.setTimer(msg);
        io.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetTimerWithBootValidTimeoutOverrideTrueNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));

    auto msg = systemBus->new_signal("/xyz/openbmc_project/control/host0/boot",
                                     "org.freedesktop.DBus.Properties",
                                     "PropertiesChanged");
    std::string interfaceName = "xyz.openbmc_project.Object.Enable";
    std::map<std::string, std::variant<bool>> changedProperties;
    changedProperties["BootValidTimeoutOverride"] = bool(true);
    std::vector<std::string> invalidatedProperties;
    msg.append(interfaceName, changedProperties, invalidatedProperties);

    try
    {
        bootValidFlag.setTimer(msg);
        io.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetTimerWithEnabledFalseNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));

    auto msg = systemBus->new_signal("/xyz/openbmc_project/control/host0/boot",
                                     "org.freedesktop.DBus.Properties",
                                     "PropertiesChanged");
    std::string interfaceName = "xyz.openbmc_project.Object.Enable";
    std::map<std::string, std::variant<bool>> changedProperties;
    changedProperties["Enabled"] = bool(false);
    std::vector<std::string> invalidatedProperties;
    msg.append(interfaceName, changedProperties, invalidatedProperties);

    try
    {
        bootValidFlag.setTimer(msg);
        io.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, CancelTimerWithBootProgressMessageNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));

    auto msg = systemBus->new_signal("/xyz/openbmc_project/state/host0",
                                     "org.freedesktop.DBus.Properties",
                                     "PropertiesChanged");
    std::string interfaceName = "xyz.openbmc_project.State.Boot.Progress";
    std::map<std::string, std::variant<bool>> changedProperties;
    changedProperties["BootProgressLastUpdate"] = bool(true);
    std::vector<std::string> invalidatedProperties;
    msg.append(interfaceName, changedProperties, invalidatedProperties);

    try
    {
        bootValidFlag.cancelTimer(msg);
        io.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetTimerThenCancelTimerNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));

    try
    {
        auto setMsg = systemBus->new_signal(
            "/xyz/openbmc_project/control/host0/boot",
            "org.freedesktop.DBus.Properties", "PropertiesChanged");
        std::string interfaceName = "xyz.openbmc_project.Object.Enable";
        std::map<std::string, std::variant<bool>> changedProperties;
        changedProperties["Enabled"] = bool(true);
        std::vector<std::string> invalidatedProperties;
        setMsg.append(interfaceName, changedProperties, invalidatedProperties);
        bootValidFlag.setTimer(setMsg);
        io.run_for(std::chrono::milliseconds(50));

        auto cancelMsg = systemBus->new_signal(
            "/xyz/openbmc_project/state/host0",
            "org.freedesktop.DBus.Properties", "PropertiesChanged");
        std::string cancelInterfaceName =
            "xyz.openbmc_project.State.Boot.Progress";
        std::map<std::string, std::variant<bool>> cancelChangedProperties;
        cancelChangedProperties["BootProgressLastUpdate"] = bool(true);
        std::vector<std::string> cancelInvalidatedProperties;
        cancelMsg.append(cancelInterfaceName, cancelChangedProperties,
                         cancelInvalidatedProperties);

        bootValidFlag.cancelTimer(cancelMsg);
        io.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetDbusPropertyOnNonexistentPathNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));

    bios_config_valid::Value value = bool(false);
    EXPECT_NO_THROW(([&]() {
        bootValidFlag.setDbusProperty(
            "nonexistent.service", "/nonexistent/path", "nonexistent.interface",
            "NonexistentProperty", value);
        io.run_for(std::chrono::milliseconds(100));
    }()));
}

} // namespace bios_config_valid::test
