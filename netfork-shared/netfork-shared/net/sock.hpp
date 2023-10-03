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

#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <span>

#include <winsock2.h>

#include <netfork-shared/log.hpp>
#include <netfork-shared/phnt_stub.hpp>

namespace netfork::net
{
    constexpr const HRESULT INCOMPLETE_RECV_DATA = 0xA0000001;

    inline BOOL winsock_init()
    {
        WSADATA wsa_data;
        return ::WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }

    SOCKET connect_to_server(PCSTR address, PCSTR port);
    // TODO: accept multiple clients
    SOCKET accept_single_client(PCSTR port);

    template <typename T, std::size_t N>
    HRESULT recv_bytes(SOCKET sock, std::span<T, N> buf)
    {
        const std::size_t total_size = buf.size_bytes();
        std::uintptr_t offset = 0;
        while (total_size - offset > 0)
        {
            const int rc = ::recv(
                sock,
                reinterpret_cast<char*>(buf.data() + offset),
                total_size - offset,
                0
            );
            if (rc == SOCKET_ERROR)
            {
                return HRESULT_FROM_WIN32(::WSAGetLastError());
            }
            // Client may have closed the connection early.
            if (rc == 0) [[unlikely]]
            {
                break;
            }

            // If a recv hasn't filled the buffer, it'll be adjusted
            // by the number of bytes read, and the next read will
            // start from the end of the previous read.
            offset += rc;
        }

        // Unlike a send where we have full control over how we're sending,
        // when we're receiving data we may not get all of it, so we need to
        // ensure we fail if we didn't receive the full message.
        return total_size == offset ? ERROR_SUCCESS : INCOMPLETE_RECV_DATA;
    }

    template <typename T>
    std::expected<T, HRESULT> recv_as(SOCKET sock)
    {
        alignas(alignof(T)) char buf[sizeof(T)] = { 0 };

        if (FAILED(recv_bytes(sock, std::span{ buf })))
        {
            return std::unexpected{ HRESULT_FROM_WIN32(::WSAGetLastError()) };
        }

        return std::bit_cast<T>(buf);
    }

    template <std::size_t N>
    HRESULT send_bytes(SOCKET sock, std::span<const std::byte, N> buf)
    {
        const std::size_t total_size = buf.size_bytes();
        std::uintptr_t offset = 0;
        while (total_size - offset > 0)
        {
            const int rc = ::send(
                sock,
                reinterpret_cast<const char*>(buf.data() + offset),
                total_size - offset,
                0
            );
            if (rc == SOCKET_ERROR)
            {
                return HRESULT_FROM_WIN32(::WSAGetLastError());
            }

            // If a send hasn't consumed the buffer, it'll be
            // adjusted by the number of bytes sent, and the
            // next send will start from the end of the previous send.
            offset += rc;
        }

        return ERROR_SUCCESS;
    }

    template <typename T>
    HRESULT send_as(SOCKET sock, const T& t)
    {
        const auto buf = std::bit_cast<std::array<const std::byte, sizeof(T)>>(t);
        return send_bytes(sock, std::span{ buf });
    }
}
