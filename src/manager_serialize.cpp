#include "manager_serialize.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/stream_file.hpp>
#include <boost/asio/write.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>
#include <phosphor-logging/lg2.hpp>

#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace bios_config
{

namespace
{

struct AsyncSerializeState
{
    bool writeInProgress = false;
    std::optional<std::string> pendingBuffer;
};

using AsyncSerializeStateMap =
    std::map<std::string, std::weak_ptr<AsyncSerializeState>>;

AsyncSerializeStateMap& asyncSerializeStates()
{
    static AsyncSerializeStateMap states;
    return states;
}

std::string asyncSerializeKey(const fs::path& path)
{
    return path.lexically_normal().string();
}

fs::path asyncSerializeTempPath(const fs::path& path)
{
    fs::path tempPath = path;
    tempPath += ".tmp";
    return tempPath;
}

void startAsyncSerializeWrite(boost::asio::any_io_executor executor,
                              const fs::path& path, const std::string& key,
                              std::shared_ptr<AsyncSerializeState> state,
                              std::string buffer);

void finishAsyncSerializeWrite(boost::asio::any_io_executor executor,
                               const fs::path& path, const std::string& key,
                               std::shared_ptr<AsyncSerializeState> state)
{
    if (state->pendingBuffer)
    {
        auto nextBuffer = std::move(*state->pendingBuffer);
        state->pendingBuffer.reset();
        startAsyncSerializeWrite(executor, path, key, std::move(state),
                                 std::move(nextBuffer));
        return;
    }

    state->writeInProgress = false;
    asyncSerializeStates().erase(key);
}

void startAsyncSerializeWrite(boost::asio::any_io_executor executor,
                              const fs::path& path, const std::string& key,
                              std::shared_ptr<AsyncSerializeState> state,
                              std::string buffer)
{
    const auto tempPath = asyncSerializeTempPath(path);
    std::shared_ptr<boost::asio::stream_file> file;
    try
    {
        file = std::make_shared<boost::asio::stream_file>(executor);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed to create async serialize file backend: {FILE} {ERR}",
            "FILE", tempPath, "ERR", e.what());
        finishAsyncSerializeWrite(executor, path, key, std::move(state));
        return;
    }

    boost::system::error_code ec;
    file->open(tempPath.string(),
               boost::asio::stream_file::write_only |
                   boost::asio::stream_file::create |
                   boost::asio::stream_file::truncate,
               ec);
    if (ec)
    {
        lg2::error(
            "Failed to open temp file for async serialization: {FILE} {ERR}",
            "FILE", tempPath, "ERR", ec.message());
        finishAsyncSerializeWrite(executor, path, key, std::move(state));
        return;
    }

    auto buf = std::make_shared<std::string>(std::move(buffer));

    boost::asio::async_write(
        *file, boost::asio::buffer(*buf),
        [executor, file, buf, path, tempPath, key, state = std::move(state)](
            boost::system::error_code writeEc, std::size_t) {
            if (writeEc)
            {
                lg2::error("Async serialize write failed: {FILE} {ERR}", "FILE",
                           tempPath, "ERR", writeEc.message());
                boost::system::error_code closeEc;
                file->close(closeEc);
                std::error_code removeEc;
                fs::remove(tempPath, removeEc);
                finishAsyncSerializeWrite(executor, path, key,
                                          std::move(state));
                return;
            }

            boost::system::error_code closeEc;
            file->close(closeEc);
            // OS-level close failures are not deterministic in UT.
            if (closeEc) // GCOVR_EXCL_BR_LINE
            {
                lg2::error("Async serialize close failed: {FILE} {ERR}", "FILE",
                           tempPath, "ERR", closeEc.message());
                std::error_code removeEc;
                fs::remove(tempPath, removeEc);
                finishAsyncSerializeWrite(executor, path, key,
                                          std::move(state));
                return;
            }

            if (state->pendingBuffer)
            {
                std::error_code removeEc;
                fs::remove(tempPath, removeEc);
                finishAsyncSerializeWrite(executor, path, key,
                                          std::move(state));
                return;
            }

            std::error_code renameEc;
            fs::rename(tempPath, path, renameEc);
            if (renameEc)
            {
                lg2::error(
                    "Async serialize rename failed: {TEMP_FILE} -> {FILE} {ERR}",
                    "TEMP_FILE", tempPath, "FILE", path, "ERR",
                    renameEc.message());
                std::error_code removeEc;
                fs::remove(tempPath, removeEc);
                finishAsyncSerializeWrite(executor, path, key,
                                          std::move(state));
                return;
            }

            finishAsyncSerializeWrite(executor, path, key, std::move(state));
        });
}

} // namespace

// BIOS_CONFIG_VERSION is introduced to manage backward compatibility with
// old BaseTableV1 where had not added the support version flag in the archived
// data itself. To manage this the deserialize will try to decode with
// BaseTableV1 when there is exception to read the version itself. If the
// version in the archive is been read correctly then next version checks will
// be handled.
//  BaseTable - Maps to Version 2
//  BaseTableV1 - Maps to version 1

