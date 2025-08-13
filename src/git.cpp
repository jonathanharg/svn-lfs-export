#include "git.hpp"
#include "config.hpp"
#include <chrono>
#include <date/date.h>
#include <date/tz.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/std.h>
#include <string>

namespace git
{

std::string GetGitAuthor(const Config& config, const std::string& username)
{
	const std::string& domain = config.overrideDomain.value_or("localhost");

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

std::string GetCommitMessage(const Config& config, const std::string& log, const std::string& username, long int rev)
{
	return fmt::format(fmt::runtime(config.commitMessage), fmt::arg("log", log), fmt::arg("usr", username),
					   fmt::arg("rev", rev));
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

	auto unixEpoch = std::chrono::duration_cast<std::chrono::seconds>(utcTime.time_since_epoch()).count();
	return fmt::format("{} {}", unixEpoch, formattedOffset);
}

std::optional<OutputLocation> MapPathToOutput(const Config& config, const long int rev, const std::string_view& path)
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
			break;
		}

		OutputLocation result;

		rule.svnPath->Rewrite(&result.repo, rule.gitRepository, capturesStrings.data(), captureGroupsWith0th);

		rule.svnPath->Rewrite(&result.branch, rule.gitBranch, capturesStrings.data(), captureGroupsWith0th);

		rule.svnPath->Rewrite(&result.path, rule.gitFilePath, capturesStrings.data(), captureGroupsWith0th);

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

} // namespace git
