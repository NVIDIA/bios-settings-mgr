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
    using BootValidFlag::processBootFlagOneTimeValue;
    using BootValidFlag::processBootFlagTimeoutValue;
    using BootValidFlag::processCancelTimerProperties;
    using BootValidFlag::processSetTimerProperties;
    using BootValidFlag::processTimerCallback;
    using BootValidFlag::setBootValidFlag;
    using BootValidFlag::setDbusProperty;
    using BootValidFlag::setTimer;
    using BootValidFlag::setupMatches;
    using BootValidFlag::timer_60;
};

class BootValidFlagTest : public bios_config::test::BiosConfigTest
{
  protected:
    void SetUp() override
    {
        bios_config::test::BiosConfigTest::SetUp();
    }

    void addSettingsInterfaces(bool oneTimeEnabled, bool timeoutOverride)
    {
        systemBus->request_name("xyz.openbmc_project.Settings");

        auto bootEnable =
            objServer->add_interface("/xyz/openbmc_project/control/host0/boot",
                                     "xyz.openbmc_project.Object.Enable");
        bootEnable->register_property(
            "Enabled", true, sdbusplus::asio::PropertyPermission::readWrite);
        bootEnable->initialize();

        auto timeout = objServer->add_interface(
            "/xyz/openbmc_project/control/host0/boot",
            "xyz.openbmc_project.Control.Boot.BootSettingsExpiryOverride");
        timeout->register_property(
            "BootValidTimeoutOverride", timeoutOverride,
            sdbusplus::asio::PropertyPermission::readWrite);
        timeout->initialize();

        auto oneTime = objServer->add_interface(
            "/xyz/openbmc_project/control/host0/boot/one_time",
            "xyz.openbmc_project.Object.Enable");
        oneTime->register_property(
            "Enabled", oneTimeEnabled,
            sdbusplus::asio::PropertyPermission::readWrite);
        oneTime->initialize();
    }
};

// ---- existing tests (preserved without change) ----

TEST_F(BootValidFlagTest, ConstructorAndRunNoThrow)
{
    EXPECT_NO_THROW(([&]() {
        BootValidFlag bootValidFlag(systemBus, ioContext);
        ioContext.run_for(std::chrono::milliseconds(200));
    }()));
}

TEST_F(BootValidFlagTest, MultipleInstancesNoThrow)
{
    EXPECT_NO_THROW(([&]() {
        BootValidFlag bootValidFlag1(systemBus, ioContext);
        ioContext.run_for(std::chrono::milliseconds(100));
        ioContext.restart();
        BootValidFlag bootValidFlag2(systemBus, ioContext);
        ioContext.run_for(std::chrono::milliseconds(100));
    }()));
}

TEST_F(BootValidFlagTest, SetDbusPropertyAndSetBootValidFlagNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    EXPECT_NO_THROW(([&]() {
        bios_config_valid::Value value = bool(false);
        bootValidFlag.setDbusProperty(
            "xyz.openbmc_project.Settings",
            "/xyz/openbmc_project/control/host0/boot",
            "xyz.openbmc_project.Object.Enable", "Enabled", value);
        ioContext.run_for(std::chrono::milliseconds(100));
        ioContext.restart();
    }()));

    EXPECT_NO_THROW(([&]() {
        bootValidFlag.setBootValidFlag();
        ioContext.run_for(std::chrono::milliseconds(200));
    }()));
}

TEST_F(BootValidFlagTest, SetupMatchesNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();
    EXPECT_NO_THROW(([&]() {
        bootValidFlag.setupMatches(*systemBus);
        ioContext.run_for(std::chrono::milliseconds(100));
    }()));
}

