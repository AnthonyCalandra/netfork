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

#include <ostream>
#include <span>
#include <variant>

#include <netfork-shared/phnt_stub.hpp>

namespace netfork::net::msg
{
	struct region_info
	{
		// Base address of the region.
		LPVOID base_address;
		// Memory protection flags. See: https://docs.microsoft.com/en-us/windows/win32/memory/memory-protection-constants
		DWORD protect;
		// Size of the region in bytes.
		SIZE_T allocation_size;
		// Number of subregions (`subregion_info` structures) in region.
		SIZE_T subregion_info_size;
	};

	std::ostream& operator<<(std::ostream& os, const region_info& r)
	{
		os << "Base Address: 0x" << std::hex << r.base_address << '\n';
		os << "Protect: 0x" << r.protect << '\n';
		os << "Allocation Size: 0x" << r.allocation_size << '\n';
		os << "Subregion Info Size: 0x" << r.subregion_info_size << std::dec;
		return os;
	}

	struct subregion_info
	{
		// Base address of the subregion. Every `base_address` is in range of
		// `region_info::base_address` and `region_info::base_address + region_info::allocation_size`.
		LPVOID base_address;
		// Subregion size in bytes.
		SIZE_T region_size;
		// Memory protection flags. See: https://docs.microsoft.com/en-us/windows/win32/memory/memory-protection-constants
		DWORD protect;
	};

	std::ostream& operator<<(std::ostream& os, const subregion_info& s)
	{
		os << "Base Address: 0x" << std::hex << s.base_address << '\n';
		os << "Region Size: 0x" << s.region_size << '\n';
		os << "Protect: 0x" << s.protect << std::dec;
		return os;
	}

	using message_type = std::variant<net::msg::region_info, net::msg::subregion_info, std::span<char>>;
}
