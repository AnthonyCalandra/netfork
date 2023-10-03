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

#include <netfork-shared/auto.hpp>
#include <netfork-shared/phnt_stub.hpp>
#include <netfork-shared/utils.hpp>

namespace netfork::io
{
    std::expected<unique_nt_handle<default_nt_handle_deleter>, NTSTATUS> create_temporary_image(
        const DWORD image_size_in_bytes,
        UNICODE_STRING& image_path)
    {
        unique_nt_handle image_handle{};

        OBJECT_ATTRIBUTES obj_attr;
        InitializeObjectAttributes(&obj_attr, &image_path, 0, nullptr, nullptr);

        IO_STATUS_BLOCK isb;

        LARGE_INTEGER file_size;
        file_size.LowPart = image_size_in_bytes;
        file_size.HighPart = 0;

        NTSTATUS status = ::NtCreateFile(
            &image_handle.get(),
            DELETE | FILE_GENERIC_READ | FILE_GENERIC_WRITE,
            &obj_attr,
            &isb,
            &file_size,
            FILE_ATTRIBUTE_TEMPORARY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_OVERWRITE_IF,
            FILE_SYNCHRONOUS_IO_NONALERT | FILE_DELETE_ON_CLOSE,
            nullptr,
            0
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        // Mark the temporary file for deletion.
        FILE_DISPOSITION_INFORMATION disposition;
        disposition.DeleteFile = true;
        status = ::NtSetInformationFile(
            image_handle.get(),
            &isb,
            &disposition,
            sizeof(disposition),
            ::FileDispositionInformation
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        // Give the file a size so that a file mapping view can be created.
        status = ::NtSetInformationFile(
            image_handle.get(),
            &isb,
            &file_size,
            sizeof(file_size),
            ::FileEndOfFileInformation
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        return image_handle;
    }

    struct image_view
    {
        unique_handle<default_handle_deleter> mapping_handle;
        map_view_ptr view;
    };

    std::expected<image_view, HRESULT> create_image_view(
        HANDLE image_file,
        const DWORD view_size)
    {
        unique_handle mapping_handle{ ::CreateFileMappingW(
            image_file,
            nullptr,
            PAGE_READWRITE,
            0, // high-order DWORD of the file size (0 for mapping the entire file)
            0, // low-order DWORD of the file size (0 for mapping the entire file)
            nullptr
        ) };
        if (!mapping_handle)
        {
            return std::unexpected{ HRESULT_FROM_WIN32(::GetLastError()) };
        }

        // Map the section into the process's address space.
        map_view_ptr view{ ::MapViewOfFile(
            mapping_handle.get(),
            FILE_MAP_ALL_ACCESS,
            0, // high-order DWORD of the file offset
            0, // low-order DWORD of the file offset
            view_size // number of bytes to map
        ) };
        if (!view)
        {
            return std::unexpected{ HRESULT_FROM_WIN32(::GetLastError()) };
        }

        return image_view{ std::move(mapping_handle), std::move(view) };
    }
}
