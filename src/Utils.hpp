#include <fmt/base.h>
#include <fmt/ostream.h>

#include <iostream>

template <typename... T>
inline void Log(fmt::format_string<T...> fmt, T&&... args)
{
	fmt::println(std::cerr, fmt, std::forward<T>(args)...);
}
