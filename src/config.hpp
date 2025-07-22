#pragma once
#include <memory>
#include <optional>
#include <re2/re2.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct Rule
{
	using RepoBranch = struct
	{
		std::string repository;
		std::string branch;
	};
	std::unique_ptr<RE2> svnPath;
	std::optional<RepoBranch> repoBranch;
	std::string gitPath;
	std::optional<long int> minRevision;
	std::optional<long int> maxRevision;
};

struct Config
{
	bool createBaseCommit = false;
	bool strictMode = false;
	std::optional<long int> minRev;
	std::optional<long int> maxRev;
	std::string svnRepo;
	std::optional<std::string> overrideDomain;
	std::string timezone;
	std::string commitMessage;
	std::vector<Rule> rules;
	std::vector<std::unique_ptr<RE2>> lfsRules;
	std::unordered_map<std::string, std::string> identityMap;

	[[nodiscard]] bool IsValid() const;
	static std::optional<Config> FromFile(const std::string_view&);

private:
	static constexpr bool kDefaultCreateBaseCommit = false;
	static constexpr bool kDefaultStrictMode = false;
	static constexpr std::string_view kDefaultTimeZone = "Etc/UTC";
	static constexpr std::string_view kDefaultCommitMessage =
		"{log}\n\nThis commit was converted from revision r{rev} by svn-lfs-export.";
};
