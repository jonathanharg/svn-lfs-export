#include "config.hpp"
#include "git.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SVN usernames map to git", "[git]")
{
	Config config;
	CHECK(git::GetAuthor(config, "") == "Unknown User <unknown@localhost>");

	config.domain = "mycorp.com";
	CHECK(git::GetAuthor(config, "") == "Unknown User <unknown@mycorp.com>");
	CHECK(git::GetAuthor(config, "johnappleseed") == "johnappleseed <johnappleseed@mycorp.com>");

	config.identityMap["jsmith"] = "my full string value";
	CHECK(git::GetAuthor(config, "jsmith") == "my full string value");
}

TEST_CASE("SVN log maps to commit message", "[git]")
{
	Config config;

	config.commitMessage = "my message";
	CHECK(git::GetCommitMessage(config, "svn log", "svn usr", 123) == "my message");

	config.commitMessage = "fmt usr:{usr} rev:{rev} log:{log}";
	CHECK(
		git::GetCommitMessage(config, "svn log", "svn usr", 123) ==
		"fmt usr:svn usr rev:123 log:svn log"
	);
}

TEST_CASE("SVN time maps to git time", "[git]")
{
	Config config;

	CHECK(git::GetTime(config, "2005-02-20T01:52:55.851101Z") == "1108864375 +0000");

	CHECK(git::GetTime(config, "2003-04-01T06:17:43.000000Z") == "1049177863 +0000");

	CHECK(git::GetTime(config, "2012-02-25T02:04:17.232774Z") == "1330135457 +0000");

	CHECK(git::GetTime(config, "2006-07-06T04:34:46.728945Z") == "1152160486 +0000");

	config.timezone = "America/New_York";
	CHECK(git::GetTime(config, "2017-03-07T00:21:32.725645Z") == "1488846092 -0500");

	config.timezone = "America/Caracas";
	CHECK(git::GetTime(config, "2018-07-19T12:17:25.163264Z") == "1532002645 -0400");

	config.timezone = "Asia/Singapore";
	CHECK(git::GetTime(config, "2005-12-05T03:04:25.784527Z") == "1133751865 +0800");

	config.timezone = "Europe/London";
	CHECK(git::GetTime(config, "2006-05-28T23:33:05.132279Z") == "1148859185 +0100");

	config.timezone = "Europe/London";
	CHECK(git::GetTime(config, "2015-11-16T04:44:26.025081Z") == "1447649066 +0000");
}

TEST_CASE("sha256 works")
{
	const char* kHelloWorldHash =
		"dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f";
	CHECK(git::GetSha256("Hello, World!") == kHelloWorldHash);
}