TEST_F(BootValidFlagTest, SetTimerWithEnabledTrueNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.installTimer(ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", {{"Enabled", true}});

    try
    {
        bootValidFlag.setTimer(msg);
        ioContext.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetTimerWithEnabledTrueStartsTimer)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.installTimer(ioContext);

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", {{"Enabled", true}});

    bootValidFlag.setTimer(msg);
    EXPECT_GT(bootValidFlag.timerExpiry(), std::chrono::steady_clock::now());
}

TEST_F(BootValidFlagTest, SetTimerWithBootValidTimeoutOverrideTrueNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable",
        {{"BootValidTimeoutOverride", true}});

    try
    {
        bootValidFlag.setTimer(msg);
        ioContext.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetTimerWithBootValidTimeoutOverrideFalseStartsTimer)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.installTimer(ioContext);

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable",
        {{"BootValidTimeoutOverride", false}});

    bootValidFlag.setTimer(msg);
    EXPECT_GT(bootValidFlag.timerExpiry(), std::chrono::steady_clock::now());
}

TEST_F(BootValidFlagTest, SetTimerWithEnabledFalseNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", {{"Enabled", false}});

    try
    {
        bootValidFlag.setTimer(msg);
        ioContext.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetTimerWithUnrelatedPropertyStartsTimer)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.installTimer(ioContext);

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", {{"OtherProperty", true}});

    bootValidFlag.setTimer(msg);
    EXPECT_GT(bootValidFlag.timerExpiry(), std::chrono::steady_clock::now());
}

TEST_F(BootValidFlagTest, CancelTimerWithBootProgressMessageNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.installTimer(ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));

    auto msg = buildPropertiesChangedMessage(
        "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Boot.Progress",
        {{"BootProgressLastUpdate", true}});

    try
    {
        bootValidFlag.cancelTimer(msg);
        ioContext.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, CancelTimerWithActiveTimerCancelsBootProgress)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.installTimer(ioContext);
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
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.installTimer(ioContext);
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
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.installTimer(ioContext);
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
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.installTimer(ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));

    try
    {
        auto setMsg = buildPropertiesChangedMessage(
            "/xyz/openbmc_project/control/host0/boot",
            "xyz.openbmc_project.Object.Enable", {{"Enabled", true}});
        bootValidFlag.setTimer(setMsg);
        ioContext.run_for(std::chrono::milliseconds(50));
        ioContext.restart();

        auto cancelMsg = buildPropertiesChangedMessage(
            "/xyz/openbmc_project/state/host0",
            "xyz.openbmc_project.State.Boot.Progress",
            {{"BootProgressLastUpdate", true}});

        bootValidFlag.cancelTimer(cancelMsg);
        ioContext.run_for(std::chrono::milliseconds(100));
    }
    catch (const sdbusplus::exception::SdBusError&)
    {}
}

TEST_F(BootValidFlagTest, SetDbusPropertyOnNonexistentPathNoThrow)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    bios_config_valid::Value value = bool(false);
    EXPECT_NO_THROW(([&]() {
        bootValidFlag.setDbusProperty(
            "nonexistent.service", "/nonexistent/path", "nonexistent.interface",
            "NonexistentProperty", value);
        ioContext.run_for(std::chrono::milliseconds(100));
    }()));
}

TEST_F(BootValidFlagTest, ConstructorUsesSettingsServiceSuccessCallbacks)
{
    addSettingsInterfaces(true, false);

    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(500));

    EXPECT_NE(bootValidFlag.timer_60, nullptr);
}

// ---- new tests for helper methods ----

// Note: onInitSuccess() itself is fully covered by
// ConstructorUsesSettingsServiceSuccessCallbacks above, which reaches it
// through the constructor's success callback with a real Settings service
// registered so every async D-Bus call gets a reply that sd-bus can use to
// release its completion-handler storage. Invoking onInitSuccess() directly
// from the test body leaves its Get reply unprocessed at test end, which
// valgrind reports as a leak, so no separate direct-call test exists.

