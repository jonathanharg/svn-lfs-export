#include "config.hpp"
#include "git.hpp"
#include "svn.hpp"
#include "utils.hpp"

#include <date/date.h>
#include <date/tz.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/ostream.h>
#include <git2.h>
#include <git2/pathspec.h>
#include <openssl/sha.h>
#include <re2/re2.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class Mode
{
	Normal = 100644,
	Executable = 100755,
	Symlink = 120000,
	GitLink = 160000,
	Subdirectory = 040000,
};

std::string Git::GetAuthor(const std::string& username)
{
	const std::string& domain = mConfig.domain.value_or("localhost");

	if (username.empty())
	{
		return fmt::format("Unknown User <unknown@{}>", domain);
	}
	if (mConfig.identityMap.contains(username))
	{
		return mConfig.identityMap.at(username);
	}

	return fmt::format("{} <{}@{}>", username, username, domain);
}

std::string Git::GetCommitMessage(const std::string& log, const std::string& username, long int rev)
{
	return fmt::format(
		fmt::runtime(mConfig.commitMessage), fmt::arg("log", log), fmt::arg("usr", username),
		fmt::arg("rev", rev)
	);
}

std::string Git::GetSha256(const std::string_view input)
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

std::string Git::WriteLFSFile(const std::string_view input, const std::string_view repo)
{
	ZoneScoped;

	std::string hash = GetSha256(input);
	std::filesystem::path root = mWriter.GetLFSRoot(repo);
	std::filesystem::path path =
		root / "lfs" / "objects" / hash.substr(0, 2) / hash.substr(2, 2) / hash;

	std::filesystem::create_directories(path.parent_path());
	std::ofstream file{path};
	file << input;

	return fmt::format(
		"version https://git-lfs.github.com/spec/v1\noid sha256:{}\nsize {}\n", hash, input.size()
	);
}

std::string Git::GetGitAttributesFile()
{
	std::string attributes;

	for (const std::string& rule : mConfig.lfsRuleStrs)
	{
		attributes.append(fmt::format("{} filter=lfs diff=lfs merge=lfs -text\n", rule));
	}
	return attributes;
}

std::string Git::GetTime(const std::string& svnTime)
{
	// It looks like SVN stores dates in UTC time
	// https://svn.haxx.se/users/archive-2003-09/0322.shtml
	// This is good, because we don't have to mess with time zones when converting
	// to Unix Epoch time (which git uses). We might however, want to apply a local
	// UTC offset based on the location of the server.

	std::istringstream dateStream{svnTime};
	std::chrono::sys_time<std::chrono::seconds> utcTime;
	date::from_stream(dateStream, "%FT%T", utcTime);

	auto unixEpoch = utcTime.time_since_epoch().count();

	const date::time_zone* tz = date::get_tzdb().locate_zone(mConfig.timezone);

	date::zoned_time<std::chrono::seconds> zonedTime{tz, utcTime};
	std::string formattedOffset = date::format("%z", zonedTime);

	return fmt::format("{} {}", unixEpoch, formattedOffset);
}

std::optional<std::string> Git::GetBranchOrigin(const std::string& repo, const std::string& branch)
{
	const bool seenRepo = mSeenRepoBranches.contains(repo);
	const bool seenBranch = seenRepo && mSeenRepoBranches.at(repo).contains(branch);

	if (seenBranch)
	{
		// Branch already exists in fast-import memory
		return std::string("");
	}

	if (!seenRepo && !mWriter.DoesRepoExist(repo))
	{
		// Omit the `from`, this is the first commit to a new repository
		// so create a commit with no ancestor.
		return std::string("");
	}

	if (mWriter.DoesBranchAlreadyExistOnDisk(repo, branch))
	{
		// Load from disk with ^0
		return fmt::format("from refs/heads/{}^0\n", branch);
	}

	if (mConfig.branchMap.contains(branch))
	{
		// Delete contents of new branch, so it's clean like svn
		const std::string fromLocation = mConfig.branchMap.at(branch);
		return fmt::format("from {}\ndeleteall\n", fromLocation);
	}

	// Unknown branch origin
	return std::nullopt;
}

