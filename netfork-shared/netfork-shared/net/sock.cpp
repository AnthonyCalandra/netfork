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

#include "sock.hpp"

#include <ws2tcpip.h>

#include <netfork-shared/auto.hpp>

namespace netfork::net
{
    SOCKET connect_to_server(PCSTR address, PCSTR port)
    {
        SOCKET sock = INVALID_SOCKET;
        const addrinfo hints{
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = IPPROTO_TCP
        };
        addrinfo* result = nullptr;

        if (const int result_code = ::getaddrinfo(address, port, &hints, &result);
            result_code != 0)
        {
            return sock;
        }

        AT_SCOPE_EXIT(::freeaddrinfo(result));

        // Attempt to connect to an address until one succeeds.
        for (auto* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
        {
            sock = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (sock == INVALID_SOCKET)
            {
                return sock;
            }

            const int result_code = ::connect(
                sock,
                ptr->ai_addr,
                static_cast<int>(ptr->ai_addrlen)
            );
            if (result_code == SOCKET_ERROR)
            {
                ::closesocket(sock);
                sock = INVALID_SOCKET;
                continue;
            }

            break;
        }

        return sock;
    }

    SOCKET accept_single_client(PCSTR port)
    {
        const addrinfo hints{
            .ai_flags = AI_PASSIVE,
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = IPPROTO_TCP
        };
        addrinfo* result = nullptr;

        if (::getaddrinfo(nullptr, port, &hints, &result) != 0)
        {
            return INVALID_SOCKET;
        }

        AT_SCOPE_EXIT(::freeaddrinfo(result));

        SOCKET listen_sock = ::socket(
            result->ai_family,
            result->ai_socktype,
            result->ai_protocol
        );
        if (listen_sock == INVALID_SOCKET)
        {
            return INVALID_SOCKET;
        }

        AT_SCOPE_EXIT(::closesocket(listen_sock));

        if (::bind(listen_sock, result->ai_addr,
                static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR)
        {
            return INVALID_SOCKET;
        }

        if (::listen(listen_sock, SOMAXCONN) == SOCKET_ERROR)
        {
            return INVALID_SOCKET;
        }

        return ::accept(listen_sock, nullptr, nullptr);
    }
}
