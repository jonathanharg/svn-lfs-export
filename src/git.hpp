#pragma once
#include "config.hpp"
#include "svn.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

class Git
{
public:
	struct Mapping
	{
		bool skip = false;
		std::string repo;
		std::string branch;
		std::string path;
		bool lfs = false;
	};

	Git(const Config& config) :
		mConfig(config) {};

	std::string GetAuthor(const std::string& username);

	std::string GetCommitMessage(const std::string& log, const std::string& username, long int rev);

	std::string GetTime(const std::string& svnTime);

	std::string GetSha256(const std::string_view inputStr);

	std::string WriteLFSFile(const std::string_view input, const std::filesystem::path& root);

	std::string GetGitAttributesFile();

	std::optional<Mapping> MapPath(const long int rev, const std::string_view& path);

	std::expected<void, std::string> WriteCommit(
		const svn::Revision& rev, std::ostream& output, const std::filesystem::path& lfsRoot
	);

private:
	const Config& mConfig;
};
