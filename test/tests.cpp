#include "config.hpp"
#include "git.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SVN usernames map to git", "[git]")
{
	Config config;
	CHECK(git::GetAuthor(config, "") == "Unknown User <unknown@localhost>");
}
