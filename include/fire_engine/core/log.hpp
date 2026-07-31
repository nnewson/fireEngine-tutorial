#pragma once

#include <cstdio>
#include <format>
#include <print>
#include <source_location>
#include <type_traits>
#include <utility>

/** @brief Public API for the Fire Engine tutorial. */
namespace fire_engine
{
/** @brief Implementation details that are not part of the public API. */
namespace detail
{
/* --- Implementation types --- */

/**
 * @brief Couples a checked format string with the source location of its call site.
 *
 * Keeping the format's argument types preserves std::println's compile-time
 * checks, unlike accepting a runtime string view.
 *
 * @tparam Args Types consumed by the format string.
 */
template <typename... Args>
class LogMessage
{
public:
    /**
     * @brief Captures a format string and the location at which it was supplied.
     * @tparam String Type of the format string expression.
     * @param format Compile-time checked format string.
     * @param location Source location to report if formatting fails.
     */
    template <typename String>
    explicit(false) consteval LogMessage(
        const String& format, std::source_location location = std::source_location::current())
        : format_{format},
          location_{location}
    {
    }

    /**
     * @brief Returns the checked format string.
     * @return Reference to the format string stored by this message.
     */
    [[nodiscard]] constexpr const std::format_string<Args...>& format() const noexcept
    {
        return format_;
    }

    /**
     * @brief Returns the source location captured for the log call.
     * @return Reference to the captured source location.
     */
    [[nodiscard]] constexpr const std::source_location& location() const noexcept
    {
        return location_;
    }

private:
    std::format_string<Args...> format_; ///< Compile-time checked message format.
    std::source_location location_;      ///< Call site used by the fallback message.
};
} // namespace detail

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
