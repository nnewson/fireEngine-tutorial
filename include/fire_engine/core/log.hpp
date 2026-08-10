#pragma once

#include <cstdio>
#include <print>
#include <source_location>
#include <type_traits>
#include <utility>

#include <fire_engine/core/detail/log_message.hpp>

/** @brief Public API for the Fire Engine tutorial. */
namespace fire_engine
{
/* --- Functions --- */

/**
 * @brief Writes a formatted message to the standard error stream without throwing.
 *
 * Error reporting is often used precisely when the program is already failing.
 * Keeping this function noexcept prevents formatting or allocation failures from
 * masking the original problem or escaping through a C callback boundary.
 * std::type_identity makes Args come from the values, then LogMessage validates
 * the format against those types and captures the source location of this call.
 *
 * @tparam Args Types of the values inserted into the message.
 * @param message Checked format string and captured call site.
 * @param args Values inserted into the format string.
 */
template <typename... Args>
void log(detail::LogMessage<std::type_identity_t<Args>...> message, Args&&... args) noexcept
{
    try
    {
        std::println(stderr, message.format(), std::forward<Args>(args)...);
    }
    catch (...)
    {
        const std::source_location& location = message.location();

        // C stdio does not throw C++ exceptions, making it suitable as the
        // last-resort path inside this noexcept function.
        // std::print may throw, so it cannot replace fprintf in this fallback.
        // NOLINTNEXTLINE(modernize-use-std-print)
        std::fprintf(stderr, "A log message could not be formatted at %s:%lu in %s.\n",
                     location.file_name(), static_cast<unsigned long>(location.line()),
                     location.function_name());
    }
}
} // namespace fire_engine
