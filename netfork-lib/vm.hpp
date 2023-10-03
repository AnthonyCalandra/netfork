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

#include <expected>
#include <vector>
#include <span>

#include <netfork-shared/auto.hpp>
#include <netfork-shared/log.hpp>
#include <netfork-shared/net/msg.hpp>
#include <netfork-shared/phnt_stub.hpp>
#include <netfork-shared/utils.hpp>

namespace netfork::vm
{
    template <typename QueryPredicate>
    generator<net::msg::message_type> query_virtual_memory_if(QueryPredicate pred)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        ULONG_PTR address = 0;
        while (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)))
        {
            address += mbi.RegionSize;

            if (mbi.State == MEM_FREE) continue;
            if (!pred(mbi)) continue;

            net::msg::region_info region_info{
                .base_address = mbi.AllocationBase,
                .protect = mbi.AllocationProtect,
                .allocation_size = 0,
                .subregion_info_size = 0
            };
            std::vector<net::msg::subregion_info> subregions;
            subregions.emplace_back(mbi.BaseAddress, mbi.RegionSize, mbi.Protect);
            region_info.allocation_size += mbi.RegionSize;

            PVOID current_allocation_base = mbi.AllocationBase;
            while (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)))
            {
                if (current_allocation_base != mbi.AllocationBase)
                {
                    break;
                }

                subregions.emplace_back(mbi.BaseAddress, mbi.RegionSize, mbi.Protect);
                region_info.allocation_size += mbi.RegionSize;
                current_allocation_base = mbi.AllocationBase;
                address += mbi.RegionSize;
            }

            region_info.subregion_info_size = subregions.size();

            LOG_DEBUG() << "Region\n" << region_info << std::endl;

            co_yield region_info;

            for (const auto& subregion : subregions)
            {
                LOG_DEBUG() << "Subregion\n" << subregion << std::endl;

                co_yield subregion;

                if (subregion.protect == 0 ||
                    subregion.protect & (PAGE_NOACCESS | PAGE_GUARD))
                {
                    continue;
                }

                [[maybe_unused]] DWORD old_protect;
                if (!::VirtualProtectEx(
                    ::GetCurrentProcess(),
                    subregion.base_address,
                    subregion.region_size,
                    PAGE_EXECUTE_READWRITE,
                    &old_protect))
                {
                    LOG_DEBUG_ERR() << "Failed to change memory protection to allow RWX at: 0x"
                        << std::hex << subregion.base_address << std::dec
                        << " GetLastError: " << ::GetLastError() << std::endl;
                }
                AT_SCOPE_EXIT(::VirtualProtectEx(
                    ::GetCurrentProcess(),
                    subregion.base_address,
                    subregion.region_size,
                    subregion.protect,
                    &old_protect
                ));

                co_yield std::span<char>{
                    reinterpret_cast<char*>(subregion.base_address),
                    subregion.region_size
                };
            }
        }

        co_return;
    }
}
