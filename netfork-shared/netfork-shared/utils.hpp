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

#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>
#include <type_traits>

#include <netfork-shared/phnt_stub.hpp>

struct default_handle_deleter
{
	constexpr default_handle_deleter() = default;
	void operator()(const HANDLE handle) const
	{
		::CloseHandle(handle);
	}
};

struct default_nt_handle_deleter
{
	constexpr default_nt_handle_deleter() = default;
	void operator()(const HANDLE handle) const
	{
		::NtClose(handle);
	}
};

template <typename HandleClass, typename HandleType, typename Deleter>
	requires std::is_class_v<HandleClass> && std::same_as<HandleClass, std::remove_cv_t<HandleClass>>
class unique_handle_impl
{
	HandleClass& derived() noexcept
	{
		return static_cast<HandleClass&>(*this);
	}

	const HandleClass& derived() const noexcept
	{
		return static_cast<const HandleClass&>(*this);
	}

protected:
	HandleType handle_;
	Deleter deleter_;

public:
	unique_handle_impl() = default;

	// Deleted copy construction and assignment.
	unique_handle_impl(const unique_handle_impl&) noexcept = delete;
	unique_handle_impl& operator=(const unique_handle_impl&) noexcept = delete;

	unique_handle_impl(unique_handle_impl&& other) noexcept
		: handle_{ other.derived().release() }
		, deleter_{ std::move(other.deleter_) }
	{
	}

	unique_handle_impl& operator=(unique_handle_impl&& other) noexcept
	{
		derived().reset(other.derived().release());
		deleter_ = std::move(other.deleter_);
		return *this;
	}

	~unique_handle_impl() noexcept
	{
		if (derived().is_valid())
		{
			deleter_(handle_);
		}
	}

	HandleType& get() noexcept
	{
		return handle_;
	}

	const HandleType& get() const noexcept
	{
		return handle_;
	}

	Deleter& get_deleter() noexcept
	{
		return deleter_;
	}

	const Deleter& get_deleter() const noexcept
	{
		return deleter_;
	}

	explicit operator bool() const noexcept
	{
		return derived().is_valid();
	}

	HandleType& operator*() noexcept
	{
		return handle_;
	}

	const HandleType& operator*() const noexcept
	{
		return handle_;
	}

	void reset(HandleType new_handle)
	{
		HandleType old_handle = std::move(derived().release());
		if (derived().is_valid())
		{
			deleter_(old_handle);
		}

		handle_ = std::move(new_handle);
	}

	HandleType release() noexcept
	{
		HandleType released = std::move(this->handle_);
		this->handle_ = HandleType{};
		return released;
	}
};

template <typename Deleter = default_handle_deleter>
class unique_handle : public unique_handle_impl<unique_handle<Deleter>, HANDLE, Deleter>
{
public:
	unique_handle() noexcept
		: unique_handle_impl<unique_handle<Deleter>, HANDLE, Deleter>()
	{
	}

	explicit unique_handle(const HANDLE handle) noexcept
	{
		this->handle_ = handle;
	}

	bool is_valid() const noexcept
	{
		// Handles can have two "invalid" values: https://devblogs.microsoft.com/oldnewthing/20040302-00/?p=40443
		return this->handle_ != INVALID_HANDLE_VALUE && this->handle_ != nullptr;
	}
};

template <typename Deleter = default_nt_handle_deleter>
using unique_nt_handle = unique_handle<Deleter>;

struct unicode_string_deleter
{
	constexpr unicode_string_deleter() = default;
	void operator()(UNICODE_STRING& str) const
	{
		::RtlFreeUnicodeString(&str);
	}
};

class managed_string : public unique_handle_impl<managed_string, UNICODE_STRING, unicode_string_deleter>
{
public:
	managed_string() noexcept
		: unique_handle_impl<managed_string, UNICODE_STRING, unicode_string_deleter>()
	{
		::RtlInitUnicodeString(&this->handle_, nullptr);
	}

	explicit managed_string(UNICODE_STRING str) noexcept
		: unique_handle_impl<managed_string, UNICODE_STRING, unicode_string_deleter>()
	{
		this->handle_ = std::move(str);
	}

	explicit managed_string(const std::size_t n)
		: unique_handle_impl<managed_string, UNICODE_STRING, unicode_string_deleter>()
	{
		LPVOID buffer = ::HeapAlloc(
			::GetProcessHeap(),
			HEAP_GENERATE_EXCEPTIONS,
			n * sizeof(WCHAR)
		);
		::RtlInitEmptyUnicodeString(&this->handle_, static_cast<PWCHAR>(buffer), n);
	}

	bool is_valid() noexcept
	{
		return NT_SUCCESS(::RtlValidateUnicodeString(0, &this->handle_));
	}
};

struct map_view_deleter
{
	constexpr map_view_deleter() = default;
	void operator()(const LPVOID view) const
	{
		::UnmapViewOfFile(view);
	}
};

using map_view_ptr = std::unique_ptr<VOID, map_view_deleter>;

// TODO: use std::generator
template <typename ResultType>
struct generator
{
	using result_type = ResultType;

	struct promise_type;
	using handle_type = std::coroutine_handle<promise_type>;

	struct promise_type
	{
		result_type value_;
		std::exception_ptr exception_;

		generator get_return_object()
		{
			return generator{ handle_type::from_promise(*this) };
		}

		std::suspend_always initial_suspend()
		{
			return {};
		}

		std::suspend_always final_suspend() noexcept
		{
			return {};
		}

		void unhandled_exception()
		{
			exception_ = std::current_exception(); // saving exception
		}

		template <std::convertible_to<result_type> From>
		std::suspend_always yield_value(From&& from)
		{
			value_ = std::forward<From>(from); // caching the result in promise
			return {};
		}

		void return_void() {}
	};

	handle_type h_;

	generator(handle_type h)
		: h_{ h }
	{
	}

	~generator()
	{
		h_.destroy();
	}

	explicit operator bool()
	{
		fill(); // The only way to reliably find out whether or not we finished coroutine,
		// whether or not there is going to be a next value generated (co_yield)
		// in coroutine via C++ getter (operator () below) is to execute/resume
		// coroutine until the next co_yield point (or let it fall off end).
		// Then we store/cache result in promise to allow getter (operator() below
		// to grab it without executing coroutine).
		return !h_.done();
	}

	result_type operator()()
	{
		fill();
		full_ = false; // we are going to move out previously cached
		// result to make promise empty again
		return std::move(h_.promise().value_);
	}

private:
	bool full_ = false;

	void fill()
	{
		if (!full_)
		{
			h_();
			if (h_.promise().exception_) std::rethrow_exception(h_.promise().exception_);
			// propagate coroutine exception in called context
			full_ = true;
		}
	}
};

// Define custom NTSTATUS codes below (note, the customer bit must be set):
constexpr const NTSTATUS INTERNAL_NETFORK_ERROR = 0xC0010001;

// Deleter for the process handle that will terminate the process
// should any initialization steps fail.
//
// "Attached" essentially means the same thing as "owned" in this context.
// (Same as attached vs. detached threads.)
struct attached_process_deleter
{
	void operator()(HANDLE process) const noexcept
	{
		::NtTerminateProcess(process, STATUS_UNSUCCESSFUL);
		::NtClose(process);
	}
};
