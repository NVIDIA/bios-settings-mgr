#include "manager_serialize.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>
#include <phosphor-logging/lg2.hpp>

#include <fstream>

namespace bios_config
{

/** @brief Function required by Cereal to perform serialization.
 *
 *  @tparam Archive - Cereal archive type (binary in this case).
 *  @param[in] archive - reference to cereal archive.
 *  @param[in] entry- const reference to bios manager object
 *  @param[in] version - Class version that enables handling a serialized data
 *                       across code levels
 */
template <class Archive>
void save(Archive& archive, const Manager& entry,
          const std::uint32_t /*version*/)
{
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
                baseBIOSTable(),
            entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
                pendingAttributes(),
            entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
                enableAfterReset());
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                BootOrder::bootOrder());
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                BootOrder::pendingBootOrder());
    archive(entry.getBootOptionValues());
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                SecureBoot::currentBoot());
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                SecureBoot::enable());
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                SecureBoot::mode());
}

/** @brief Function required by Cereal to perform deserialization.
 *
 *  @tparam Archive - Cereal archive type (binary in our case).
 *  @param[in] archive - reference to cereal archive.
 *  @param[out] entry - reference to bios manager object
 *  @param[in] version - Class version that enables handling a serialized data
 *                       across code levels
 */
template <class Archive>
void load(Archive& archive, Manager& entry, const std::uint32_t /*version*/)
{
    Manager::BaseTable baseTable;
    Manager::PendingAttributes pendingAttrs;
    bool enableAfterResetFlag;

    archive(baseTable, pendingAttrs, enableAfterResetFlag);
    entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
        baseBIOSTable(baseTable, true);
    entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
        pendingAttributes(pendingAttrs, true);
    entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
        enableAfterReset(enableAfterResetFlag, true);

    try
    {
        Manager::BootOrderType bootOrderValue;
        archive(bootOrderValue);
        entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::BootOrder::
            bootOrder(bootOrderValue, true);

        Manager::BootOrderType pendingBootOrderValue;
        archive(pendingBootOrderValue);
        entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::BootOrder::
            pendingBootOrder(pendingBootOrderValue, true);

        Manager::BootOptionsType bootOptionsValues;
        archive(bootOptionsValues);
        entry.setBootOptionValues(bootOptionsValues);

        Manager::CurrentBootType currentBootValue;
        archive(currentBootValue);
        entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::SecureBoot::
            currentBoot(currentBootValue, true);

        bool enableValue;
        archive(enableValue);
        entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::SecureBoot::
            enable(enableValue, true);

        Manager::ModeType modeValue;
        archive(modeValue);
        entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::SecureBoot::
            mode(modeValue, true);
    }
    catch (cereal::Exception& e)
    {
        // Cannot read these properties, it could be different version
        lg2::error("Failed to load: {ERROR}", "ERROR", e);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to load: {ERROR}", "ERROR", e);
    }
}

void serialize(const Manager& obj, const fs::path& path)
{
    std::ofstream os(path.c_str(), std::ios::out | std::ios::binary);
    cereal::BinaryOutputArchive oarchive(os);
    oarchive(obj);
}

bool deserialize(const fs::path& path, Manager& entry)
{
    try
    {
        if (fs::exists(path))
        {
            std::ifstream is(path.c_str(), std::ios::in | std::ios::binary);
            cereal::BinaryInputArchive iarchive(is);
            iarchive(entry);
            return true;
        }
        return false;
    }
    catch (cereal::Exception& e)
    {
        lg2::error("Failed to serialize: {ERROR}", "ERROR", e);
        fs::remove(path);
        return false;
    }
    catch (const std::length_error& e)
    {
        lg2::error("Failed to serialize: {ERROR}", "ERROR", e);
        fs::remove(path);
        return false;
    }
}

} // namespace bios_config
