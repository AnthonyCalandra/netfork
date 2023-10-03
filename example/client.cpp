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

#include <netfork.hpp>
#include <netfork-shared/auto.hpp>
#include <netfork-shared/log.hpp>
#include <netfork-shared/net/sock.hpp>
#include <netfork-shared/phnt_stub.hpp>

namespace
{
    constexpr PCSTR SERVER_IP = "localhost";
    constexpr PCSTR SERVER_PORT = "43594";

    void shutdown_client(SOCKET netfork_server_sock)
    {
        shutdown(netfork_server_sock, SD_SEND);

        WSAEVENT fd_close_event = ::WSACreateEvent();
        WSAEventSelect(netfork_server_sock, fd_close_event, FD_CLOSE);
        // Wait for the `FD_CLOSE` event.
        WSAWaitForMultipleEvents(1, &fd_close_event, TRUE, WSA_INFINITE, FALSE);
        WSACloseEvent(fd_close_event);

        closesocket(netfork_server_sock);
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

    SOCKET netfork_server_sock = net::connect_to_server(SERVER_IP, SERVER_PORT);
    if (netfork_server_sock == INVALID_SOCKET)
    {
        LOG_DEBUG_ERR() << "Unable to connect to server at "
            << SERVER_IP << ':' << SERVER_PORT << std::endl;
        return 1;
    }

    const auto ctx = netfork::fork(netfork_server_sock, nullptr);
    if (ctx == fork_context::child)
    {
        LOG_DEBUG() << "netfork succeeded" << std::endl;
    }
    else
    {
        if (ctx == fork_context::parent)
        {
            LOG_DEBUG() << "netfork succeeded" << std::endl;
        }
        else
        {
            LOG_DEBUG() << "netfork failed" << std::endl;
        }

        ::shutdown_client(netfork_server_sock);
    }

    std::cin.get();
    return 0;
}
