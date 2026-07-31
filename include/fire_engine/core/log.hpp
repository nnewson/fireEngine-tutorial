#pragma once

#include <cstdio>
#include <format>
#include <print>
#include <source_location>
#include <type_traits>
#include <utility>

namespace fire_engine
{
namespace detail
{
/* --- Implementation types --- */

// This wrapper captures the call site while the format string is converted.
// Keeping the format's argument types preserves std::println's compile-time
// checks, unlike accepting a runtime string_view.
template <typename... Args>
class LogMessage
{
public:
    template <typename String>
    explicit(false) consteval LogMessage(
        const String& format, std::source_location location = std::source_location::current())
        : format_{format},
          location_{location}
    {
    }

    [[nodiscard]] constexpr const std::format_string<Args...>& format() const noexcept
    {
        return format_;
    }

    [[nodiscard]] constexpr const std::source_location& location() const noexcept
    {
        return location_;
    }

private:
    std::format_string<Args...> format_;
    std::source_location location_;
};
} // namespace detail

/* --- Functions --- */

// Error reporting is often used precisely when the program is already
// failing. Keep it noexcept so formatting or allocation failures cannot mask
// the original problem or escape through a C callback boundary. type_identity
// makes Args come from the values, then LogMessage validates the format against
// those types and captures the source location of this call.
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