static std::uint32_t currentVersion = BIOS_CONFIG_VERSION;

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
    std::uint32_t version = BIOS_CONFIG_VERSION;
    archive(version);
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
                baseBIOSTable(),
            entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
                pendingAttributes(),
            entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
                enableAfterReset(),
            entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
                credentialBootstrap());
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                BootOrder::bootOrder());
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                BootOrder::pendingBootOrder());
    archive(entry.getBootOptionValues());
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                SecureBoot::currentBoot());
    archive(entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::
                SecureBoot::pendingEnable());
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
    Manager::oldBaseTable baseTableV1;

    Manager::PendingAttributes pendingAttrs;
    bool enableAfterResetFlag;
    bool credentialBootstrapFlag;

    lg2::info("Load Bios Config Version: {VERSION}", "VERSION", currentVersion);
    if (currentVersion == BIOS_CONFIG_VERSION)
    {
        archive(currentVersion);
        archive(baseTable, pendingAttrs, enableAfterResetFlag,
                credentialBootstrapFlag);
    }
    else if (currentVersion == BIOS_CONFIG_VERSION_2)
    {
        archive(currentVersion);
        archive(baseTable, pendingAttrs, enableAfterResetFlag);
        credentialBootstrapFlag = true;
    }
    else
    {
        archive(baseTableV1, pendingAttrs, enableAfterResetFlag);
        entry.convertBiosDataToVersion1(baseTableV1, baseTable);
        credentialBootstrapFlag = true;
    }

    auto appliedBaseTable = entry.sdbusplus::xyz::openbmc_project::BIOSConfig::
                                server::Manager::baseBIOSTable(baseTable, true);
    entry.cacheAllAttributes(appliedBaseTable);

    entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
        pendingAttributes(pendingAttrs, true);
    entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
        enableAfterReset(enableAfterResetFlag, true);
    entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::Manager::
        credentialBootstrap(credentialBootstrapFlag, true);

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
        pendingEnable(enableValue, true);

    Manager::ModeType modeValue;
    archive(modeValue);
    entry.sdbusplus::xyz::openbmc_project::BIOSConfig::server::SecureBoot::mode(
        modeValue, true);
}

bool serializeToBuffer(const Manager& obj, std::string& out)
{
    try
    {
        std::ostringstream os(std::ios::binary);
        cereal::BinaryOutputArchive oarchive(os);
        oarchive(obj);
        out = os.str();
        return true;
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to serialize to buffer: {ERROR}", "ERROR", e);
        return false;
    }
}

void serialize(const Manager& obj, const fs::path& path)
{
    try
    {
        std::ofstream os(path, std::ios::out | std::ios::binary);

        if (!os.is_open())
        {
            lg2::error("Failed to open file for serialization: {FILE}", "FILE",
                       path);
            return;
        }

        cereal::BinaryOutputArchive oarchive(os);
        oarchive(obj);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to Serialize : {ERROR} ", "ERROR", e);
    }
}

void asyncSerialize(boost::asio::io_context& io, const Manager& obj,
                    const fs::path& path)
{
    std::string buffer;
    if (!serializeToBuffer(obj, buffer))
    {
        return;
    }

    const auto key = asyncSerializeKey(path);
    auto& states = asyncSerializeStates();
    auto state = states[key].lock();
    if (!state)
    {
        state = std::make_shared<AsyncSerializeState>();
        states[key] = state;
    }

    if (state->writeInProgress)
    {
        state->pendingBuffer = std::move(buffer);
        return;
    }

    state->writeInProgress = true;
    startAsyncSerializeWrite(io.get_executor(), path, key, state,
                             std::move(buffer));
}

bool deserialize(const fs::path& path, Manager& entry)
{
    currentVersion = BIOS_CONFIG_VERSION;
    try
    {
        if (fs::exists(path))
        {
            try
            {
                std::ifstream is(path.c_str(), std::ios::in | std::ios::binary);
                if (!is.is_open())
                {
                    lg2::error(
                        "Failed to open file for deserialization: {FILE}",
                        "FILE", path);
                    return false;
                }
                cereal::BinaryInputArchive iarchive(is);
                iarchive(entry);
            }
            catch (...)
            {
                try
                {
                    lg2::error("Trying with old Bios Config Version: {VERSION}",
                               "VERSION", 2);
                    std::ifstream is(path.c_str(),
                                     std::ios::in | std::ios::binary);
                    cereal::BinaryInputArchive iarchive(is);
                    currentVersion = BIOS_CONFIG_VERSION_2;
                    iarchive(entry);
                }
                catch (...)
                {
                    lg2::error("Trying with old Bios Config Version: {VERSION}",
                               "VERSION", 1);
                    std::ifstream is(path.c_str(),
                                     std::ios::in | std::ios::binary);
                    cereal::BinaryInputArchive iarchive(is);
                    currentVersion = BIOS_CONFIG_VERSION_1;
                    iarchive(entry);
                }
            }
            return true;
        }
        return false;
    }
    catch (cereal::Exception& e)
    {
        lg2::error("Cereal failed to serialize: {ERROR}", "ERROR", e);
        fs::remove(path);
        return false;
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to serialize: {ERROR}", "ERROR", e);
        fs::remove(path);
        return false;
    }
}

} // namespace bios_config
