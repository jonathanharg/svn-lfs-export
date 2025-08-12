#pragma once
#include "config.hpp"
#include <string>

namespace git
{

enum class Mode
{
	Normal = 100644,
	Executable = 100755,
	Symlink = 120000,
	GitLink = 160000,
	Subdirectory = 040000,
};

struct OutputLocation
{
	std::string repo;
	std::string branch;
	std::string path;
	bool lfs = false;
};

std::string GetGitAuthor(const Config& config, const std::string& username);

std::string GetCommitMessage(const Config& config, const std::string& log, const std::string& username, long int rev);

std::string GetGitTime(const Config& config, const std::string& svnTime);

std::optional<OutputLocation> MapPathToOutput(const Config& config, const long int rev, const std::string_view& path);

} // namespace git
