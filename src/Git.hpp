#pragma once
#include "Config.hpp"
#include "Svn.hpp"
#include "Writer.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

class Git
{
public:
	struct StartingState
	{
		bool isRepoEmpty = false;
		std::vector<std::string> existingBranches;
	};

	struct Mapping
	{
		bool skip = false;
		std::string branch;
		std::string path;
		bool lfs = false;
	};

	Git(const Config& config, IFastImport& writer, StartingState startingState) :
		mConfig(config),
		mWriter(writer),
		mStartingState(std::move(startingState)) {};

	std::string GetAuthor(const std::string& username);

	std::string GetCommitMessage(const std::string& log, const std::string& username, long int rev);

	std::string GetGitAttributesContent();

	std::string GetTime(const std::string& svnTime);

	std::string WriteLFSFile(const std::string_view input);

	std::string ConvertSymlink(std::string_view svnSymlink);

	std::optional<Mapping> MapPath(const long int rev, const std::string_view& svnPath);

	std::optional<std::string> GetBranchOrigin(const std::string& branch);

	std::expected<void, std::string> WriteCommit(const svn::Revision& rev);

private:
	const Config& mConfig;
	IFastImport& mWriter;
	const StartingState mStartingState;

	bool mFirstCommit = true;
	std::unordered_set<std::string> mSeenBranches;
};
