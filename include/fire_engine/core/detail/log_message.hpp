#pragma once

#include <format>
#include <source_location>

namespace fire_engine::detail
{
/** @cond INTERNAL */
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
/** @endcond */
} // namespace fire_engine::detail
