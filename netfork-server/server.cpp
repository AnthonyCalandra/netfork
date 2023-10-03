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

#include <bit>
#include <expected>
#include <utility>

#include "image.hpp"
#include "pe.hpp"
#include "proc.hpp"
#include "vm.hpp"

#include <netfork-shared/auto.hpp>
#include <netfork-shared/log.hpp>
#include <netfork-shared/net/msg.hpp>
#include <netfork-shared/net/sock.hpp>
#include <netfork-shared/phnt_stub.hpp>
#include <netfork-shared/utils.hpp>

namespace
{
    constexpr PCSTR SERVICE_PORT = "43594";

    std::expected<managed_string, NTSTATUS> get_nt_path(PCWSTR unexpanded_path)
    {
        managed_string path{ MAX_PATH };

        {
            UNICODE_STRING unexpanded_temp_path;
            RtlInitUnicodeString(&unexpanded_temp_path, unexpanded_path);

            const NTSTATUS status = RtlExpandEnvironmentStrings_U(
                nullptr,
                &unexpanded_temp_path,
                &path.get(),
                nullptr
            );
            if (NT_ERROR(status))
            {
                return std::unexpected{ status };
            }
        }

        return path;
    }
}

int main()
{
    using namespace netfork;

    if (!net::winsock_init())
    {
        LOG_DEBUG_ERR() << "Winsock failed to initialize." << std::endl;
        return 1;
    }

    AT_SCOPE_EXIT(::WSACleanup());

    SOCKET client_sock = net::accept_single_client(SERVICE_PORT);
    if (client_sock == INVALID_SOCKET)
    {
        LOG_DEBUG_ERR() << "Failed to accept client on port " << SERVICE_PORT << std::endl;
        return 1;
    }

    AT_SCOPE_EXIT([client_sock]
        {
            ::shutdown(client_sock, SD_BOTH);
            ::closesocket(client_sock);
        }());

    const auto remote_thread_context = net::recv_as<CONTEXT>(client_sock);
    const auto forked_peb = net::recv_as<PEB>(client_sock);
    const auto forked_teb = net::recv_as<TEB>(client_sock);
    const auto size_of_image = net::recv_as<DWORD>(client_sock);
    if (!remote_thread_context || !forked_peb || !forked_teb || !size_of_image)
    {
        LOG_DEBUG_ERR() << "Failed to receive CONTEXT, PEB, TEB, or image size." << std::endl;
        return 1;
    }

    // FIXME: This will not work if we are netforking multiple images at once.
    // Fix would involve attaching a unique ID to the name.
    auto image_path = ::get_nt_path(L"\\??\\%TEMP%\\netforked-image.exe");
    if (!image_path)
    {
	    LOG_DEBUG_ERR() << "Failed to get NT path for image." << std::endl;
		return 1;
    }

    auto image_file_handle = io::create_temporary_image(
        size_of_image.value(),
        image_path.value().get()
    );
    if (!image_file_handle)
    {
        LOG_DEBUG_ERR() << "Failed to create temporary image file." << std::endl;
        return 1;
    }

    {
        auto image_view_info = io::create_image_view(
            image_file_handle.value().get(),
            size_of_image.value()
        );
        if (!image_view_info)
        {
            LOG_DEBUG_ERR() << "Failed to create image view." << std::endl;
            return 1;
        }

        io::image_view image_view{ std::move(image_view_info.value()) };

        const auto recv_result = net::recv_bytes(
            client_sock,
            std::span<std::byte>{
                static_cast<std::byte*>(image_view.view.get()),
                size_of_image.value()
            }
        );
        if (SUCCEEDED(recv_result))
        {
            LOG_DEBUG() << "Received 0x" << std::hex << size_of_image.value()
                << std::dec << " image bytes" << std::endl;
        }
        else
        {
            LOG_DEBUG() << "Failed to receive 0x" << std::hex << size_of_image.value()
                << std::dec << " image bytes" << std::endl;
        }

        if (!io::pe::modify_pe_image_for_execution(image_view.view, forked_peb.value()))
        {
            LOG_DEBUG_ERR() << "Failed to modify PE image for execution." << std::endl;
            return 1;
        }
    }

    unique_nt_handle<attached_process_deleter> forked_process_handle{};
    {
        auto handle = proc::create_forked_process(image_file_handle.value().get());
        if (!handle)
        {
            LOG_DEBUG_ERR() << "Failed create forked process." << std::endl;
            return 1;
        }

        forked_process_handle = std::move(handle).value();
    }

    if (!vm::rebuild_forked_process(forked_process_handle.get(), client_sock))
    {
        LOG_DEBUG_ERR() << "Failed to rebuild forked process." << std::endl;
        return 1;
    }

    unique_nt_handle forked_thread_handle{};
    {
        auto handle = proc::create_forked_thread(
            forked_process_handle.get(),
            remote_thread_context.value()
        );
        if (!handle)
        {
            LOG_DEBUG_ERR() << "Failed create forked process." << std::endl;
            return 1;
        }

        forked_thread_handle = std::move(handle).value();
    }

    ::ResumeThread(forked_thread_handle.get());
    ::WaitForSingleObject(forked_process_handle.get(), INFINITE);

    if (DWORD exit_code = 0;
        ::GetExitCodeProcess(forked_process_handle.get(), static_cast<LPDWORD>(&exit_code)))
    {
        LOG_DEBUG() << "Exit code of child process: " << exit_code << std::endl;
    }

    // Release the process handle; we are essentially "detaching" from the process.
    forked_process_handle.release();

    return 0;
}
