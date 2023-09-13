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

namespace netfork::proc
{
    std::expected<unique_nt_handle<attached_process_deleter>, NTSTATUS>
    create_forked_process(HANDLE image_file_handle)
    {
        unique_nt_handle image_section_handle{};
        // Create an image section from the temporary file.
        NTSTATUS status = ::NtCreateSection(
            &image_section_handle.get(),
            SECTION_ALL_ACCESS,
            nullptr,
            nullptr,
            PAGE_READONLY,
            SEC_IMAGE,
            image_file_handle
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        unique_nt_handle<attached_process_deleter> forked_process_handle{};
        status = ::NtCreateProcessEx(
            &forked_process_handle.get(),
            PROCESS_ALL_ACCESS,
            nullptr,
            NtCurrentProcess(),
            0,
            image_section_handle.get(),
            nullptr,
            nullptr,
            0
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        PROCESS_BASIC_INFORMATION process_info;
        status = ::NtQueryInformationProcess(
            forked_process_handle.get(),
            ::ProcessBasicInformation,
            &process_info,
            sizeof(process_info),
            nullptr
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        PRTL_USER_PROCESS_PARAMETERS parameters = nullptr;

        UNICODE_STRING image_name;
        UNICODE_STRING window_name;

        WCHAR final_image_path[MAX_PATH]{ 0 };

        {
            // Get the full path name in DOS format given the image file handle.
            const DWORD required_buf_size = ::GetFinalPathNameByHandleW(
                image_file_handle,
                reinterpret_cast<LPWSTR>(&final_image_path),
                MAX_PATH,
                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS
            );
            if (required_buf_size > MAX_PATH || required_buf_size == 0)
            {
                return std::unexpected{ INTERNAL_NETFORK_ERROR };
            }
        }

        ::RtlInitUnicodeString(&image_name, reinterpret_cast<PCWSTR>(final_image_path));
        ::RtlInitUnicodeString(&window_name, L"netforked process");

        status = ::RtlCreateProcessParametersEx(
            &parameters,
            &image_name,
            nullptr,
            nullptr,
            &image_name,
            nullptr,
            &window_name,
            nullptr,
            nullptr,
            nullptr,
            RTL_USER_PROC_PARAMS_NORMALIZED
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        AT_SCOPE_EXIT(::RtlDestroyProcessParameters(parameters));

        // Allocate space for the process parameter block in the target.
        const SIZE_T params_size = static_cast<SIZE_T>(parameters->MaximumLength) +
            parameters->EnvironmentSize;
        const SIZE_T params_remote_size = params_size;

        PVOID params_remote = ::VirtualAlloc2(
            forked_process_handle.get(),
            nullptr,
            params_remote_size,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE,
            nullptr,
            0
        );

        // Switch the process parameters to a denormalized form that uses offsets instead of
        // absolute pointers. The target's ntdll will normalize them back on initialization.
        ::RtlDeNormalizeProcessParams(parameters);

        // Unfortunately, denormalization doesn't apply to the environment pointer, so we need
        // to adjust it to be valid remotely.
        reinterpret_cast<ULONG_PTR&>(parameters->Environment) += reinterpret_cast<ULONG_PTR>(params_remote) -
            reinterpret_cast<ULONG_PTR>(parameters);

        status = ::NtWriteVirtualMemory(
            forked_process_handle.get(),
            params_remote,
            parameters,
            params_size,
            nullptr
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        // Update the reference in the PEB.
        status = ::NtWriteVirtualMemory(
            forked_process_handle.get(),
            &process_info.PebBaseAddress->ProcessParameters,
            &params_remote,
            sizeof(params_remote),
            nullptr
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        // Transfer the process handle to a new smart handle with a default deleter.
        // i.e. we are essentially "detaching" from the process.
        return forked_process_handle;
    }

    std::expected<unique_nt_handle<default_nt_handle_deleter>, NTSTATUS>
    create_forked_thread(HANDLE forked_process_handle, const CONTEXT& thread_context)
    {
        // Determine parameters for the initial thread
        SECTION_IMAGE_INFORMATION image_info;
        NTSTATUS status = ::NtQueryInformationProcess(
            forked_process_handle,
            ::ProcessImageInformation,
            &image_info,
            sizeof(image_info),
            nullptr
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        unique_nt_handle forked_thread_handle{};
        status = ::NtCreateThreadEx(
            &forked_thread_handle.get(),
            THREAD_ALL_ACCESS,
            nullptr,
            forked_process_handle,
            image_info.TransferAddress,
            nullptr,
            THREAD_CREATE_FLAGS_CREATE_SUSPENDED,
            image_info.ZeroBits,
            image_info.CommittedStackSize,
            image_info.MaximumStackSize,
            nullptr
        );
        if (NT_ERROR(status))
        {
            return std::unexpected{ status };
        }

        ::SetThreadContext(forked_thread_handle.get(), &thread_context);
        return forked_thread_handle;
    }
}
