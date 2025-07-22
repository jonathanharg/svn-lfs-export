#include "config.hpp"
#include "svn.hpp"
#include "utils.hpp"
#include <apr_general.h>
#include <apr_hash.h>
#include <apr_pools.h>
#include <argparse/argparse.hpp>
#include <chrono>
#include <cstdlib>
#include <date/date.h>
#include <date/tz.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/std.h>
#include <iostream>
#include <memory>
#include <optional>
#include <re2/re2.h>
#include <sstream>
#include <string>
#include <string_view>
#include <svn_pools.h>
#include <svn_props.h>
#include <svn_repos.h>
#include <vector>

constexpr std::array<std::string_view, 5> kPathChangeStrings = {
	"Modified", "Add", "Delete", "Replace", "Reset" /* Unused */
};

constexpr std::array<std::string_view, 5> kNodeKindStrings = {
	"None", "File", "Directory", "Unknown", "Symlink" /* Unused */
};

enum class GitMode
{
	Normal = 100644,
	Executable = 100755,
	// Symlink = 120000,
	// GitLink = 160000,
	// Subdirectory = 040000,
};

struct OutputLocation
{
	std::string repo;
	std::string branch;
	std::string path;
	bool lfs = false;
};

std::optional<std::string> GetProp(apr_hash_t* hash, const char* prop)
{
	auto* value = static_cast<svn_string_t*>(apr_hash_get(hash, prop, APR_HASH_KEY_STRING));
	if (value)
	{
		return std::string(value->data, value->len);
	}
	return {};
};

std::optional<OutputLocation> MapPathToOutput(const Config& config, const long int rev,
					      const std::string_view& path)
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
		// 7. Append GIT PATH with the non-captured suffix being careful of
		//    duplicate path separators (e.g. //)
		//    [NOTE. This is now a user skill issue]
		// 8. Check if GIT PATH full matches with a rule in LFS RULES
		// 9. Output GIT REPO, GIT BRANCH, a GIT PATH, and if LFS

		if (rule.minRev && *rule.minRev > rev)
		{
			continue;
		}
		if (rule.maxRev && *rule.maxRev < rev)
		{
			continue;
		}

		int capturesGroups = rule.svnPath->NumberOfCapturingGroups();

		std::vector<std::string_view> capturesStrings(capturesGroups);

		std::vector<RE2::Arg> args(capturesGroups);
		std::vector<RE2::Arg*> argPtrs(capturesGroups);

		for (int i = 0; i < capturesGroups; ++i)
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
		capturesStrings.emplace(capturesStrings.begin(), wholeCaptureBegin,
					wholeCaptureEnd);

		if (!rule.repoBranch)
		{
			break;
		}

		OutputLocation result;

		rule.svnPath->Rewrite(&result.repo, rule.repoBranch->repo, capturesStrings.data(),
				      captureGroupsWith0th);

		rule.svnPath->Rewrite(&result.branch, rule.repoBranch->branch,
				      capturesStrings.data(), captureGroupsWith0th);

		rule.svnPath->Rewrite(&result.path, rule.gitPath, capturesStrings.data(),
				      captureGroupsWith0th);

		// Append any of the non-captured SVN path to the output git path
		result.path.append(consumedPtr);

		for (const auto& lfsRule : config.lfsRules)
		{
			if (RE2::FullMatch(result.path, *lfsRule))
			{
				result.lfs = true;
				break;
			}
		}

		return result;
	}
	return std::nullopt;
}

std::string GetGitAuthor(const Config& config, const std::optional<std::string>& username)
{
	const std::string& domain = config.overrideDomain.value_or("localhost");

	if (!username.has_value())
	{
		return fmt::format("Unknown User <unknown@{}>", domain);
	}
	if (config.identityMap.contains(*username))
	{
		return config.identityMap.at(*username);
	}

	return fmt::format("{} <{}@{}>", *username, *username, domain);
}

std::string GetCommitMessage(const Config& config, const std::string& log,
			     const std::string& username, long int rev)
{
	return fmt::format(fmt::runtime(config.commitMessage), fmt::arg("log", log),
			   fmt::arg("usr", username), fmt::arg("rev", rev));
}

std::string GetGitTime(const Config& config, const std::string& svnTime)
{
	// It looks like SVN stores dates in UTC time
	// https://svn.haxx.se/users/archive-2003-09/0322.shtml
	// This is good, because we don't have to mess with time zones when converting
	// to Unix Epoch time (which git uses). We might however, want to apply a local
	// UTC offset based on the location of the server.

	std::istringstream dateStream{svnTime};
	std::chrono::sys_time<std::chrono::milliseconds> utcTime;
	dateStream >> date::parse("%FT%T%Ez", utcTime);

	// I'm 90% sure SVN stores UTC with 6 decimal places / microseconds
	// But add a fail-safe to parse 0 decimal places / seconds
	if (dateStream.fail())
	{
		dateStream.clear();
		dateStream.exceptions(std::ios::failbit);
		dateStream.str(svnTime);
		dateStream >> date::parse("%FT%TZ", utcTime);
	}
	static const date::time_zone* tz = date::get_tzdb().locate_zone(config.timezone);

	date::zoned_time<std::chrono::milliseconds> zonedTime{tz, utcTime};
	std::string formattedOffset = date::format("%z", zonedTime);

	auto unixEpoch =
		std::chrono::duration_cast<std::chrono::seconds>(utcTime.time_since_epoch())
			.count();
	return fmt::format("{} {}", unixEpoch, formattedOffset);
}

