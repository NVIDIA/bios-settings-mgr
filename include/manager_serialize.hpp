#pragma once

#include "boot_option.hpp"
#include "manager.hpp"

#include <boost/asio/io_context.hpp>

#include <filesystem>

namespace bios_config
{

/** @brief Serialize and persist the bios manager object
 *
 *  @param[in] obj - bios manager object
 *  @param[in] path - path to the file where the bios manager object
 *                    is to be serialized
 */
void serialize(const Manager& obj, const fs::path& path);

/** @brief Serialize to memory buffer (non-blocking, fast)
 *
 *  @param[in] obj - bios manager object
 *  @param[out] out - serialized binary data
 *  @return true on success, false on error
 */
bool serializeToBuffer(const Manager& obj, std::string& out);

/** @brief Serialize and persist asynchronously via io_uring (no threads)
 *
 *  Serializes to buffer in-memory, then async-writes to file.
 *  Does not block the event loop.
 *
 *  @param[in] io - io_context (e.g. from systemBus->get_io_context())
 *  @param[in] obj - bios manager object
 *  @param[in] path - path to the file
 */
void asyncSerialize(boost::asio::io_context& io, const Manager& obj,
                    const fs::path& path);

/** @brief Deserialize the persisted data and populate the bios manager object
 *
 *  @param[in] path - path to the persisted file
 *  @param[in/out] entry - reference to the bios manager object which is the
 *                         target of deserialization.
 *
 *  @return bool - true if the deserialization was successful, false otherwise.
 */
bool deserialize(const fs::path& path, Manager& entry);

} // namespace bios_config
