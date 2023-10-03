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

#include <winsock2.h>
#include <netfork-shared/phnt_stub.hpp>

namespace netfork
{
	enum class fork_context
	{
		error = 0, parent, child
	};

	fork_context fork(_In_ SOCKET nf_server_sock, _In_opt_ PCONTEXT restore_context);
}
