#include "config.hpp"
#include "git.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

TEST_CASE("SVN usernames map to git", "[git]")
{
	Config config;
	CHECK(GetGitAuthor(config, "") == "Unknown User <unknown@localhost>");
}