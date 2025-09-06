#include "config.hpp"
#include "git.hpp"
#include "svn.hpp"

#include <date/date.h>
#include <date/tz.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <git2.h>
#include <git2/pathspec.h>
#include <openssl/sha.h>
#include <re2/re2.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

std::string GetAuthor(const Config& config, const std::string& username)
{
	const std::string& domain = config.domain.value_or("localhost");

	if (username.empty())
	{
		return fmt::format("Unknown User <unknown@{}>", domain);
	}
	if (config.identityMap.contains(username))
	{
		return config.identityMap.at(username);
	}

	return fmt::format("{} <{}@{}>", username, username, domain);
}

std::string GetCommitMessage(
	const Config& config, const std::string& log, const std::string& username, long int rev
)
{
	return fmt::format(
		fmt::runtime(config.commitMessage), fmt::arg("log", log), fmt::arg("usr", username),
		fmt::arg("rev", rev)
	);
}

std::string GetSha256(const std::string_view input)
{
	std::array<unsigned char, SHA256_DIGEST_LENGTH> hash{};
	SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash.data());

	std::string result;
	result.reserve(SHA256_DIGEST_LENGTH * 2);
	for (unsigned char byte : hash)
	{
		result += fmt::format("{:02x}", byte);
	}
	return result;
}

std::string GetLFSPointer(const std::string_view input)
{
	return fmt::format(
		"version https://git-lfs.github.com/spec/v1\noid sha256:{}\nsize {}", GetSha256(input),
		input.size()
	);
}

std::string GetTime(const Config& config, const std::string& svnTime)
{
	// It looks like SVN stores dates in UTC time
	// https://svn.haxx.se/users/archive-2003-09/0322.shtml
	// This is good, because we don't have to mess with time zones when converting
	// to Unix Epoch time (which git uses). We might however, want to apply a local
	// UTC offset based on the location of the server.

	std::istringstream dateStream{svnTime};
	std::chrono::sys_time<std::chrono::seconds> utcTime;
	dateStream >> date::parse("%FT%T", utcTime);

	auto unixEpoch = utcTime.time_since_epoch().count();

	const date::time_zone* tz = date::get_tzdb().locate_zone(config.timezone);

	date::zoned_time<std::chrono::seconds> zonedTime{tz, utcTime};
	std::string formattedOffset = date::format("%z", zonedTime);

	return fmt::format("{} {}", unixEpoch, formattedOffset);
}

std::optional<Mapping>
MapPath(const Config& config, const long int rev, const std::string_view& path)
{
	const std::vector<Rule>& rules = config.rules;

	for (const Rule& rule : rules)
	{
		// Given a RULE, takes an INPUT REVISION and INPUT SVN PATH
		// 1. If not MIN REVISION <= INPUT REVISION <= MAX REVISION continue
		//    to next rule
		// 2. If not INPUT SVN PATH starts with and matches against
		//    RULE SVN PATH continue, recording any non-captured suffix
		// 3. If no REPOSITORY, ignore path, break and skip all other rules
		// 4. Rewrite GIT REPO with substitutions from SVN PATH match
		// 5. Rewrite BRANCH with substitutions from SVN PATH match
		// 6. Rewrite GIT PATH with substitutions from SVN PATH match
		// 7. Append GIT PATH with the non-captured suffix
		// 8. Check if GIT PATH full matches with a rule in LFS RULES
		// 9. Output GIT REPO, GIT BRANCH, a GIT PATH, and if LFS

		if (rule.minRevision && *rule.minRevision > rev)
		{
			continue;
		}
		if (rule.maxRevision && *rule.maxRevision < rev)
		{
			continue;
		}

		int capturesGroups = rule.svnPath->NumberOfCapturingGroups();
		// Regex must be valid so the capture group number is always >= 0
		auto containerSize = static_cast<size_t>(capturesGroups);

		std::vector<std::string_view> capturesStrings(containerSize);

		std::vector<RE2::Arg> args(containerSize);
		std::vector<RE2::Arg*> argPtrs(containerSize);

		for (size_t i = 0; i < containerSize; ++i)
		{
			args[i] = RE2::Arg(&capturesStrings[i]);
			argPtrs[i] = &args[i];
		}

		std::string_view consumedPtr(path);
		if (!RE2::ConsumeN(&consumedPtr, *rule.svnPath, argPtrs.data(), capturesGroups))
		{
			continue;
		}

		// Insert the whole capture so the \0 substitution can be used properly
		int captureGroupsWith0th = capturesGroups + 1;

		auto wholeCaptureBegin = path.begin();
		auto wholeCaptureEnd = consumedPtr.begin();
		capturesStrings.emplace(capturesStrings.begin(), wholeCaptureBegin, wholeCaptureEnd);

		if (rule.skipRevision)
		{
			return Mapping(true);
		}

		Mapping result;

		rule.svnPath->Rewrite(
			&result.repo, rule.gitRepository, capturesStrings.data(), captureGroupsWith0th
		);

		rule.svnPath->Rewrite(
			&result.branch, rule.gitBranch, capturesStrings.data(), captureGroupsWith0th
		);

		rule.svnPath->Rewrite(
			&result.path, rule.gitFilePath, capturesStrings.data(), captureGroupsWith0th
		);

		// Append any of the non-captured SVN path to the output git path
		result.path.append(consumedPtr);

		const int res = git_pathspec_matches_path(
			config.lfsPathspec.get(), GIT_PATHSPEC_DEFAULT, result.path.c_str()
		);
		result.lfs = res == 1;

		return result;
	}
	return std::nullopt;
}

