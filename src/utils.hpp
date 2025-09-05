#include <fmt/base.h>
#include <fmt/ostream.h>

#include <iostream>

template <typename... T>
inline void LogError(fmt::format_string<T...> fmt, T&&... args)
{
	fmt::println(std::cerr, fmt, std::forward<T>(args)...);
}

template <typename... T>
inline void LogInfo(fmt::format_string<T...> fmt, T&&... args)
{
	fmt::println("progress {}", fmt::format(fmt, std::forward<T>(args)...));
}
