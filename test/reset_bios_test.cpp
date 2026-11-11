// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.

#include "asio_connection.hpp"
#include "common.hpp"
#include "manager.hpp"

#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace bios_config::test
{
namespace
{
constexpr auto biosEroTSpiService = "com.Nvidia.BiosEroTSpi";
constexpr auto clearNVRAMPath = "/xyz/openbmc_project/bios/clear_nvram";
constexpr auto clearNVRAMInterface = "com.nvidia.BIOS.ClearNVRAM";
constexpr auto settingsService = "xyz.openbmc_project.Settings";
constexpr auto bootSettingsPath = "/xyz/openbmc_project/control/host0/boot";
constexpr auto bootFlagsInterface = "xyz.openbmc_project.Control.Boot.Flags";
constexpr auto enableInterface = "xyz.openbmc_project.Object.Enable";
constexpr int maxPollAttempts = 100;
constexpr auto pollInterval = std::chrono::milliseconds(20);
constexpr auto settleDuration = std::chrono::milliseconds(200);
constexpr auto deferredClearDelay = std::chrono::milliseconds(100);
constexpr auto drainStep = std::chrono::milliseconds(50);
constexpr auto drainTimeout = std::chrono::seconds(5);
} // namespace

class TestableManager : public Manager
{
  public:
    using Manager::handleBiosNvramServiceCheck;
    using Manager::Manager;

    ResetFlag resetBIOSSettings(ResetFlag value) override
    {
        if (failResetCleanup && value == ResetFlag::NoAction)
        {
            ++cleanupResetAttempts;
            throw std::runtime_error("Injected reset cleanup failure");
        }
        return Manager::resetBIOSSettings(value);
    }

    bool failResetCleanup = false;
    size_t cleanupResetAttempts = 0;
};

class ResetBiosTest : public BiosConfigTest
{
  protected:
    virtual bool registerClearBackend() const
    {
        return true;
    }

    void handleClear(boost::asio::yield_context yield)
    {
        const auto call = ++clearCalls;
        if (rejectFromCall != 0 && call >= rejectFromCall)
        {
            clearRejected = true;
            throw std::runtime_error("Clear rejected");
        }

        if (successDelay != std::chrono::milliseconds::zero())
        {
            boost::asio::steady_timer timer(ioContext, successDelay);
            timer.async_wait(yield);
            successReturnedAfterRejection = clearRejected;
        }
    }

    void SetUp() override
    {
        char tempDirectoryTemplate[] = "/tmp/bios_config_test.XXXXXX";
        const char* tempDirectory = mkdtemp(tempDirectoryTemplate);
        ASSERT_NE(tempDirectory, nullptr);

        tempDir = fs::path(tempDirectory);
        persistPath = tempDir / "biosData";
        seedPath = tempDir / "seedData";
        systemBus = std::make_shared<sdbusplus::asio::connection>(
            ioContext, sdbusplus::bus::new_bus());
        objServer = std::make_unique<sdbusplus::asio::object_server>(systemBus);

        systemBus->request_name(settingsService);

        if (registerClearBackend())
        {
            systemBus->request_name(biosEroTSpiService);
            clearInterface =
                objServer->add_interface(clearNVRAMPath, clearNVRAMInterface);
            clearInterface->register_method(
                "Clear", [this](boost::asio::yield_context yield) {
                    handleClear(yield);
                });
            clearInterface->initialize();
        }

        cmosInterface =
            objServer->add_interface(bootSettingsPath, bootFlagsInterface);
        cmosInterface->register_property(
            "CMOSClear", cmosClear,
            [this](const bool& requested, bool& current) {
                current = requested;
                cmosClear = requested;
                return true;
            },
            [](const bool& current) { return current; });
        cmosInterface->initialize();

        enableObjectInterface =
            objServer->add_interface(bootSettingsPath, enableInterface);
        enableObjectInterface->register_property(
            "Enabled", bootOverrideEnabled,
            [this](const bool& requested, bool& current) {
                current = requested;
                bootOverrideEnabled = requested;
                return true;
            },
            [](const bool& current) { return current; });
        enableObjectInterface->initialize();

        manager = std::make_unique<TestableManager>(*objServer, systemBus,
                                                    persistPath.string());
    }

    void TearDown() override
    {
        drainPendingWork();
        drainRedfishEvents();
        manager.reset();

        if (objServer)
        {
            if (enableObjectInterface)
            {
                objServer->remove_interface(enableObjectInterface);
            }
            if (cmosInterface)
            {
                objServer->remove_interface(cmosInterface);
            }
            if (clearInterface)
            {
                objServer->remove_interface(clearInterface);
            }
        }

        enableObjectInterface.reset();
        cmosInterface.reset();
        clearInterface.reset();
        objServer.reset();
        BiosConfigTest::TearDown();

        if (systemBus)
        {
            systemBus->close();
            systemBus.reset();
        }
    }

    void runUntil(const std::function<bool()>& done)
    {
        for (int i = 0; i < maxPollAttempts && !done(); ++i)
        {
            ioContext.restart();
            ioContext.run_for(pollInterval);
        }
    }

    void settle()
    {
        ioContext.restart();
        ioContext.run_for(settleDuration);
    }

    bool hasPendingSerialization() const
    {
        std::error_code ec;
        if (!std::filesystem::exists(tempDir, ec))
        {
            return ec != std::error_code{};
        }

        std::filesystem::recursive_directory_iterator entry(tempDir, ec);
        const std::filesystem::recursive_directory_iterator end;
        while (!ec && entry != end)
        {
            if (entry->path().extension() == ".tmp")
            {
                return true;
            }
            entry.increment(ec);
        }
        return ec != std::error_code{};
    }

    void drainPendingWork()
    {
        settle();

        for (auto waited = std::chrono::milliseconds::zero();
             waited < drainTimeout; waited += drainStep)
        {
            if (!hasPendingSerialization())
            {
                return;
            }

            ioContext.restart();
            ioContext.run_for(drainStep);
        }

        EXPECT_FALSE(hasPendingSerialization());
    }

    void drainRedfishEvents()
    {
        auto& redfishBus =
            phosphor::logging::AsioConnection::getAsioConnection();
        auto& redfishIoContext = redfishBus->get_io_context();
        redfishIoContext.restart();
        redfishIoContext.run_for(settleDuration);
    }

    Manager::ResetFlag resetFlag()
    {
        return static_cast<Base&>(*manager).resetBIOSSettings();
    }

    std::unique_ptr<TestableManager> manager;
    std::shared_ptr<sdbusplus::asio::dbus_interface> clearInterface;
    std::shared_ptr<sdbusplus::asio::dbus_interface> cmosInterface;
    std::shared_ptr<sdbusplus::asio::dbus_interface> enableObjectInterface;
    bool cmosClear = true;
    bool bootOverrideEnabled = true;
    size_t clearCalls = 0;
    size_t rejectFromCall = 0;
    std::chrono::milliseconds successDelay{0};
    bool clearRejected = false;
    bool successReturnedAfterRejection = false;
};

class ResetBiosWithoutBackendTest : public ResetBiosTest
{
  protected:
    bool registerClearBackend() const override
    {
        return false;
    }
};

TEST_F(ResetBiosWithoutBackendTest,
       FactoryDefaultsPreservesLegacyBehaviorWithoutBackend)
{
    manager->resetBIOSSettings(Manager::ResetFlag::FactoryDefaults);
    settle();

    EXPECT_EQ(clearCalls, size_t{0});
    EXPECT_EQ(resetFlag(), Manager::ResetFlag::FactoryDefaults);
    EXPECT_TRUE(cmosClear);
    EXPECT_TRUE(bootOverrideEnabled);
}

TEST_F(ResetBiosTest, ServiceCheckFailureDoesNotRequestClear)
{
    manager->handleBiosNvramServiceCheck(
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        true);
    settle();

    EXPECT_EQ(clearCalls, size_t{0});
    EXPECT_EQ(resetFlag(), Manager::ResetFlag::NoAction);
    EXPECT_TRUE(cmosClear);
    EXPECT_TRUE(bootOverrideEnabled);
}

TEST_F(ResetBiosTest, SuccessfulClearResetsBootTimeTriggers)
{
    successDelay = deferredClearDelay;
    manager->resetBIOSSettings(Manager::ResetFlag::FactoryDefaults);

    runUntil([this] { return clearCalls == 1; });

    EXPECT_EQ(resetFlag(), Manager::ResetFlag::FactoryDefaults);
    EXPECT_TRUE(cmosClear);
    EXPECT_TRUE(bootOverrideEnabled);

    runUntil([this] {
        return resetFlag() == Manager::ResetFlag::NoAction && !cmosClear &&
               !bootOverrideEnabled;
    });

    EXPECT_EQ(clearCalls, size_t{1});
    EXPECT_EQ(resetFlag(), Manager::ResetFlag::NoAction);
    EXPECT_FALSE(cmosClear);
    EXPECT_FALSE(bootOverrideEnabled);
}

TEST_F(ResetBiosTest, FailedClearPreservesBootTimeTriggers)
{
    rejectFromCall = 1;
    manager->resetBIOSSettings(Manager::ResetFlag::FactoryDefaults);

    runUntil([this] { return clearCalls == 1; });
    settle();

    EXPECT_EQ(resetFlag(), Manager::ResetFlag::FactoryDefaults);
    EXPECT_TRUE(cmosClear);
    EXPECT_TRUE(bootOverrideEnabled);
}

TEST_F(ResetBiosTest, FailedCmosClearStillClearsIndependentTriggers)
{
    objServer->remove_interface(cmosInterface);
    cmosInterface.reset();
    manager->resetBIOSSettings(Manager::ResetFlag::FactoryDefaults);

    runUntil([this] { return clearCalls == 1; });
    settle();

    EXPECT_EQ(resetFlag(), Manager::ResetFlag::NoAction);
    EXPECT_TRUE(cmosClear);
    EXPECT_FALSE(bootOverrideEnabled);
}

TEST_F(ResetBiosTest, FailedBootOverrideStillClearsIndependentTriggers)
{
    objServer->remove_interface(enableObjectInterface);
    enableObjectInterface.reset();
    manager->resetBIOSSettings(Manager::ResetFlag::FactoryDefaults);

    runUntil([this] { return clearCalls == 1 && !cmosClear; });
    settle();

    EXPECT_EQ(resetFlag(), Manager::ResetFlag::NoAction);
    EXPECT_FALSE(cmosClear);
    EXPECT_TRUE(bootOverrideEnabled);
}

TEST_F(ResetBiosTest, FailedResetFlagCleanupPreservesResetTrigger)
{
    manager->failResetCleanup = true;
    manager->resetBIOSSettings(Manager::ResetFlag::FactoryDefaults);

    runUntil([this] { return manager->cleanupResetAttempts == 1; });

    EXPECT_EQ(manager->cleanupResetAttempts, size_t{1});
    EXPECT_EQ(resetFlag(), Manager::ResetFlag::FactoryDefaults);
    EXPECT_FALSE(cmosClear);
    EXPECT_FALSE(bootOverrideEnabled);
}

TEST_F(ResetBiosTest, OverlappingRequestsCoalesceOnFirstSuccessfulClear)
{
    rejectFromCall = 2;
    successDelay = deferredClearDelay;
    manager->resetBIOSSettings(Manager::ResetFlag::FactoryDefaults);
    manager->resetBIOSSettings(Manager::ResetFlag::FactoryDefaults);

    runUntil([this] {
        return clearCalls == 2 && resetFlag() == Manager::ResetFlag::NoAction &&
               !cmosClear && !bootOverrideEnabled;
    });

    EXPECT_EQ(clearCalls, size_t{2});
    EXPECT_TRUE(successReturnedAfterRejection);
    EXPECT_EQ(resetFlag(), Manager::ResetFlag::NoAction);
    EXPECT_FALSE(cmosClear);
    EXPECT_FALSE(bootOverrideEnabled);
}

} // namespace bios_config::test
