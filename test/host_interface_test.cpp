// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
#include "common.hpp"
#include "host_interface.hpp"

#include <dlfcn.h>
#include <grp.h>

#include <sdbusplus/asio/object_server.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{
// Members reported for the redfish-hostiface group; empty means no group.
std::vector<std::string> hostIfaceMembers;
} // namespace

// The users to delete come from getgrnam(); answer for the host interface
// group here and pass every other lookup to libc.
extern "C" struct group* getgrnam(const char* name)
{
    static group g{};
    static std::vector<std::string> names;
    static std::vector<char*> members;
    if (std::strcmp(name, "redfish-hostiface") != 0)
    {
        using Fn = struct group* (*)(const char*);
        static auto* real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "getgrnam"));
        return real != nullptr ? real(name) : nullptr;
    }
    if (hostIfaceMembers.empty())
    {
        return nullptr;
    }
    names = hostIfaceMembers;
    members.clear();
    for (auto& n : names)
    {
        members.push_back(n.data());
    }
    members.push_back(nullptr);
    g.gr_mem = members.data();
    return &g;
}

namespace bios_config::test
{
namespace
{
constexpr auto osPrefix =
    "xyz.openbmc_project.State.OperatingSystem.Status.OSStatus.";
constexpr auto osStatusIntf =
    "xyz.openbmc_project.State.OperatingSystem.Status";
constexpr auto ethernetIntf = "xyz.openbmc_project.Network.EthernetInterface";
} // namespace

class HostInterfaceTest : public BiosConfigTest
{
  protected:
    void TearDown() override
    {
        hostIfaceMembers.clear();
        BiosConfigTest::TearDown();
    }

    void addHost(const std::string& state)
    {
        systemBus->request_name("xyz.openbmc_project.State.Host");
        hostIface = objServer->add_interface("/xyz/openbmc_project/state/host0",
                                             osStatusIntf);
        hostIface->register_property("OperatingSystemState",
                                     std::string(osPrefix) + state);
        hostIface->register_property("Unrelated", int64_t{0});
        hostIface->register_property("Description", std::string("host"));
        hostIface->initialize();
    }

    void addNic(bool enabled)
    {
        systemBus->request_name("xyz.openbmc_project.Network");
        nicIface = objServer->add_interface(
            "/xyz/openbmc_project/network/hostusb0", ethernetIntf);
        nicIface->register_property(
            "NICEnabled", enabled,
            [this](const bool& req, bool& old) {
                old = req;
                nicEnabled = req;
                return true;
            },
            [](const bool& v) { return v; });
        nicIface->initialize();
        nicEnabled = enabled;
    }

    void addUser(const std::string& name)
    {
        systemBus->request_name("xyz.openbmc_project.User.Manager");
        auto iface =
            objServer->add_interface("/xyz/openbmc_project/user/" + name,
                                     "xyz.openbmc_project.Object.Delete");
        iface->register_method("Delete", [this, name]() {
            deleted.insert(name);
        });
        iface->initialize();
        userIfaces.push_back(iface);
    }

    std::unique_ptr<HostInterface> make(std::optional<std::string> setting)
    {
        return std::make_unique<HostInterface>(
            systemBus, [this] { ++resets; }, [setting] { return setting; });
    }

    void runUntil(const std::function<bool()>& done)
    {
        for (int i = 0; i < 100 && !done(); ++i)
        {
            ioContext.restart();
            ioContext.run_for(std::chrono::milliseconds(20));
        }
    }

    void settle()
    {
        ioContext.restart();
        ioContext.run_for(std::chrono::milliseconds(300));
    }

