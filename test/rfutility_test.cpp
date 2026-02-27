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
#include "rfutility.hpp"

#include <gmock/gmock.h>

namespace bios_config::test
{

using namespace bios_config;

class RfUtilityTest : public BiosConfigTest
{
  protected:
    void SetUp() override
    {
        BiosConfigTest::SetUp();
    }
};

TEST_F(RfUtilityTest, ParsePropertyValueAndSendEventExtractsValueAfterLastDot)
{
    std::string propertyName = "TestProperty";
    std::string dbusPropertyValue = "xyz.openbmc_project.Test.Value";
    std::string objectPath = "/test/path";

    EXPECT_NO_THROW(parsePropertyValueAndSendEvent(
        propertyName, dbusPropertyValue, objectPath));
}

TEST_F(RfUtilityTest, ParsePropertyValueAndSendEventHandlesNoDot)
{
    std::string propertyName = "TestProperty";
    std::string dbusPropertyValue = "SimpleValue";
    std::string objectPath = "/test/path";

    EXPECT_NO_THROW(parsePropertyValueAndSendEvent(
        propertyName, dbusPropertyValue, objectPath));
}

TEST_F(RfUtilityTest, ParsePropertyValueAndSendEventHandlesMultipleDots)
{
    std::string propertyName = "TestProperty";
    std::string dbusPropertyValue = "xyz.openbmc_project.Test.Some.Value";
    std::string objectPath = "/test/path";

    EXPECT_NO_THROW(parsePropertyValueAndSendEvent(
        propertyName, dbusPropertyValue, objectPath));
}

TEST_F(RfUtilityTest, SendRedfishEventDoesNotThrow)
{
    std::string propertyName = "TestProperty";
    std::string propertyValue = "TestValue";
    std::string objectPath = "/test/path";

    EXPECT_NO_THROW(sendRedfishEvent(propertyName, propertyValue, objectPath));
}

TEST_F(RfUtilityTest, SendRedfishEventWithEmptyStrings)
{
    std::string propertyName = "";
    std::string propertyValue = "";
    std::string objectPath = "";

    EXPECT_NO_THROW(sendRedfishEvent(propertyName, propertyValue, objectPath));
}

TEST_F(RfUtilityTest, ParsePropertyValueAndSendEventWithEmptyValue)
{
    std::string propertyName = "TestProperty";
    std::string dbusPropertyValue = "";
    std::string objectPath = "/test/path";

    EXPECT_NO_THROW(parsePropertyValueAndSendEvent(
        propertyName, dbusPropertyValue, objectPath));
}

TEST_F(RfUtilityTest, ParsePropertyValueAndSendEventWithDotAtEnd)
{
    std::string propertyName = "TestProperty";
    std::string dbusPropertyValue = "xyz.openbmc_project.Test.";
    std::string objectPath = "/test/path";

    EXPECT_NO_THROW(parsePropertyValueAndSendEvent(
        propertyName, dbusPropertyValue, objectPath));
}

} // namespace bios_config::test
