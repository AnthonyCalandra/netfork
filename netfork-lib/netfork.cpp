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

#include "netfork.hpp"

#include <psapi.h>

#include <span>
#include <string>
#include <utility>
#include <variant>

#include "vm.hpp"

#include <netfork-shared/auto.hpp>
#include <netfork-shared/log.hpp>
#include <netfork-shared/net/msg.hpp>
#include <netfork-shared/net/sock.hpp>
#include <netfork-shared/phnt_stub.hpp>

namespace
{
    struct image_info
    {
        LPVOID base_address;
        DWORD size;
    };

    image_info get_image_info()
    {
        HMODULE process_image;
        // Get the current module's base address.
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            nullptr,
            &process_image
        );

        MODULEINFO mi{};
        GetModuleInformation(GetCurrentProcess(), process_image, &mi, sizeof(mi));

        return { .base_address = mi.lpBaseOfDll, .size = mi.SizeOfImage };
    }

    struct message_transporter
    {
        SOCKET socket;

        message_transporter(SOCKET s)
            : socket{ s }
        {
        }

        HRESULT operator()(const netfork::net::msg::region_info& msg) const
        {
            return netfork::net::send_as(socket, msg);
        }

        HRESULT operator()(const netfork::net::msg::subregion_info& msg) const
        {
            return netfork::net::send_as(socket, msg);
        }

        HRESULT operator()(std::span<const char> msg) const
        {
            return netfork::net::send_bytes(socket, std::as_bytes(msg));
        }
    };
}

namespace netfork
{
    fork_context fork(_In_ SOCKET nf_server_sock, _In_opt_ PCONTEXT restore_context)
    {
        {
            CONTEXT current_context{ .ContextFlags = CONTEXT_ALL };
            ::RtlCaptureContext(&current_context);

            if (current_context.Rax == std::to_underlying(fork_context::child))
            {
                return fork_context::child;
            }

            current_context.Rax = std::to_underlying(fork_context::child);

            PCONTEXT context_to_restore = &current_context;
            if (restore_context)
            {
                context_to_restore = restore_context;
            }

            if (const auto result = net::send_as(nf_server_sock, *context_to_restore);
                FAILED(result))
            {
                LOG_DEBUG_ERR() << "send_as failed with error: " << result << std::endl;
                return fork_context::error;
            }
        }

        {
            PEB peb{};

            ::RtlAcquirePebLock();
            std::memcpy(&peb, ::NtCurrentTeb()->ProcessEnvironmentBlock, sizeof(PEB));
            ::RtlReleasePebLock();

            if (const auto result = net::send_as(nf_server_sock, peb); FAILED(result))
            {
                LOG_DEBUG_ERR() << "send_as failed with error: " << result << std::endl;
                return fork_context::error;
            }
        }

        {
            TEB teb{};
            std::memcpy(&teb, ::NtCurrentTeb(), sizeof(TEB));

            if (const auto result = net::send_as(nf_server_sock, teb); FAILED(result))
            {
                LOG_DEBUG_ERR() << "send_as failed with error: " << result << std::endl;
                return fork_context::error;
            }
        }

        const auto [image_allocation_base, image_size] = get_image_info();

        if (const auto result = net::send_as(nf_server_sock, image_size); FAILED(result))
        {
            LOG_DEBUG_ERR() << "send_as failed with error: " << result << std::endl;
            return fork_context::error;
        }

        {
            auto vm_image_results = vm::query_virtual_memory_if(
                [image_allocation_base](const MEMORY_BASIC_INFORMATION& mbi)
                {
                    return mbi.Type == MEM_IMAGE
                        && mbi.AllocationBase == image_allocation_base;
                });

            while (vm_image_results)
            {
                message_transporter encoder{ nf_server_sock };

                const auto msg = vm_image_results();
                // We only want to send the image itself
                // (no sub/region info since we don't need it).
                if (std::holds_alternative<std::span<char>>(msg))
                {
                    const auto buf = std::get<std::span<char>>(msg);
                    const auto result = encoder(buf);
                    if (FAILED(result))
                    {
                        LOG_DEBUG_ERR() << "Failed to send image bytes; error: "
                            << result << std::endl;
                        return fork_context::error;
                    }

                    LOG_DEBUG() << "Sent 0x" << std::hex << buf.size()
                        << std::dec << " image bytes" << std::endl;
                }
            }
        }

        {
            auto vm_committed_results = vm::query_virtual_memory_if(
                [](const MEMORY_BASIC_INFORMATION& mbi)
                {
                    return mbi.Type != MEM_IMAGE;
                });

            while (vm_committed_results)
            {
                const auto msg = vm_committed_results();
                const auto result = std::visit(
                    message_transporter{ nf_server_sock },
                    msg
                );
                if (FAILED(result))
                {
                    LOG_DEBUG_ERR() << "Failed to send region data; error: "
                        << result << std::endl;
                    return fork_context::error;
                }

                LOG_DEBUG() << "Sent 0x" << std::hex << sizeof(msg)
                    << std::dec << " bytes" << std::endl;
            }
        }

        return fork_context::parent;
    }
}
