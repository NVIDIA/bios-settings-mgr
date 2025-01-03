
#pragma once

#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/BIOSConfig/BootOption/server.hpp>
#include <xyz/openbmc_project/Object/Delete/server.hpp>

#include <string>

namespace bios_config
{
using BootOptionDbusBase = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::BIOSConfig::server::BootOption,
    sdbusplus::xyz::openbmc_project::Object::server::Delete>;

class Manager;

class BootOptionDbus : public BootOptionDbusBase
{
  public:
    /** @brief Constructs BootOptionDbus object.
     *
     *  @param[in] bus - Bus to attach to.
     *  @param[in] path - Path to attach at.
     *  @param[in] parent - Reference of parent.
     *  @param[in] key - Key of this object.
     */
    BootOptionDbus(sdbusplus::bus_t& bus, const char* path, Manager& parent,
                   const std::string key);

    bool enabled(bool value) override;
    bool pendingEnabled(bool value) override;
    std::string description(std::string value) override;
    std::string displayName(std::string value) override;
    std::string uefiDevicePath(std::string value) override;

    void delete_() override;

  private:
    Manager& parent;
    const std::string key;
};
} // namespace bios_config
