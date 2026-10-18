// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2020-2021 NVIDIA CORPORATION &
// AFFILIATES
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
#include "password_pam.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <xyz/openbmc_project/BIOSConfig/Common/error.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <fstream>

namespace bios_config_pwd::test
{

using namespace bios_config_pwd;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

class PasswordTest : public bios_config::test::BiosConfigTest
{
  protected:
    void SetUp() override
    {
        bios_config::test::BiosConfigTest::SetUp();
        if (objServer && systemBus)
        {
            password = std::make_unique<Password>(*objServer, systemBus,
                                                  persistPath.string());
        }
    }

    std::filesystem::path stateFilePath() const
    {
        return persistPath / passwordStateFile;
    }

    void writeStateFile(const std::string& bytes)
    {
        std::ofstream os(stateFilePath(), std::ios::out | std::ios::binary);
        ASSERT_TRUE(os.is_open());
        os.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    // A directory in place of the temporary file makes every persist attempt
    // fail, even when the tests run as root.
    void breakPersist()
    {
        std::filesystem::create_directory(persistPath / "password_state.tmp");
    }

    std::unique_ptr<Password> password;
};

TEST_F(PasswordTest, ConstructorCreatesPassword)
{
    EXPECT_NE(password, nullptr);
}

// sdbusplus only turns the errors a method declares into a D-Bus error reply;
// any other exception terminates the daemon.
TEST_F(PasswordTest, ChangePasswordIsRefusedWithDeclaredError)
{
    EXPECT_THROW(password->changePassword("user", "old", "new"),
                 sdbusplus::xyz::openbmc_project::BIOSConfig::Common::Error::
                     PasswordNotSettable);
}

TEST_F(PasswordTest, ConstructorThrowsWhenPersistPathIsFile)
{
    password.reset();

    const auto filePath = tempDir / "persist_is_file";
    {
        std::ofstream file(filePath);
        ASSERT_TRUE(file.is_open());
    }

    EXPECT_THROW(Password(*objServer, systemBus, filePath.string()),
                 InternalFailure);
}

TEST_F(PasswordTest, PropertiesDefaultFalseWithNoStateFile)
{
    EXPECT_FALSE(password->getPasswordInitialized());
    EXPECT_FALSE(password->getPasswordSync());
}

TEST_F(PasswordTest, SettingPasswordInitializedPersists)
{
    password->passwordInitialized(true);
    EXPECT_TRUE(password->getPasswordInitialized());
    EXPECT_TRUE(std::filesystem::exists(stateFilePath()));

    // Fixed object path: drop the fixture object before re-creating it.
    password.reset();
    Password reloaded(*objServer, systemBus, persistPath.string());
    deserializePasswordState(stateFilePath(), reloaded);
    EXPECT_TRUE(reloaded.getPasswordInitialized());
    EXPECT_FALSE(reloaded.getPasswordSync());
}

TEST_F(PasswordTest, SettingPasswordSyncPersists)
{
    password->passwordSync(true);
    EXPECT_TRUE(password->getPasswordSync());

    // Fixed object path: drop the fixture object before re-creating it.
    password.reset();
    Password reloaded(*objServer, systemBus, persistPath.string());
    deserializePasswordState(stateFilePath(), reloaded);
    EXPECT_FALSE(reloaded.getPasswordInitialized());
    EXPECT_TRUE(reloaded.getPasswordSync());
}

TEST_F(PasswordTest, DeserializeMissingFileSetsBothFalse)
{
    password->passwordInitialized(true);
    password->passwordSync(true);
    std::filesystem::remove(stateFilePath());

    EXPECT_FALSE(deserializePasswordState(stateFilePath(), *password));
    EXPECT_FALSE(password->getPasswordInitialized());
    EXPECT_FALSE(password->getPasswordSync());
}

TEST_F(PasswordTest, DeserializeLegacyTwoBoolFileLoads)
{
    {
        std::ofstream os(stateFilePath(), std::ios::out | std::ios::binary);
        ASSERT_TRUE(os.is_open());
        cereal::BinaryOutputArchive archive(os);
        bool initialized = true;
        bool changed = true;
        archive(initialized, changed);
    }

    EXPECT_TRUE(deserializePasswordState(stateFilePath(), *password));
    EXPECT_TRUE(password->getPasswordInitialized());
    EXPECT_TRUE(password->getPasswordSync());
}

TEST_F(PasswordTest, DeserializeFutureVersionFailsClosed)
{
    {
        std::ofstream os(stateFilePath(), std::ios::out | std::ios::binary);
        ASSERT_TRUE(os.is_open());
        cereal::BinaryOutputArchive archive(os);
        constexpr uint32_t magic =
            (static_cast<uint32_t>('B') << 0) |
            (static_cast<uint32_t>('C') << 8) |
            (static_cast<uint32_t>('P') << 16) |
            (static_cast<uint32_t>('W') << 24);
        archive(magic, passwordStateSerializeVersion + 1, true, true);
    }

    EXPECT_FALSE(deserializePasswordState(stateFilePath(), *password));
    EXPECT_FALSE(password->getPasswordInitialized());
    EXPECT_FALSE(password->getPasswordSync());
}

TEST_F(PasswordTest, DeserializeCorruptFileFailsClosed)
{
    {
        std::ofstream os(stateFilePath(), std::ios::out | std::ios::binary);
        ASSERT_TRUE(os.is_open());
        os << "not a valid password_state file";
    }

    EXPECT_FALSE(deserializePasswordState(stateFilePath(), *password));
    EXPECT_FALSE(password->getPasswordInitialized());
    EXPECT_FALSE(password->getPasswordSync());
}

TEST_F(PasswordTest, DeserializeLegacyNonBooleanBytesFailClosed)
{
    password->passwordInitialized(true);
    password->passwordSync(true);
    writeStateFile("no");

    EXPECT_FALSE(deserializePasswordState(stateFilePath(), *password));
    EXPECT_FALSE(password->getPasswordInitialized());
    EXPECT_FALSE(password->getPasswordSync());
}

TEST_F(PasswordTest, DeserializeTornHeaderFailsClosed)
{
    // 1 to 7 bytes of a header-shaped file, e.g. a write cut short.
    const std::string header = "BCPWxxxx";
    for (size_t len = 1; len < sizeof(uint32_t) * 2; ++len)
    {
        password->passwordInitialized(true);
        password->passwordSync(true);
        writeStateFile(header.substr(0, len));

        EXPECT_FALSE(deserializePasswordState(stateFilePath(), *password))
            << "length " << len;
        EXPECT_FALSE(password->getPasswordInitialized()) << "length " << len;
        EXPECT_FALSE(password->getPasswordSync()) << "length " << len;
    }
}

TEST_F(PasswordTest, DeserializeNonBooleanPayloadFailsClosed)
{
    password->passwordInitialized(true);
    {
        std::ofstream os(stateFilePath(), std::ios::out | std::ios::binary);
        ASSERT_TRUE(os.is_open());
        cereal::BinaryOutputArchive archive(os);
        constexpr uint32_t magic =
            (static_cast<uint32_t>('B') << 0) |
            (static_cast<uint32_t>('C') << 8) |
            (static_cast<uint32_t>('P') << 16) |
            (static_cast<uint32_t>('W') << 24);
        const uint8_t initialized = 2;
        const uint8_t sync = 1;
        archive(magic, passwordStateSerializeVersion, initialized, sync);
    }

    EXPECT_FALSE(deserializePasswordState(stateFilePath(), *password));
    EXPECT_FALSE(password->getPasswordInitialized());
    EXPECT_FALSE(password->getPasswordSync());
}

TEST_F(PasswordTest, DeserializeTrailingBytesFailClosed)
{
    password->passwordInitialized(true);
    {
        std::ofstream os(stateFilePath(), std::ios::out | std::ios::binary);
        ASSERT_TRUE(os.is_open());
        cereal::BinaryOutputArchive archive(os);
        constexpr uint32_t magic =
            (static_cast<uint32_t>('B') << 0) |
            (static_cast<uint32_t>('C') << 8) |
            (static_cast<uint32_t>('P') << 16) |
            (static_cast<uint32_t>('W') << 24);
        archive(magic, passwordStateSerializeVersion, true, true, true);
    }

    EXPECT_FALSE(deserializePasswordState(stateFilePath(), *password));
    EXPECT_FALSE(password->getPasswordInitialized());
    EXPECT_FALSE(password->getPasswordSync());
}

TEST_F(PasswordTest, PersistedStateIsVersionedAndLeavesNoTempFile)
{
    password->passwordInitialized(true);

    // magic + fileVer + two bools
    EXPECT_EQ(std::filesystem::file_size(stateFilePath()), 10u);
    EXPECT_FALSE(std::filesystem::exists(
        stateFilePath().string() + std::string(".tmp")));
}

TEST_F(PasswordTest, SetFailsWithoutChangingPropertyWhenPersistFails)
{
    breakPersist();

    // SdBusError, not the generated InternalFailure: only sdbusplus internal
    // exceptions become an error reply from a property setter; anything else
    // terminates the daemon.
    EXPECT_THROW(password->passwordInitialized(true),
                 sdbusplus::exception::SdBusError);
    EXPECT_FALSE(password->getPasswordInitialized());
    EXPECT_THROW(password->passwordSync(true),
                 sdbusplus::exception::SdBusError);
    EXPECT_FALSE(password->getPasswordSync());
    EXPECT_FALSE(std::filesystem::exists(stateFilePath()));
}

TEST_F(PasswordTest, FailedSetKeepsPreviouslyPersistedState)
{
    password->passwordInitialized(true);
    breakPersist();

    EXPECT_THROW(password->passwordSync(true),
                 sdbusplus::exception::SdBusError);
    EXPECT_TRUE(password->getPasswordInitialized());
    EXPECT_FALSE(password->getPasswordSync());

    std::filesystem::remove_all(persistPath / "password_state.tmp");
    // Fixed object path: drop the fixture object before re-creating it.
    password.reset();
    Password reloaded(*objServer, systemBus, persistPath.string());
    EXPECT_TRUE(deserializePasswordState(stateFilePath(), reloaded));
    EXPECT_TRUE(reloaded.getPasswordInitialized());
    EXPECT_FALSE(reloaded.getPasswordSync());
}

} // namespace bios_config_pwd::test
