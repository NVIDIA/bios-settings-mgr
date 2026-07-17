#include "bootvalidflag.hpp"
#include "common.hpp"
#include "manager.hpp"
#include "manager_serialize.hpp"
#include "password.hpp"
#include "rfutility.hpp"
#include "secureboot.hpp"

#include <boost/asio/stream_file.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

enum class AllocationFailureMode
{
    BadAlloc,
    NonStdException,
};

int failAfterAllocation = -1;
AllocationFailureMode allocationFailureMode = AllocationFailureMode::BadAlloc;
thread_local bool insideAllocator = false;

void maybeFailAllocation()
{
    if (insideAllocator || failAfterAllocation < 0)
    {
        return;
    }

    if (failAfterAllocation == 0)
    {
        const AllocationFailureMode mode = allocationFailureMode;
        failAfterAllocation = -1;
        allocationFailureMode = AllocationFailureMode::BadAlloc;
        if (mode == AllocationFailureMode::NonStdException)
        {
            throw 1;
        }
        throw std::bad_alloc();
    }
    --failAfterAllocation;
}

void* allocateBytes(std::size_t size)
{
    maybeFailAllocation();
    insideAllocator = true;
    void* ptr = std::malloc(size == 0 ? 1 : size);
    insideAllocator = false;
    if (ptr == nullptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

void* allocateAlignedBytes(std::size_t size, std::align_val_t alignment)
{
    maybeFailAllocation();
    insideAllocator = true;
    void* ptr = nullptr;
    const auto align = static_cast<std::size_t>(alignment);
    const int rc = posix_memalign(&ptr, align, size == 0 ? 1 : size);
    insideAllocator = false;
    if (rc != 0 || ptr == nullptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

// Restores the injection globals even if func() exits via an exception.
struct AllocationFailureGuard
{
    ~AllocationFailureGuard()
    {
        failAfterAllocation = -1;
        allocationFailureMode = AllocationFailureMode::BadAlloc;
    }
};

template <typename Func>
void runWithAllocationFailure(
    int failAt, Func&& func,
    AllocationFailureMode mode = AllocationFailureMode::BadAlloc)
{
    AllocationFailureGuard guard;
    failAfterAllocation = failAt;
    allocationFailureMode = mode;
    try
    {
        func();
    }
    catch (const std::bad_alloc&)
    {
        // Expected: the injected allocation failure surfaced directly.
    }
    catch (const std::exception&)
    {
        // Expected: an intermediate layer translated the injected failure
        // into its own exception type (sdbusplus, cereal, filesystem, ...).
    }
    catch (int)
    {
        // Expected: AllocationFailureMode::NonStdException throws a plain
        // int to exercise catch(...) handlers in the code under test.
    }
    // Any other exception type is unexpected and propagates so the test
    // fails instead of silently masking a regression.
}

} // namespace

void* operator new(std::size_t size)
{
    return allocateBytes(size);
}

void* operator new[](std::size_t size)
{
    return allocateBytes(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocateAlignedBytes(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocateAlignedBytes(size, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return allocateBytes(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return allocateBytes(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void operator delete(void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept
{
    std::free(ptr);
}

namespace bios_config::test
{

using ManagerServer =
    sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager;

class AllocationFailureTest : public BiosConfigTest
{
  protected:
    void SetUp() override
    {
        BiosConfigTest::SetUp();
        // Construct the io_context's io_uring file service before any test
        // injects allocation failures. boost::asio performs one-time service
        // setup allocations on the first stream_file creation (used by
        // asyncSerialize) which are not unwound if an injected
        // std::bad_alloc lands mid-construction and would be reported as a
        // leak. Mirrors the AsioConnection warm-up in
        // RedfishEventAllocationFailures.
        boost::asio::stream_file warmup(ioContext);
        (void)warmup;
    }
};

class TestableBootValidFlag : public bios_config_valid::BootValidFlag
{
  public:
    using BootValidFlag::BootValidFlag;
    using BootValidFlag::setDbusProperty;
};

class TestablePassword : public bios_config_pwd::Password
{
  public:
    using Password::Password;

    using Password::getParam;

    // Overload discarding the seed JSON, preserving the four-argument shape.
    bool getParam(
        std::array<uint8_t, bios_config_pwd::maxHashSize>& orgUsrPwdHash,
        std::array<uint8_t, bios_config_pwd::maxHashSize>& orgAdminPwdHash,
        std::array<uint8_t, bios_config_pwd::maxSeedSize>& seed,
        std::string& hashAlgo)
    {
        nlohmann::json discarded;
        return Password::getParam(orgUsrPwdHash, orgAdminPwdHash, seed,
                                  hashAlgo, discarded);
    }
};

TEST_F(AllocationFailureTest, RedfishEventAllocationFailures)
{
    // Warm up the static AsioConnection singleton with a clean call first so
    // its one-time io_context/sd_bus connection is constructed outside of an
    // injected allocation failure. Otherwise the singleton can be first built
    // mid-injection and cached in a degraded state (observed as an asio
    // "Bad file descriptor" on later, failure-free calls).
    EXPECT_NO_THROW(sendRedfishEvent("Property", "Value", "/xyz/test/object"));

    for (int failAt = 0; failAt < 80; ++failAt)
    {
        runWithAllocationFailure(failAt, [] {
            sendRedfishEvent("Property", "Value", "/xyz/test/object");
        });
    }

    EXPECT_NO_THROW(sendRedfishEvent("Property", "Value", "/xyz/test/object"));
}

TEST_F(AllocationFailureTest, BootValidFlagSetPropertyAllocationFailures)
{
    TestableBootValidFlag bootValidFlag(systemBus, ioContext);
    const std::string service(80, 's');
    const std::string path = "/" + std::string(80, 'p');
    const std::string interface(80, 'i');
    const std::string property(80, 'v');

    for (int failAt = 0; failAt < 120; ++failAt)
    {
        bios_config_valid::Value value(false);
        runWithAllocationFailure(failAt, [&] {
            bootValidFlag.setDbusProperty(service, path, interface, property,
                                          value);
        });
    }

    SUCCEED();
}

TEST_F(AllocationFailureTest, ManagerCreateBootOptionAllocationFailures)
{
    for (int failAt = 0; failAt < 800; ++failAt)
    {
        auto path = tempDir / ("manager_alloc_" + std::to_string(failAt));
        auto manager =
            std::make_unique<Manager>(*objServer, systemBus, path.string());

        runWithAllocationFailure(failAt, [&] {
            manager->createBootOption("BootAlloc" + std::to_string(failAt));
        });
    }

    SUCCEED();
}

TEST_F(AllocationFailureTest, ManagerSerializationAllocationFailures)
{
    auto manager = std::make_unique<Manager>(
        *objServer, systemBus, (tempDir / "serialize_alloc").string());
    manager->ManagerServer::enableAfterReset(true, true);

    for (int failAt = 0; failAt < 120; ++failAt)
    {
        runWithAllocationFailure(failAt, [&] {
            std::string buffer;
            (void)serializeToBuffer(*manager, buffer);
        });
    }

    for (int failAt = 0; failAt < 120; ++failAt)
    {
        auto path = tempDir / ("serialize_alloc_" + std::to_string(failAt));
        runWithAllocationFailure(failAt, [&] { serialize(*manager, path); });
    }

    for (int failAt = 0; failAt < 120; ++failAt)
    {
        auto path = tempDir / ("async_alloc_" + std::to_string(failAt));
        runWithAllocationFailure(failAt, [&] {
            asyncSerialize(ioContext, *manager, path);
            ioContext.restart();
            ioContext.run_for(std::chrono::milliseconds(1));
        });
    }

    SUCCEED();
}

TEST_F(AllocationFailureTest, PasswordGetParamAllocationFailures)
{
    const auto passwordDir = tempDir / "password_alloc";
    std::filesystem::create_directories(passwordDir);

    nlohmann::json seedData;
    seedData["HashAlgo"] = "SHA256";
    seedData["Seed"] = std::vector<uint8_t>(bios_config_pwd::maxSeedSize, 0xAA);
    seedData["UserPwdHash"] =
        std::vector<uint8_t>(bios_config_pwd::maxHashSize, 0x11);
    seedData["AdminPwdHash"] =
        std::vector<uint8_t>(bios_config_pwd::maxHashSize, 0x22);
    {
        std::ofstream file(passwordDir / bios_config_pwd::biosSeedFile);
        file << seedData.dump();
    }

    auto password = std::make_unique<TestablePassword>(*objServer, systemBus,
                                                       passwordDir.string());
    for (int failAt = 0; failAt < 39; ++failAt)
    {
        runWithAllocationFailure(failAt, [&] {
            std::array<uint8_t, bios_config_pwd::maxHashSize> userHash;
            std::array<uint8_t, bios_config_pwd::maxHashSize> adminHash;
            std::array<uint8_t, bios_config_pwd::maxSeedSize> seed;
            std::string hashAlgo;
            (void)password->getParam(userHash, adminHash, seed, hashAlgo);
        });
    }

    SUCCEED();
}

TEST_F(AllocationFailureTest, SecureBootAllocationFailures)
{
    auto secureboot = std::make_unique<SecureBoot>(
        *objServer, systemBus, (tempDir / "secureboot_alloc").string());

    for (int failAt = 0; failAt < 120; ++failAt)
    {
        runWithAllocationFailure(failAt, [&] {
            secureboot->pendingEnable((failAt % 2) == 0);
        });
    }

    SUCCEED();
}

TEST_F(AllocationFailureTest, NonStdAllocationFailures)
{
    auto manager = std::make_unique<Manager>(
        *objServer, systemBus, (tempDir / "non_std_manager").string());
    manager->ManagerServer::enableAfterReset(true, true);

    for (int failAt = 0; failAt < 120; ++failAt)
    {
        runWithAllocationFailure(
            failAt,
            [&] {
                std::string buffer;
                (void)serializeToBuffer(*manager, buffer);
            },
            AllocationFailureMode::NonStdException);
    }

    for (int failAt = 0; failAt < 120; ++failAt)
    {
        auto path = tempDir / ("non_std_serialize_" + std::to_string(failAt));
        runWithAllocationFailure(
            failAt, [&] { serialize(*manager, path); },
            AllocationFailureMode::NonStdException);
    }

    for (int failAt = 0; failAt < 120; ++failAt)
    {
        auto path = tempDir / ("non_std_async_" + std::to_string(failAt));
        runWithAllocationFailure(
            failAt,
            [&] {
                asyncSerialize(ioContext, *manager, path);
                ioContext.restart();
                ioContext.run_for(std::chrono::milliseconds(1));
            },
            AllocationFailureMode::NonStdException);
    }

    for (int failAt = 0; failAt < 200; ++failAt)
    {
        const auto path =
            tempDir / ("non_std_deserialize_" + std::to_string(failAt));
        {
            std::ofstream file(path, std::ios::binary);
            std::uint32_t version = BIOS_CONFIG_VERSION_2;
            file.write(reinterpret_cast<const char*>(&version),
                       sizeof(version));
        }
        runWithAllocationFailure(
            failAt, [&] { (void)deserialize(path, *manager); },
            AllocationFailureMode::NonStdException);
        std::filesystem::remove(path);
    }

    for (int failAt = 0; failAt < 1200; ++failAt)
    {
        const std::string id =
            "Boot" + std::string(96, 'A') + std::to_string(failAt);
        runWithAllocationFailure(
            failAt, [&] { manager->createBootOption(id); },
            AllocationFailureMode::NonStdException);
    }

    auto secureboot = std::make_unique<SecureBoot>(
        *objServer, systemBus, (tempDir / "non_std_secureboot").string());
    for (int failAt = 0; failAt < 120; ++failAt)
    {
        runWithAllocationFailure(
            failAt, [&] { secureboot->pendingEnable((failAt % 2) == 0); },
            AllocationFailureMode::NonStdException);
    }
    secureboot.reset();

    const auto secureBootDir = tempDir / "non_std_secureboot_deserialize";
    std::filesystem::create_directories(secureBootDir);
    for (int failAt = 0; failAt < 360; ++failAt)
    {
        {
            std::ofstream file(secureBootDir / secureBootPersistFile,
                               std::ios::binary);
            file << "BAD";
        }
        runWithAllocationFailure(
            failAt,
            [&] {
                auto invalidSecureboot = std::make_unique<SecureBoot>(
                    *objServer, systemBus, secureBootDir.string());
                (void)invalidSecureboot;
            },
            AllocationFailureMode::NonStdException);
    }

    for (int failAt = 0; failAt < 160; ++failAt)
    {
        const auto path =
            tempDir / ("non_std_password_ctor_" + std::to_string(failAt));
        runWithAllocationFailure(
            failAt,
            [&] {
                auto password = std::make_unique<bios_config_pwd::Password>(
                    *objServer, systemBus, path.string());
                (void)password;
            },
            AllocationFailureMode::NonStdException);
    }

    SUCCEED();
}

} // namespace bios_config::test