    std::shared_ptr<sdbusplus::asio::dbus_interface> hostIface;
    std::shared_ptr<sdbusplus::asio::dbus_interface> nicIface;
    std::vector<std::shared_ptr<sdbusplus::asio::dbus_interface>> userIfaces;
    std::optional<bool> nicEnabled;
    std::set<std::string> deleted;
    int resets = 0;
};

TEST_F(HostInterfaceTest, InactiveDeletesUsersRearmsBootstrapEnablesNic)
{
    hostIfaceMembers = {"hiuser"};
    addUser("hiuser");
    addNic(false);
    addHost("Inactive");

    auto hi = make(std::string("Disabled"));
    runUntil([this] { return resets == 1 && nicEnabled == true; });

    EXPECT_EQ(resets, 1);
    EXPECT_EQ(nicEnabled, true);
    runUntil([this] { return deleted.contains("hiuser"); });
    EXPECT_THAT(deleted, ::testing::ElementsAre("hiuser"));
}

TEST_F(HostInterfaceTest, StandbyDisablesNicWhenBiosSaysDisabled)
{
    addNic(true);
    addHost("Standby");

    auto hi = make(std::string("Disabled"));
    runUntil([this] { return nicEnabled == false; });

    EXPECT_EQ(nicEnabled, false);
    EXPECT_EQ(resets, 0);
}

TEST_F(HostInterfaceTest, StandbyEnablesNicWhenBiosAllowsIt)
{
    addNic(false);
    addHost("Standby");

    auto hi = make(std::string("Enabled"));
    runUntil([this] { return nicEnabled == true; });

    EXPECT_EQ(nicEnabled, true);
    EXPECT_EQ(resets, 0);
}

TEST_F(HostInterfaceTest, StandbyLeavesNicWithoutBiosAttribute)
{
    addNic(true);
    addHost("Standby");

    auto hi = make(std::nullopt);
    settle();

    EXPECT_EQ(nicEnabled, true);
    EXPECT_EQ(resets, 0);
}

TEST_F(HostInterfaceTest, OtherStatesChangeNothing)
{
    addNic(true);
    addHost("BootComplete");

    auto hi = make(std::string("Disabled"));
    settle();

    EXPECT_EQ(nicEnabled, true);
    EXPECT_EQ(resets, 0);
}

TEST_F(HostInterfaceTest, AppliesOsStateChanges)
{
    addNic(true);
    addHost("Standby");
    auto hi = make(std::nullopt);
    settle();
    ASSERT_EQ(resets, 0);

    hostIface->set_property("OperatingSystemState",
                            std::string(osPrefix) + "Inactive");
    runUntil([this] { return resets == 1; });

    EXPECT_EQ(resets, 1);
}

TEST_F(HostInterfaceTest, IgnoresChangesOfOtherTypes)
{
    addNic(true);
    addHost("Standby");
    auto hi = make(std::nullopt);
    settle();

    hostIface->set_property("Unrelated", int64_t{1});
    settle();

    EXPECT_EQ(resets, 0);
    EXPECT_EQ(nicEnabled, true);
}

TEST_F(HostInterfaceTest, ReadsStateWhenHostServiceAppears)
{
    addNic(false);
    auto hi = make(std::string("Enabled"));
    settle();
    ASSERT_EQ(nicEnabled, false);

    addHost("Standby");
    runUntil([this] { return nicEnabled == true; });

    EXPECT_EQ(nicEnabled, true);
}

TEST_F(HostInterfaceTest, FailedCallsAreLoggedNotFatal)
{
    hostIfaceMembers = {"nouser"};
    addHost("Inactive");

    auto hi = make(std::nullopt);
    runUntil([this] { return resets == 1; });
    settle();

    EXPECT_EQ(resets, 1);
    EXPECT_FALSE(nicEnabled.has_value());
}

TEST(RedfishHostInterfaceSetting, ReadsStringAttribute)
{
    Manager::BaseTable table;
    EXPECT_EQ(redfishHostInterfaceSetting(table), std::nullopt);

    auto& entry = table["RedfishHostInterface"];
    std::get<5>(entry) = std::string("Disabled");
    EXPECT_EQ(redfishHostInterfaceSetting(table), "Disabled");

    std::get<5>(entry) = int64_t{1};
    EXPECT_EQ(redfishHostInterfaceSetting(table), std::nullopt);
}

TEST_F(HostInterfaceTest, IgnoresOtherStringProperties)
{
    addNic(true);
    addHost("Standby");
    auto hi = make(std::nullopt);
    settle();

    hostIface->set_property("Description", std::string("changed"));
    settle();

    EXPECT_EQ(resets, 0);
    EXPECT_EQ(nicEnabled, true);
}

TEST_F(HostInterfaceTest, IgnoresHostServiceGoingAway)
{
    addNic(true);
    auto hi = make(std::nullopt);
    settle();

    // A second connection owns the host name and then closes, so the name
    // gets a new owner and later none.
    boost::asio::io_context otherIo;
    auto other = std::make_shared<sdbusplus::asio::connection>(otherIo);
    other->request_name("xyz.openbmc_project.State.Host");
    settle();
    other.reset();
    settle();

    EXPECT_EQ(resets, 0);
    EXPECT_EQ(nicEnabled, true);
}

TEST_F(HostInterfaceTest, IgnoresMalformedPropertiesChanged)
{
    addNic(true);
    addHost("Standby");
    auto hi = make(std::nullopt);
    settle();

    auto signal = systemBus->new_signal("/xyz/openbmc_project/state/host0",
                                        "org.freedesktop.DBus.Properties",
                                        "PropertiesChanged");
    // The match filters on arg0, so the interface name goes first; the
    // changed-properties dictionary is replaced by an integer.
    signal.append(std::string(osStatusIntf), int32_t{1});
    signal.signal_send();
    settle();

    EXPECT_EQ(resets, 0);
    EXPECT_EQ(nicEnabled, true);
}

class HostInterfaceManagerTest : public BiosConfigTest
{
  protected:
    void SetUp() override
    {
        BiosConfigTest::SetUp();
        manager = std::make_unique<Manager>(*objServer, systemBus,
                                            persistPath.string());
    }

    std::unique_ptr<Manager> manager;
};

TEST_F(HostInterfaceManagerTest, RearmCopiesEnableAfterReset)
{
    manager->enableAfterReset(true);
    manager->credentialBootstrap(false);
    rearmCredentialBootstrap(*manager);
    EXPECT_TRUE(static_cast<Base&>(*manager).credentialBootstrap());

    manager->enableAfterReset(false);
    rearmCredentialBootstrap(*manager);
    EXPECT_FALSE(static_cast<Base&>(*manager).credentialBootstrap());
}

TEST_F(HostInterfaceManagerTest, SettingOfReadsManagerTable)
{
    EXPECT_EQ(redfishHostInterfaceSettingOf(*manager), std::nullopt);
}

} // namespace bios_config::test