TEST_F(BootValidFlagTest, ProcessBootFlagOneTimeValueWithPersistentFlagSet)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // oneTimeEnabled=false means persistentFlag=true, so the !persistentFlag
    // branch is NOT entered (no D-Bus call made)
    EXPECT_NO_THROW(bootValidFlag.processBootFlagOneTimeValue(false));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessBootFlagOneTimeValueWithPersistentFlagNotSet)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // oneTimeEnabled=true means persistentFlag=false, so the !persistentFlag
    // branch IS entered; async D-Bus call is made (fires with ec!=0 in test
    // env)
    EXPECT_NO_THROW(bootValidFlag.processBootFlagOneTimeValue(true));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessBootFlagTimeoutValueWhenTimeoutEnabled)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // bootFlagTimeoutDis=true means !bootFlagTimeoutDis=false, no
    // setDbusProperty
    EXPECT_NO_THROW(bootValidFlag.processBootFlagTimeoutValue(true));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessBootFlagTimeoutValueWhenTimeoutDisabled)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // bootFlagTimeoutDis=false means !bootFlagTimeoutDis=true, setDbusProperty
    // is called (async D-Bus call fires with ec!=0 in test env)
    EXPECT_NO_THROW(bootValidFlag.processBootFlagTimeoutValue(false));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessTimerCallbackOnSuccess)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // success: error_code{} means no error — calls setBootValidFlag internally
    EXPECT_NO_THROW(
        bootValidFlag.processTimerCallback(boost::system::error_code{}));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessTimerCallbackOnError)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // error path: operation_aborted is the standard timer-cancelled error
    EXPECT_NO_THROW(bootValidFlag.processTimerCallback(
        boost::asio::error::operation_aborted));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest,
       ProcessSetTimerPropertiesWithBootValidTimeoutOverrideTrue)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // BootValidTimeoutOverride=true → early return, timer NOT started
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    std::map<std::string, std::variant<bool>> props;
    props["BootValidTimeoutOverride"] = bool(true);
    EXPECT_NO_THROW(bootValidFlag.processSetTimerProperties(props));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest,
       ProcessSetTimerPropertiesWithBootValidTimeoutOverrideFalse)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // BootValidTimeoutOverride=false → no early return, falls through to timer
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    std::map<std::string, std::variant<bool>> props;
    props["BootValidTimeoutOverride"] = bool(false);
    EXPECT_NO_THROW(bootValidFlag.processSetTimerProperties(props));
    // Cancel the 60s timer so io_context doesn't run forever
    bootValidFlag.timer_60->cancel();
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessSetTimerPropertiesWithEnabledFalse)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // Enabled=false → early return, timer NOT started
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    std::map<std::string, std::variant<bool>> props;
    props["Enabled"] = bool(false);
    EXPECT_NO_THROW(bootValidFlag.processSetTimerProperties(props));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessSetTimerPropertiesWithEnabledTrue)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // Enabled=true → no early return, falls through to timer
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    std::map<std::string, std::variant<bool>> props;
    props["Enabled"] = bool(true);
    EXPECT_NO_THROW(bootValidFlag.processSetTimerProperties(props));
    // Cancel the 60s timer so io_context doesn't block
    bootValidFlag.timer_60->cancel();
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessSetTimerPropertiesWithUnrecognizedProperty)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // An unrecognized property falls through both if/else-if branches and
    // reaches timer setup
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    std::map<std::string, std::variant<bool>> props;
    props["UnknownProperty"] = bool(true);
    EXPECT_NO_THROW(bootValidFlag.processSetTimerProperties(props));
    bootValidFlag.timer_60->cancel();
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessSetTimerPropertiesWithEmptyMap)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // Empty map: loop body never executes, falls straight to timer setup
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    std::map<std::string, std::variant<bool>> props;
    EXPECT_NO_THROW(bootValidFlag.processSetTimerProperties(props));
    bootValidFlag.timer_60->cancel();
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest,
       ProcessCancelTimerPropertiesWithBootProgressAndActiveTimer)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // Set timer with a future expiry so timer_60->expiry() > now() is true
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    bootValidFlag.timer_60->expires_after(std::chrono::seconds(100));

    std::map<std::string, std::variant<bool>> props;
    props["BootProgressLastUpdate"] = bool(true);
    EXPECT_NO_THROW(bootValidFlag.processCancelTimerProperties(props));
    // Timer was cancelled; run io to consume the operation_aborted callback
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest,
       ProcessCancelTimerPropertiesWithBootProgressAndExpiredTimer)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    // Set timer expiry to a point in the past so expiry() > now() is false
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    bootValidFlag.timer_60->expires_at(
        std::chrono::steady_clock::now() - std::chrono::seconds(1));

    std::map<std::string, std::variant<bool>> props;
    props["BootProgressLastUpdate"] = bool(true);
    EXPECT_NO_THROW(bootValidFlag.processCancelTimerProperties(props));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessCancelTimerPropertiesWithUnrelatedProperty)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    bootValidFlag.timer_60->expires_after(std::chrono::seconds(100));

    // Property is not "BootProgressLastUpdate" → inner if body not entered
    std::map<std::string, std::variant<bool>> props;
    props["SomeOtherProperty"] = bool(true);
    EXPECT_NO_THROW(bootValidFlag.processCancelTimerProperties(props));
    bootValidFlag.timer_60->cancel();
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessCancelTimerPropertiesWithEmptyMap)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);

    // Empty map: loop never executes, no crash
    std::map<std::string, std::variant<bool>> props;
    EXPECT_NO_THROW(bootValidFlag.processCancelTimerProperties(props));
    ioContext.run_for(std::chrono::milliseconds(100));
}