int main()
{
	argparse::ArgumentParser program("svn-lfs-export");
	const auto maybeConfig = Config::FromFile("config.toml");

	if (!maybeConfig)
	{
		std::cerr << maybeConfig.error();
		return EXIT_FAILURE;
	}

	const Config& config = maybeConfig.value();

	apr_initialize();
	SVNPool root;
	SVNPool scratch;
	svn_repos_t* repository = nullptr;
	svn_error_t* err = nullptr;

	err = svn_repos_open3(&repository, config.svnRepo.c_str(), nullptr, root, scratch);
	SVN_INT_ERR(err);

	svn_fs_t* fs = svn_repos_fs(repository);
	if (!fs)
	{
		std::cerr << "ERROR: SVN failed to open fs.";
		return EXIT_FAILURE;
	}

	long int youngestRev = 1;
	err = svn_fs_youngest_rev(&youngestRev, fs, scratch);
	SVN_INT_ERR(err);

	long int startRev = config.minRev.value_or(1);
	long int stopRev = config.maxRev.value_or(youngestRev);

	LogInfo("Running from r{} to r{}", startRev, stopRev);

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

	for (long int rev = startRev; rev <= stopRev; rev++)
	{
		LogInfo("Converting r{}", rev);
		SVNPool revPool;

		svn_fs_root_t* revFs = nullptr;
		err = svn_fs_revision_root(&revFs, fs, rev, revPool);
		SVN_INT_ERR(err);

		apr_hash_t* revProps = nullptr;
		err = svn_fs_revision_proplist2(&revProps, fs, rev, false, revPool, scratch);
		SVN_INT_ERR(err);

		static constexpr const char* epoch = "1970-01-01T00:00:00Z";

		auto authorProp = GetProp(revProps, SVN_PROP_REVISION_AUTHOR);
		auto logProp = GetProp(revProps, SVN_PROP_REVISION_LOG).value_or("");
		auto dateProp = GetProp(revProps, SVN_PROP_REVISION_DATE).value_or(epoch);

		std::string gitAuthor = GetGitAuthor(config, authorProp);
		std::string gitMessage =
			GetCommitMessage(config, logProp, authorProp.value_or("unknown"), rev);
		std::string gitTime = GetGitTime(config, dateProp);

		svn_fs_path_change_iterator_t* it = nullptr;
		err = svn_fs_paths_changed3(&it, revFs, revPool, scratch);
		SVN_INT_ERR(err);

		svn_fs_path_change3_t* changes = nullptr;
		err = svn_fs_path_change_get(&changes, it);
		SVN_INT_ERR(err);

		// TODO: Make the commit after first gathering files
		std::string ref = "refs/heads/main";
		Output("commit {}", ref);
		Output("committer {} {}", gitAuthor, gitTime);
		Output("data {}\n{}", gitMessage.length(), gitMessage);

		while (changes)
		{
			std::string path{changes->path.data, changes->path.len};
			std::string_view changeKind = kPathChangeStrings.at(changes->change_kind);
			std::string_view nodeKind = kNodeKindStrings.at(changes->node_kind);

			bool textMod = changes->text_mod;
			bool propMod = changes->prop_mod;

			// TODO: Get Copy from and mergeinfo
			// => Copy from might help performance / eliminate unnecessary duplication
			// => merge info might help us create branches

			std::optional<OutputLocation> destination =
				MapPathToOutput(config, rev, path);

			LogError("SVN {} {}: {:?} (mod text {}, props {})", changeKind, nodeKind,
				 path, textMod, propMod);

			if (destination)
			{
				LogError("-> {}/{} {} (LFS {})", destination->repo,
					 destination->branch, destination->path, destination->lfs);
			}

			// TODO: are we sure we can skip over directories here?
			if (changes->node_kind != svn_node_kind_t::svn_node_file)
			{
				err = svn_fs_path_change_get(&changes, it);
				SVN_INT_ERR(err);
				continue;
			}

			svn_stream_t* content = nullptr;
			// TODO: Should probably free these pools early?
			// or have a file level pool
			err = svn_fs_file_contents(&content, revFs, path.c_str(), revPool);
			SVN_INT_ERR(err);

			svn_filesize_t fileSize = 0;
			// TODO: I'm not sure if we want/need to be doing this
			err = svn_fs_file_length(&fileSize, revFs, path.c_str(), revPool);
			SVN_INT_ERR(err);

			std::vector<char> buffer(fileSize);

			// WARNING: This will probably overflow
			apr_size_t readSize = fileSize;
			err = svn_stream_read_full(content, buffer.data(), &readSize);

			std::string_view file{buffer.data(), static_cast<size_t>(fileSize)};

			Output("M {} inline {}", static_cast<int>(GitMode::Normal),
			       destination->path.c_str());
			Output("data {}\n{}", file.length(), file);

			err = svn_fs_path_change_get(&changes, it);
			SVN_INT_ERR(err);
		}

		scratch.clear();
	}
}