std::expected<void, std::string>
WriteCommit(const Config& config, const svn::Revision& rev, std::ostream& output)
{
	// 1. For each revision, get revision properties
	//     - Author
	//     - Date
	//     - Log message
	// 2. Gather all changes in a revision, grouping by output repo & branch
	//    storing metadata and a lazy file stream.
	//     - Output path -> output repo, branch, path & LFS
	//     - Change type added/removed/modified
	//     - Copy from?
	//     - Merge info?
	//     - File size
	//     - File data stream
	// 3. For each repo & branch, commit files.

	// THINK ABOUT
	//  - Failure / cancelling mid process
	//  - Saving marks
	//  - Continuing from where you left off
	//  - Branch creation, working out "from" commit
	//  - Merging?

	std::string committer = git::GetAuthor(config, rev.GetAuthor());
	std::string message =
		git::GetCommitMessage(config, rev.GetLog(), rev.GetAuthor(), rev.GetNumber());
	std::string time = git::GetTime(config, rev.GetDate());

	std::unordered_map<const svn::File*, Mapping> fileMappings;

	using Repo = std::string;
	using Branch = std::string;
	using FileList = std::vector<const svn::File*>;
	std::unordered_map<Repo, std::unordered_map<Branch, FileList>> repoBranchMappings;

	for (const auto& file : rev.GetFiles())
	{
		std::optional<Mapping> destination = MapPath(config, rev.GetNumber(), file.path);

		if (!destination)
		{
			if (config.strictMode)
			{
				return std::unexpected(
					fmt::format(
						"ERROR: The path {:?} for r{} does not map to a git location. Stopping progress because strict_mode is enabled",
						file.path, rev.GetNumber()
					)
				);
			}
			else
			{
				continue;
			}
		}

		if (destination->skip)
		{
			continue;
		}
		fileMappings[&file] = *destination;
		repoBranchMappings[destination->repo][destination->branch].push_back(&file);
	}

	// FIXME: For loop followed by triple nested for loop feels like a bad way to do this.
	for (const auto& [repo, branchMap] : repoBranchMappings)
	{
		for (const auto& [branch, fileList] : branchMap)
		{
			std::string ref = fmt::format("refs/heads/{}", branch);
			fmt::println(output, "commit {}", ref);
			fmt::println(output, "original-oid r{}", rev.GetNumber());
			fmt::println(output, "committer {} {}", committer, time);
			fmt::println(output, "data {}\n{}", message.length(), message);

			for (const auto* file : fileList)
			{
				auto destination = fileMappings[file];

				if (file->changeType == svn::File::Change::Delete)
				{
					fmt::println(output, "D {}", destination.path);
				}
				else if (!file->isDirectory)
				{
					std::string_view buff{file->buffer.get(), file->size};

					fmt::println(
						output, "M {} inline {}", static_cast<int>(Mode::Normal), destination.path
					);
					fmt::println(output, "data {}\n{}", file->size, buff);
				}
			}
		}
	}

	return {};
}

} // namespace git
