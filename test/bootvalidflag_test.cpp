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

#include <systemd/sd-bus.h>

#include <boost/asio.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <gmock/gmock.h>

namespace bios_config_valid::test
{

using namespace bios_config_valid;

namespace
{

sdbusplus::message_t buildPropertiesChangedMessage(
    const char* path, const std::string& interfaceName,
    const std::map<std::string, bool>& changedProperties)
{
    sd_bus* bus = nullptr;
    if (sd_bus_new(&bus) < 0)
    {
        throw std::runtime_error("Failed to create test D-Bus");
    }

    (void)sd_bus_set_address(bus,
                             "unix:abstract=bios-settings-mgr-test-message");
    (void)sd_bus_start(bus);

    sd_bus_message* raw = nullptr;
    if (sd_bus_message_new_signal(bus, &raw, path,
                                  "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged") < 0)
    {
        sd_bus_unref(bus);
        throw std::runtime_error("Failed to create PropertiesChanged message");
    }

    const char* interface = interfaceName.c_str();
    (void)sd_bus_message_append_basic(raw, 's', interface);

    (void)sd_bus_message_open_container(raw, 'a', "{sv}");
    for (const auto& [propertyName, propertyValue] : changedProperties)
    {
        const char* property = propertyName.c_str();
        int value = propertyValue ? 1 : 0;

        (void)sd_bus_message_open_container(raw, 'e', "sv");
        (void)sd_bus_message_append_basic(raw, 's', property);
        (void)sd_bus_message_open_container(raw, 'v', "b");
        (void)sd_bus_message_append_basic(raw, 'b', &value);
        (void)sd_bus_message_close_container(raw);
        (void)sd_bus_message_close_container(raw);
    }
    (void)sd_bus_message_close_container(raw);

    (void)sd_bus_message_open_container(raw, 'a', "s");
    (void)sd_bus_message_close_container(raw);

    (void)sd_bus_message_seal(raw, 1, 0);
    (void)sd_bus_message_rewind(raw, 1);

    sd_bus_unref(bus);
    return sdbusplus::message_t(raw, std::false_type{});
}

} // namespace

class TestableBootValidFlag : public BootValidFlag
{
  public:
    using BootValidFlag::BootValidFlag;

    void installTimer(boost::asio::io_context& io)
    {
        timer_60 = std::make_unique<boost::asio::steady_timer>(io);
    }

    std::chrono::steady_clock::time_point timerExpiry() const
    {
        return timer_60->expiry();
    }

    void setTimerExpiry(std::chrono::steady_clock::time_point expiry)
    {
        timer_60->expires_at(expiry);
    }

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
    bootValidFlag.installTimer(io);
    io.run_for(std::chrono::milliseconds(100));

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", {{"Enabled", true}});

    try
    {
        bootValidFlag.setTimer(msg);
        io.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetTimerWithEnabledTrueStartsTimer)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    bootValidFlag.installTimer(io);

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", {{"Enabled", true}});

    bootValidFlag.setTimer(msg);
    EXPECT_GT(bootValidFlag.timerExpiry(), std::chrono::steady_clock::now());
}

TEST_F(BootValidFlagTest, SetTimerWithBootValidTimeoutOverrideTrueNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable",
        {{"BootValidTimeoutOverride", true}});

    try
    {
        bootValidFlag.setTimer(msg);
        io.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetTimerWithBootValidTimeoutOverrideFalseStartsTimer)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    bootValidFlag.installTimer(io);

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable",
        {{"BootValidTimeoutOverride", false}});

    bootValidFlag.setTimer(msg);
    EXPECT_GT(bootValidFlag.timerExpiry(), std::chrono::steady_clock::now());
}

TEST_F(BootValidFlagTest, SetTimerWithEnabledFalseNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    io.run_for(std::chrono::milliseconds(100));

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", {{"Enabled", false}});

    try
    {
        bootValidFlag.setTimer(msg);
        io.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetTimerWithUnrelatedPropertyStartsTimer)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    bootValidFlag.installTimer(io);

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", {{"OtherProperty", true}});

    bootValidFlag.setTimer(msg);
    EXPECT_GT(bootValidFlag.timerExpiry(), std::chrono::steady_clock::now());
}

TEST_F(BootValidFlagTest, CancelTimerWithBootProgressMessageNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    bootValidFlag.installTimer(io);
    io.run_for(std::chrono::milliseconds(100));

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Boot.Progress",
        {{"BootProgressLastUpdate", true}});

    try
    {
        bootValidFlag.cancelTimer(msg);
        io.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, CancelTimerWithActiveTimerCancelsBootProgress)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    bootValidFlag.installTimer(io);
    bootValidFlag.setTimerExpiry(
        std::chrono::steady_clock::now() + std::chrono::seconds(60));

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Boot.Progress",
        {{"BootProgressLastUpdate", true}});

    EXPECT_NO_THROW(bootValidFlag.cancelTimer(msg));
}

TEST_F(BootValidFlagTest, CancelTimerIgnoresUnrelatedProperty)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    bootValidFlag.installTimer(io);
    const auto expiry =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    bootValidFlag.setTimerExpiry(expiry);

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Boot.Progress", {{"OtherProperty", true}});

    bootValidFlag.cancelTimer(msg);
    EXPECT_EQ(bootValidFlag.timerExpiry(), expiry);
}

TEST_F(BootValidFlagTest, CancelTimerWithExpiredTimerDoesNotCancel)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    bootValidFlag.installTimer(io);
    bootValidFlag.setTimerExpiry(
        std::chrono::steady_clock::now() - std::chrono::seconds(1));

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Boot.Progress",
        {{"BootProgressLastUpdate", true}});

    EXPECT_NO_THROW(bootValidFlag.cancelTimer(msg));
}

TEST_F(BootValidFlagTest, SetTimerThenCancelTimerNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, io);
    bootValidFlag.installTimer(io);
    io.run_for(std::chrono::milliseconds(100));

    try
    {
        auto setMsg = buildPropertiesChangedMessage(
            "/xyz/openbmc_project/control/host0/boot",
            "xyz.openbmc_project.Object.Enable", {{"Enabled", true}});
        bootValidFlag.setTimer(setMsg);
        io.run_for(std::chrono::milliseconds(50));

        auto cancelMsg = buildPropertiesChangedMessage(
            "/xyz/openbmc_project/state/host0",
            "xyz.openbmc_project.State.Boot.Progress",
            {{"BootProgressLastUpdate", true}});

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
