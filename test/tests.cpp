#include "config.hpp"
#include "git.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SVN usernames map to git", "[git]")
{
	Config config;
	Git git(config);
	CHECK(git.GetAuthor("") == "Unknown User <unknown@localhost>");

	config.domain = "mycorp.com";
	CHECK(git.GetAuthor("") == "Unknown User <unknown@mycorp.com>");
	CHECK(git.GetAuthor("johnappleseed") == "johnappleseed <johnappleseed@mycorp.com>");

	config.identityMap["jsmith"] = "my full string value";
	CHECK(git.GetAuthor("jsmith") == "my full string value");
}

TEST_CASE("SVN log maps to commit message", "[git]")
{
	Config config;
	Git git(config);

	config.commitMessage = "my message";
	CHECK(git.GetCommitMessage("svn log", "svn usr", 123) == "my message");

	config.commitMessage = "fmt usr:{usr} rev:{rev} log:{log}";
	CHECK(
		git.GetCommitMessage("svn log", "svn usr", 123) ==
		"fmt usr:svn usr rev:123 log:svn log"
	);
}

TEST_CASE("SVN time maps to git time", "[git]")
{
	Config config;
	Git git(config);

	CHECK(git.GetTime("2005-02-20T01:52:55.851101Z") == "1108864375 +0000");

	CHECK(git.GetTime("2003-04-01T06:17:43.000000Z") == "1049177863 +0000");

	CHECK(git.GetTime("2012-02-25T02:04:17.232774Z") == "1330135457 +0000");

	CHECK(git.GetTime("2006-07-06T04:34:46.728945Z") == "1152160486 +0000");

	config.timezone = "America/New_York";
	CHECK(git.GetTime("2017-03-07T00:21:32.725645Z") == "1488846092 -0500");

	config.timezone = "America/Caracas";
	CHECK(git.GetTime("2018-07-19T12:17:25.163264Z") == "1532002645 -0400");

	config.timezone = "Asia/Singapore";
	CHECK(git.GetTime("2005-12-05T03:04:25.784527Z") == "1133751865 +0800");

	config.timezone = "Europe/London";
	CHECK(git.GetTime("2006-05-28T23:33:05.132279Z") == "1148859185 +0100");

	config.timezone = "Europe/London";
	CHECK(git.GetTime("2015-11-16T04:44:26.025081Z") == "1447649066 +0000");
}

TEST_CASE("sha256 works")
{
	Config config;
	Git git(config);
	const char* kHelloWorldHash =
		"dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f";
	CHECK(git.GetSha256("Hello, World!") == kHelloWorldHash);
}