std::optional<Git::Mapping> Git::MapPath(const long int rev, const std::string_view& path)
{
	ZoneScoped;

	const std::vector<Rule>& rules = mConfig.rules;

	for (const Rule& rule : rules)
	{
		ZoneScopedN("Rule");

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

		const auto* wholeCaptureBegin = path.begin();
		const auto* wholeCaptureEnd = consumedPtr.begin();
		capturesStrings.emplace(capturesStrings.begin(), wholeCaptureBegin, wholeCaptureEnd);

		if (rule.skipRevision)
		{
			return Mapping{.skip = true};
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

		if (!result.path.empty() && result.path.front() == '/')
		{
			result.path.erase(0, 1);
		}

		// Pathspecs will match everything if they're empty, only run it if it's not empty
		if (mConfig.lfsRuleStrs.size() > 0)
		{
			const int res = git_pathspec_matches_path(
				mConfig.lfsPathspec.get(), GIT_PATHSPEC_DEFAULT, result.path.c_str()
			);
			result.lfs = res == 1;
		}

		// fast-import paths can't start with '/' and removing it automatically means
		// less regex shenanigans for the user
		if (result.path.starts_with('/'))
		{
			result.path.erase(0, 1);
		}

		return result;
	}
	return std::nullopt;
}

std::expected<void, std::string> Git::WriteCommit(const svn::Revision& rev)
{
	ZoneScoped;

	const std::string committer = GetAuthor(rev.GetAuthor());
	const std::string message = GetCommitMessage(rev.GetLog(), rev.GetAuthor(), rev.GetNumber());
	const std::string time = GetTime(rev.GetDate());

	struct MappedFile
	{
		const svn::File* svn;
		Mapping git;
		auto operator<=>(const MappedFile& other) const
		{
			auto cmp = git.repo <=> other.git.repo;
			if (cmp != 0)
			{
				return cmp;
			}
			return git.branch <=> other.git.branch;
		}
	};
	std::vector<MappedFile> mappings;

	for (const auto& file : rev.GetFiles())
	{
		std::optional<Mapping> destination = MapPath(rev.GetNumber(), file.path);

		if (destination)
		{
			if (!destination->skip)
			{
				mappings.emplace_back(&file, *destination);
			}
		}
		else
		{
			if (mConfig.strictMode && !file.isDirectory)
			{
				return std::unexpected(
					fmt::format(
						"ERROR: The path {:?} for r{} does not map to a git location. Stopping progress because strict_mode is enabled",
						file.path, rev.GetNumber()
					)
				);
			}
		}
	}

	std::sort(mappings.begin(), mappings.end());

	std::string lastRepo;
	std::string lastBranch;

	// One SVN revision mapps to multiple different git commits
	const bool isMultiCommit =
		!mappings.empty() && (mappings.front().git.repo != mappings.back().git.repo ||
							  mappings.front().git.branch != mappings.back().git.branch);

	for (const auto& file : mappings)
	{
		const std::string& repo = file.git.repo;
		const std::string& branch = file.git.branch;

		assert(!repo.empty());
		assert(!branch.empty());

		std::string output;
		auto outputIt = std::back_inserter(output);
		// TODO: output.reserve() based on some heuristic

		if (repo != lastRepo || branch != lastBranch)
		{
			// We've moved onto a new branch/repository, start a new commit!
			lastRepo = repo;
			lastBranch = branch;

			// Only mark unambiguous commits
			const std::string mark =
				!isMultiCommit ? fmt::format("mark :{}\n", rev.GetNumber()) : "";

			const auto from = GetBranchOrigin(repo, branch);
			if (!from.has_value())
			{
				return std::unexpected(
					fmt::format(
						"ERROR: Unknown branch origin for r{} at {:?} (for git branch {}/{}). Provide an origin in the [branch_origin] section of your config.toml file.",
						rev.GetNumber(), file.svn->path, repo, branch
					)
				);
			}

			fmt::format_to(
				outputIt,
				"commit refs/heads/{}\n"
				"{}"
				"original-oid r{}\n"
				"committer {} {}\n"
				"data {}\n"
				"{}\n"
				"{}",
				branch, mark, rev.GetNumber(), committer, time, message.length(), message, *from
			);

			mSeenRepoBranches[repo].insert(branch);

			std::string attributes = GetGitAttributesFile();
			if (attributes.length() > 0)
			{
				// We don't need to be writing this for every commit, just the first to each repo
				// Oh well, it's easier to do it this way for now
				fmt::format_to(
					outputIt,
					"M {} inline .gitattributes\n"
					"data {}\n"
					"{}\n",
					static_cast<int>(Mode::Normal), attributes.size(), attributes
				);
			}
		}

		if (file.svn->changeType == svn::File::Change::Delete)
		{
			fmt::format_to(outputIt, "D {}\n", file.git.path);
		}
		else if (!file.svn->isDirectory)
		{
			auto fileContents = file.svn->GetContents();
			std::string_view svnFile{fileContents.get(), file.svn->size};
			std::string_view outputFile = svnFile;
			std::string lfsPointer;
			Mode mode = file.svn->isExecutable ? Mode::Executable : Mode::Normal;

			if (file.git.lfs)
			{
				lfsPointer = WriteLFSFile(svnFile, file.git.repo);
				outputFile = lfsPointer;
			}

			std::string symlinkPath;
			if (file.svn->isSymlink)
			{
				// svn symlinks are in the format "link path/to/target"
				static const RE2 pattern("link (.*)");
				RE2::FullMatch(svnFile, pattern, &symlinkPath);

				mode = Mode::Symlink;
				outputFile = symlinkPath;
			}

			fmt::format_to(
				outputIt,
				"M {} inline {}\n"
				"data {}\n"
				"{}\n",
				static_cast<int>(mode), file.git.path, outputFile.size(), outputFile
			);
		}
		mWriter.WriteToFastImport(repo, output);
	}

	return {};
}
