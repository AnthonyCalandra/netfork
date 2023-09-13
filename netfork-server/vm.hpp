/**
 * netfork
 * Copyright (C) 2023 Anthony Calandra
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include <netfork-shared/log.hpp>
#include <netfork-shared/net/msg.hpp>
#include <netfork-shared/net/sock.hpp>
#include <netfork-shared/phnt_stub.hpp>
#include <netfork-shared/utils.hpp>

namespace netfork::vm
{
    constexpr const std::size_t REGION_BUFFER_SIZE = 4096;

    BOOL rebuild_forked_process(HANDLE forked_process_handle, SOCKET client_sock)
    {
        while (true)
        {
            net::msg::region_info region_info;

            {
                auto region_info_msg = net::recv_as<net::msg::region_info>(client_sock);
                if (!region_info_msg)
                {
                    break;
                }

                region_info = std::move(region_info_msg).value();
            }

            LOG_DEBUG() << "Received: Region\n" << region_info << std::endl;

            DWORD region_allocation_protect = region_info.protect;
            if (region_allocation_protect & PAGE_EXECUTE_WRITECOPY)
            {
                region_allocation_protect &= ~PAGE_EXECUTE_WRITECOPY;
                region_allocation_protect |= PAGE_EXECUTE_READWRITE;
            }
            if (region_allocation_protect & PAGE_WRITECOPY)
            {
                region_allocation_protect &= ~PAGE_WRITECOPY;
                region_allocation_protect |= PAGE_READWRITE;
            }

            PVOID region_ptr = ::VirtualAlloc2(
                forked_process_handle,
                region_info.base_address,
                region_info.allocation_size,
                MEM_RESERVE,
                region_allocation_protect,
                nullptr,
                0
            );
            if (!region_ptr)
            {
                LOG_DEBUG_ERR() << "Failed to allocate reserved memory at 0x"
                    << std::hex << region_info.base_address << std::dec
                    << " GetLastError: " << ::GetLastError() << std::endl;
            }

            for (SIZE_T subregion_idx = 0; subregion_idx < region_info.subregion_info_size; subregion_idx++)
            {
                net::msg::subregion_info subregion_info;

                {
                    auto subregion_info_msg = net::recv_as<net::msg::subregion_info>(client_sock);
                    if (!subregion_info_msg)
                    {
                        LOG_DEBUG_ERR() << "Fatal error when rebuilding virtual memory: "
                            << subregion_info_msg.error() << std::endl;
                        return FALSE;
                    }

                    subregion_info = std::move(subregion_info_msg).value();
                }


                LOG_DEBUG() << "Received: Subregion\n" << subregion_info << std::endl;

                // Likely a reserved block.
                // It's safe to skip the rest of this block since the client isn't sending
                // the region over.
                if (subregion_info.protect == 0)
                {
                    continue;
                }

                DWORD block_allocation_protect = subregion_info.protect;
                if (block_allocation_protect & PAGE_EXECUTE_WRITECOPY)
                {
                    block_allocation_protect &= ~PAGE_EXECUTE_WRITECOPY;
                    block_allocation_protect |= PAGE_EXECUTE_READWRITE;
                }
                if (block_allocation_protect & PAGE_WRITECOPY)
                {
                    block_allocation_protect &= ~PAGE_WRITECOPY;
                    block_allocation_protect |= PAGE_READWRITE;
                }

                region_ptr = ::VirtualAlloc2(
                    forked_process_handle,
                    subregion_info.base_address,
                    subregion_info.region_size,
                    MEM_COMMIT,
                    PAGE_READWRITE,
                    nullptr,
                    0
                );
                if (!region_ptr)
                {
                    LOG_DEBUG_ERR() << "Failed to allocate reserved memory at 0x"
                        << std::hex << subregion_info.base_address << std::dec
                        << " GetLastError: " << ::GetLastError() << std::endl;
                }

                if (subregion_info.protect & PAGE_GUARD)
                {
                    [[maybe_unused]] DWORD old_protect; // required for VirtualProtectEx
                    if (!::VirtualProtectEx(
                        forked_process_handle,
                        subregion_info.base_address,
                        subregion_info.region_size,
                        block_allocation_protect,
                        &old_protect))
                    {
                        LOG_DEBUG_ERR() << "Failed to change memory protection to: 0x"
                            << std::hex << block_allocation_protect << std::dec
                            << " GetLastError: " << ::GetLastError() << std::endl;
                    }

                    // It's safe to skip the rest of this block since the client isn't
                    // sending the region over.
                    continue;
                }

                {
                    std::size_t remaining_region_size = subregion_info.region_size;
                    std::uintptr_t offset = 0;
                    std::vector<std::byte> buffer(REGION_BUFFER_SIZE);

                    while (remaining_region_size > 0)
                    {
                        const auto bytes_to_read = std::min(
                            REGION_BUFFER_SIZE,
                            remaining_region_size
                        );

                        if (FAILED(net::recv_bytes(client_sock, std::span{ buffer.data(), bytes_to_read })))
                        {
                            LOG_DEBUG_ERR() << "Failed to receive full region." << std::endl;
                        }

                        const auto target_address = reinterpret_cast<LPVOID>(
                            reinterpret_cast<std::uintptr_t>(subregion_info.base_address) + offset);
                        SIZE_T bytes_written = 0;
                        const BOOL write_successful = ::WriteProcessMemory(
                            forked_process_handle,
                            target_address,
                            buffer.data(),
                            bytes_to_read,
                            &bytes_written
                        );
                        if (!write_successful || bytes_written != bytes_to_read)
                        {
                            LOG_DEBUG_ERR() << "Failed to write memory at 0x"
                                << std::hex << target_address << std::dec
                                << " GetLastError: " << ::GetLastError() << std::endl;
                        }

                        LOG_DEBUG() << "Received 0x"
                            << std::hex << subregion_info.region_size << std::dec
                            << " bytes of region; written 0x"
                            << std::hex << bytes_written << std::dec << std::endl;

                        remaining_region_size -= bytes_to_read;
                        offset += bytes_to_read;
                    }
                }

                [[maybe_unused]] DWORD old_protect; // required for VirtualProtectEx
                if (!::VirtualProtectEx(
                    forked_process_handle,
                    subregion_info.base_address,
                    subregion_info.region_size,
                    block_allocation_protect,
                    &old_protect))
                {
                    LOG_DEBUG_ERR() << "Failed to change memory protection to: 0x"
                        << std::hex << block_allocation_protect << std::dec
                        << " GetLastError: " << ::GetLastError() << std::endl;
                }
            }
        }

        return TRUE;
    }
}
