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

#pragma once

#include <boost/asio.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace bios_config::test
{

namespace fs = std::filesystem;

class BiosConfigTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        char tmplt[] = "/tmp/bios_config_test.XXXXXX";
        tempDir = fs::path(mkdtemp(tmplt));
        persistPath = tempDir / "biosData";
        seedPath = tempDir / "seedData";

        systemBus = std::make_shared<sdbusplus::asio::connection>(ioContext);
        objServer = std::make_unique<sdbusplus::asio::object_server>(systemBus);
    }

    void TearDown() override
    {
        ioContext.restart();
        ioContext.run_for(std::chrono::milliseconds(200));

        if (fs::exists(tempDir))
        {
            fs::remove_all(tempDir);
        }
    }

    boost::asio::io_context ioContext;
    std::shared_ptr<sdbusplus::asio::connection> systemBus;
    std::unique_ptr<sdbusplus::asio::object_server> objServer;
    fs::path tempDir;
    fs::path persistPath;
    fs::path seedPath;
};

} // namespace bios_config::test
