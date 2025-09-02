#pragma once
#include "config.hpp"
#include "svn.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace git
{

struct Mapping
{
	bool skip = false;
	std::string repo;
	std::string branch;
	std::string path;
	bool lfs = false;
};

std::string GetAuthor(const Config& config, const std::string& username);

std::string GetCommitMessage(
	const Config& config, const std::string& log, const std::string& username, long int rev
);

std::string GetTime(const Config& config, const std::string& svnTime);

std::string GetSha256(const std::string_view inputStr);

std::string GetLFSPointer(const std::string_view inputStr);

std::optional<Mapping>
MapPath(const Config& config, const long int rev, const std::string_view& path);

std::expected<void, std::string> WriteCommit(const Config& config, const svn::Revision& rev);

} // namespace git
