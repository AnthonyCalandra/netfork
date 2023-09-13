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

#include "image.hpp"

#include <netfork-shared/log.hpp>
#include <netfork-shared/phnt_stub.hpp>

namespace netfork::io::pe
{
    // Read the PE file in memory so the relevant offsets can be adjusted.
    BOOL modify_pe_image_for_execution(const map_view_ptr& view, const PEB& forked_peb)
    {
        PIMAGE_NT_HEADERS nt_headers = ::RtlImageNtHeader(view.get());
        if (!nt_headers)
        {
            return FALSE;
        }

        PIMAGE_OPTIONAL_HEADER optional_header = &nt_headers->OptionalHeader;
        // Set the image base address to reflect where it was in the forked process.
        optional_header->ImageBase = reinterpret_cast<ULONGLONG>(forked_peb.ImageBaseAddress);
        // Disable ASLR for this executable. This is necessary because the forked process could
        // map the executable to a base address that is different from the original process.
        optional_header->DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;

        // Get the section headers and patch the raw data sizes and offsets to reflect the virtual
        // sizes and offsets. When a PE is loaded from a file, the loader will copy the raw file
        // data into the virtual memory space based on the `PointerToRawData` and `SizeofOfRawData`
        // fields.
        //
        // When the current process was forked, the PE was mapped into memory at the virtual
        // offsets and sizes. In order to properly mirror the same image in virtual memory for
        // the forked process, it needs to be adjusted so that the virtual offsets and sizes are
        // the same as the raw fields.
        PIMAGE_SECTION_HEADER section_header = reinterpret_cast<PIMAGE_SECTION_HEADER>(
            reinterpret_cast<PBYTE>(nt_headers) + sizeof(IMAGE_NT_HEADERS));
        const WORD total_sections = nt_headers->FileHeader.NumberOfSections;
        for (WORD section_index = 0; section_index < total_sections; section_index++)
        {
            [[maybe_unused]] const DWORD old_pointer_to_raw_data = section_header->PointerToRawData;
            [[maybe_unused]] const DWORD old_size_of_raw_data = section_header->SizeOfRawData;

            section_header->PointerToRawData = section_header->VirtualAddress;
            section_header->SizeOfRawData = section_header->Misc.VirtualSize;

            LOG_DEBUG() << section_header->Name << " modified\n"
                << "\tPointerToRawData: " << std::hex << old_pointer_to_raw_data << " -> "
                << section_header->PointerToRawData << '\n'
                << "\tSizeOfRawData: " << std::hex << old_size_of_raw_data << " -> "
                << section_header->SizeOfRawData << std::endl;

            // Move to the next section header.
            section_header++;
        }

        return TRUE;
    }
}