TEST_F(BootValidFlagTest, ProcessSetTimerThenCancelViaHelpers)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    ioContext.run_for(std::chrono::milliseconds(100));
    ioContext.restart();

    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);

    // Start timer
    std::map<std::string, std::variant<bool>> setProps;
    setProps["Enabled"] = bool(true);
    EXPECT_NO_THROW(bootValidFlag.processSetTimerProperties(setProps));

    // Timer should now be active; cancel it via processCancelTimerProperties
    std::map<std::string, std::variant<bool>> cancelProps;
    cancelProps["BootProgressLastUpdate"] = bool(true);
    EXPECT_NO_THROW(bootValidFlag.processCancelTimerProperties(cancelProps));

    // Let the operation_aborted callback fire (which calls
    // processTimerCallback)
    ioContext.run_for(std::chrono::milliseconds(100));
}

// Helper: build a sdbusplus message that is readable (sealed + rewound).
// new_signal() creates an unsealed message; msg.read() throws EPERM unless
// the message has been sealed first.  We seal it via the raw sd-bus API so
// that setTimer() / cancelTimer() can actually execute their msg.read() call.
static sdbusplus::message_t makeReadablePropertiesChangedMsg(
    sdbusplus::bus_t& bus, const std::string& path, const std::string& ifname,
    const std::map<std::string, std::variant<bool>>& props)
{
    auto msg = bus.new_signal(path.c_str(), "org.freedesktop.DBus.Properties",
                              "PropertiesChanged");
    std::vector<std::string> invalidated;
    msg.append(ifname, props, invalidated);
    // Seal the message so sd_bus allows reading from it.
    sd_bus_message_seal(msg.get(), 1, 0);
    // Rewind to the start of the body so msg.read() starts at field 0.
    sd_bus_message_rewind(msg.get(), true);
    return msg;
}

TEST_F(BootValidFlagTest, SetTimerWithReadableMessageCoversLine204)
{
    // Uses a properly sealed message so that msg.read() in setTimer() succeeds,
    // covering the fallthrough branch at line 204 and executing line 205.
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    ioContext.run_for(std::chrono::milliseconds(50));
    ioContext.restart();

    std::map<std::string, std::variant<bool>> props;
    props["Enabled"] = bool(true);

    auto msg = makeReadablePropertiesChangedMsg(
        *systemBus, "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", props);

    EXPECT_NO_THROW(bootValidFlag.setTimer(msg));
}

TEST_F(BootValidFlagTest, CancelTimerWithReadableMessageCoversLine157)
{
    // Uses a properly sealed message so that msg.read() in cancelTimer()
    // succeeds, covering the fallthrough branch at line 157 and executing line
    // 158.
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    bootValidFlag.timer_60 =
        std::make_unique<boost::asio::steady_timer>(ioContext);
    ioContext.run_for(std::chrono::milliseconds(50));
    ioContext.restart();

    std::map<std::string, std::variant<bool>> props;
    props["BootProgressLastUpdate"] = bool(true);

    auto msg = makeReadablePropertiesChangedMsg(
        *systemBus, "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Boot.Progress", props);

    EXPECT_NO_THROW(bootValidFlag.cancelTimer(msg));
}

} // namespace bios_config_valid::test
