#include "boot_option.hpp"

#include "manager.hpp"
#include "manager_serialize.hpp"

namespace bios_config
{

BootOptionDbus::BootOptionDbus(sdbusplus::bus_t& bus, const char* path,
                               Manager& parent, const std::string key) :
    BootOptionDbusBase(bus, path),
    parent(parent), key(key)
{}

bool BootOptionDbus::enabled(bool value)
{
    auto enabled = BootOptionDbusBase::enabled(value, false);
    parent.bootOptionValues[key]["Enabled"] = enabled;
    auto pendingEnabled = BootOptionDbusBase::pendingEnabled(value, false);
    parent.bootOptionValues[key]["PendingEnabled"] = pendingEnabled;
    serialize(parent, parent.biosFile);
    return enabled;
}

bool BootOptionDbus::pendingEnabled(bool value)
{
    auto v = BootOptionDbusBase::pendingEnabled(value, false);
    parent.bootOptionValues[key]["PendingEnabled"] = v;
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
} // namespace bios_config
