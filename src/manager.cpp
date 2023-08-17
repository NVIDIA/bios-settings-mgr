/*
// Copyright (c) 2020 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#include "manager.hpp"

#include "manager_serialize.hpp"
#include "xyz/openbmc_project/BIOSConfig/Common/error.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <boost/asio.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <regex>

namespace bios_config
{

using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace sdbusplus::xyz::openbmc_project::BIOSConfig::Common::Error;

BootOptionDbus::BootOptionDbus(sdbusplus::bus_t& bus, const char* path,
                               Manager& parent, const std::string key) :
    BootOptionDbusBase(bus, path),
    parent(parent), key(key)
{}

bool BootOptionDbus::enabled(bool value)
{
    auto v = BootOptionDbusBase::enabled(value, false);
    parent.bootOptionValues[key]["Enabled"] = v;
    serialize(parent, parent.biosFile);
    return v;
}
std::string BootOptionDbus::description(std::string value)
{
    auto v = BootOptionDbusBase::description(value, false);
    parent.bootOptionValues[key]["Description"] = v;
    serialize(parent, parent.biosFile);
    return v;
}
std::string BootOptionDbus::displayName(std::string value)
{
    auto v = BootOptionDbusBase::displayName(value, false);
    parent.bootOptionValues[key]["DisplayName"] = v;
    serialize(parent, parent.biosFile);
    return v;
}
std::string BootOptionDbus::uefiDevicePath(std::string value)
{
    auto v = BootOptionDbusBase::uefiDevicePath(value, false);
    parent.bootOptionValues[key]["UefiDevicePath"] = v;
    serialize(parent, parent.biosFile);
    return v;
}

void BootOptionDbus::delete_()
{
    parent.deleteBootOption(key);
}

void Manager::setAttribute(AttributeName attribute, AttributeValue value)
{
    auto pendingAttrs = Base::pendingAttributes();
    auto iter = pendingAttrs.find(attribute);

    if (iter != pendingAttrs.end())
    {
        std::get<1>(iter->second) = value;
    }
    else
    {
        Manager::PendingAttribute attributeValue;

        if (std::get_if<int64_t>(&value))
        {
            std::get<0>(attributeValue) = AttributeType::Integer;
        }
        else
        {
            std::get<0>(attributeValue) = AttributeType::String;
        }

        std::get<1>(attributeValue) = value;
        pendingAttrs.emplace(attribute, attributeValue);
    }

    pendingAttributes(pendingAttrs);
}

Manager::AttributeDetails Manager::getAttribute(AttributeName attribute)
{
    Manager::AttributeDetails value;

    auto table = Base::baseBIOSTable();
    auto iter = table.find(attribute);

    if (iter != table.end())
    {
        std::get<0>(value) =
            std::get<static_cast<uint8_t>(Index::attributeType)>(iter->second);
        std::get<1>(value) =
            std::get<static_cast<uint8_t>(Index::currentValue)>(iter->second);

        auto pending = Base::pendingAttributes();
        auto pendingIter = pending.find(attribute);
        if (pendingIter != pending.end())
        {
            std::get<2>(value) = std::get<1>(pendingIter->second);
        }
        else if (std::get_if<std::string>(&std::get<1>(value)))
        {
            std::get<2>(value) = std::string();
        }
    }
    else
    {
        throw AttributeNotFound();
    }

    return value;
}

Manager::BaseTable Manager::baseBIOSTable(BaseTable value)
{
    pendingAttributes({});
    auto baseTable = Base::baseBIOSTable(value, false);
    serialize(*this, biosFile);
    Base::resetBIOSSettings(Base::ResetFlag::NoAction);
    return baseTable;
}

bool Manager::enableAfterReset(bool value)
{
    auto enableAfterResetFlag = Base::enableAfterReset(value, false);
    serialize(*this, biosFile);
    return enableAfterResetFlag;
}

bool Manager::validateEnumOption(
    const std::string& attrValue,
    const std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>>>& options)
{
    for (const auto& enumOptions : options)
    {
        if ((BoundType::OneOf == std::get<0>(enumOptions)) &&
            (attrValue == std::get<std::string>(std::get<1>(enumOptions))))
        {
            return true;
        }
    }

    lg2::error("No valid attribute");
    return false;
}

bool Manager::validateStringOption(
    const std::string& attrValue,
    const std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>>>& options)
{
    size_t minStringLength = 0;
    size_t maxStringLength = 0;
    for (const auto& stringOptions : options)
    {
        if (BoundType::MinStringLength == std::get<0>(stringOptions))
        {
            minStringLength = std::get<int64_t>(std::get<1>(stringOptions));
        }
        else if (BoundType::MaxStringLength == std::get<0>(stringOptions))
        {
            maxStringLength = std::get<int64_t>(std::get<1>(stringOptions));
        }
        else
        {
            continue;
        }
    }

    if (attrValue.length() < minStringLength ||
        attrValue.length() > maxStringLength)
    {
        lg2::error(
            "{ATTRVALUE} Length is out of range, bound is invalid, maxStringLength = {MAXLEN}, minStringLength = {MINLEN}",
            "ATTRVALUE", attrValue, "MAXLEN", maxStringLength, "MINLEN",
            minStringLength);
        return false;
    }

    return true;
}

bool Manager::validateIntegerOption(
    const int64_t& attrValue,
    const std::vector<
        std::tuple<BoundType, std::variant<int64_t, std::string>>>& options)
{
    int64_t lowerBound = 0;
    int64_t upperBound = 0;
    int64_t scalarIncrement = 0;

    for (const auto& integerOptions : options)
    {
        if (BoundType::LowerBound == std::get<0>(integerOptions))
        {
            lowerBound = std::get<int64_t>(std::get<1>(integerOptions));
        }
        else if (BoundType::UpperBound == std::get<0>(integerOptions))
        {
            upperBound = std::get<int64_t>(std::get<1>(integerOptions));
        }
        else if (BoundType::ScalarIncrement == std::get<0>(integerOptions))
        {
            scalarIncrement = std::get<int64_t>(std::get<1>(integerOptions));
        }
    }

    if ((attrValue < lowerBound) || (attrValue > upperBound))
    {
        lg2::error("Integer, bound is invalid");
        return false;
    }

    if (scalarIncrement == 0 ||
        ((std::abs(attrValue - lowerBound)) % scalarIncrement) != 0)
    {
        lg2::error(
            "((std::abs({ATTR_VALUE} - {LOWER_BOUND})) % {SCALAR_INCREMENT}) != 0",
            "ATTR_VALUE", attrValue, "LOWER_BOUND", lowerBound,
            "SCALAR_INCREMENT", scalarIncrement);
        return false;
    }

    return true;
}

Manager::PendingAttributes Manager::pendingAttributes(PendingAttributes value)
{
    // Clear the pending attributes
    if (value.empty())
    {
        auto pendingAttrs = Base::pendingAttributes({}, false);
        serialize(*this, biosFile);
        return pendingAttrs;
    }

    // Validate all the BIOS attributes before setting PendingAttributes
    BaseTable biosTable = Base::baseBIOSTable();
    for (const auto& pair : value)
    {
        auto iter = biosTable.find(pair.first);
        // BIOS attribute not found in the BaseBIOSTable
        if (iter == biosTable.end())
        {
            lg2::error("BIOS attribute not found in the BaseBIOSTable");
            throw AttributeNotFound();
        }

        auto attributeType =
            std::get<static_cast<uint8_t>(Index::attributeType)>(iter->second);
        if (attributeType != std::get<0>(pair.second))
        {
            lg2::error("attributeType is not same with bios base table");
            throw InvalidArgument();
        }

        // Validate enumeration BIOS attributes
        if (attributeType == AttributeType::Enumeration)
        {
            // For enumeration the expected variant types is Enumeration
            if (std::get<1>(pair.second).index() == 0)
            {
                lg2::error("Enumeration property value is not enum");
                throw InvalidArgument();
            }

            const auto& attrValue =
                std::get<std::string>(std::get<1>(pair.second));
            const auto& options =
                std::get<static_cast<uint8_t>(Index::options)>(iter->second);

            if (!validateEnumOption(attrValue, options))
            {
                throw InvalidArgument();
            }
        }

        if (attributeType == AttributeType::String)
        {
            // For enumeration the expected variant types is std::string
            if (std::get<1>(pair.second).index() == 0)
            {
                lg2::error("String property value is not string");
                throw InvalidArgument();
            }

            const auto& attrValue =
                std::get<std::string>(std::get<1>(pair.second));
            const auto& options =
                std::get<static_cast<uint8_t>(Index::options)>(iter->second);

            if (!validateStringOption(attrValue, options))
            {
                throw InvalidArgument();
            }
        }

        if (attributeType == AttributeType::Integer)
        {
            // For enumeration the expected variant types is Integer
            if (std::get<1>(pair.second).index() == 1)
            {
                lg2::error("Enumeration property value is not int");
                throw InvalidArgument();
            }

            const auto& attrValue = std::get<int64_t>(std::get<1>(pair.second));
            const auto& options =
                std::get<static_cast<uint8_t>(Index::options)>(iter->second);

            if (!validateIntegerOption(attrValue, options))
            {
                throw InvalidArgument();
            }
        }
    }

    PendingAttributes pendingAttribute = Base::pendingAttributes();

    for (const auto& pair : value)
    {
        auto iter = pendingAttribute.find(pair.first);
        if (iter != pendingAttribute.end())
        {
            iter = pendingAttribute.erase(iter);
        }

        pendingAttribute.emplace(std::make_pair(pair.first, pair.second));
    }

    auto pendingAttrs = Base::pendingAttributes(pendingAttribute, false);
    serialize(*this, biosFile);

    return pendingAttrs;
}

void Manager::createBootOption(std::string id)
{
    const std::regex illegalDbusRegex("[^A-Za-z0-9_]");
    const std::string key = std::regex_replace(id, illegalDbusRegex, "_");
    if (bootOptionValues.contains(key))
    {
        throw InvalidArgument();
    }

    std::string path = std::string(bootOptionsPath) + "/" + key;
    bootOptionValues[key] = {{"Enabled", true},
                             {"Description", ""},
                             {"DisplayName", ""},
                             {"UefiDevicePath", ""}};
    dbusBootOptions[key] =
        std::make_unique<BootOptionDbus>(*systemBus, path.c_str(), *this, key);
    for (const auto& v : bootOptionValues[key])
    {
        dbusBootOptions[key]->BootOptionDbusBase::setPropertyByName(v.first,
                                                                    v.second);
    }

    serialize(*this, biosFile);
}

void Manager::deleteBootOption(const std::string& key)
{
    bootOptionValues.erase(key);
    dbusBootOptions.erase(key);

    serialize(*this, biosFile);
}

Manager::BootOptionsType Manager::getBootOptionValues() const
{
    return bootOptionValues;
}

void Manager::setBootOptionValues(const BootOptionsType& loaded)
{
    bootOptionValues = loaded;
    dbusBootOptions.clear();
    for (const auto& [key, values] : bootOptionValues)
    {
        std::string path = std::string(bootOptionsPath) + "/" + key;
        dbusBootOptions[key] = std::make_unique<BootOptionDbus>(
            *systemBus, path.c_str(), *this, key);
        for (const auto& v : values)
        {
            dbusBootOptions[key]->BootOptionDbusBase::setPropertyByName(
                v.first, v.second);
        }
    }
}

Manager::BootOrderType Manager::bootOrder(Manager::BootOrderType value)
{
    auto newValue = Base::bootOrder(value, false);
#ifdef CLEAR_PENDING_BOOTORDER_ON_UPDATE
    Manager::pendingBootOrder(std::vector<std::string>());
#else
    Manager::pendingBootOrder(value);
#endif
    return newValue;
}

Manager::BootOrderType Manager::pendingBootOrder(Manager::BootOrderType value)
{
    auto newValue = Base::pendingBootOrder(value, false);
    serialize(*this, biosFile);
    return newValue;
}

Manager::CurrentBootType Manager::currentBoot(Manager::CurrentBootType value)
{
    auto newValue = Base::currentBoot(value, false);
    serialize(*this, biosFile);
    return newValue;
}

bool Manager::enable(bool value)
{
    auto newValue = Base::enable(value, false);
    serialize(*this, biosFile);
    return newValue;
}

Manager::ModeType Manager::mode(Manager::ModeType value)
{
    auto newValue = Base::mode(value, false);
    serialize(*this, biosFile);
    return newValue;
}

Manager::Manager(sdbusplus::asio::object_server& objectServer,
                 std::shared_ptr<sdbusplus::asio::connection>& systemBus) :
    bios_config::Base(*systemBus, objectPath),
    objServer(objectServer), systemBus(systemBus)
{
    fs::path biosDir(BIOS_PERSIST_PATH);
    fs::create_directories(biosDir);
    biosFile = biosDir / biosPersistFile;
    deserialize(biosFile, *this);
}

} // namespace bios_config
