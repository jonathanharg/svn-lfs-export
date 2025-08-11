#pragma once
#include <expected>
#include <memory>
#include <optional>
#include <re2/re2.h>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <unordered_map>
#include <vector>

struct Rule
{
	bool skipRevision;
	std::unique_ptr<RE2> svnPath;
	std::string repo;
	std::string branch;
	std::string gitPath;
	std::optional<long int> minRev;
	std::optional<long int> maxRev;
};

struct Config
{
	static std::expected<Config, std::string> Parse(const toml::table& root);
	static std::expected<Config, std::string> FromFile(const std::string_view&);

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

private:
	[[nodiscard]] std::expected<void, std::string> IsValid() const;

	static constexpr bool kDefaultCreateBaseCommit = false;
	static constexpr bool kDefaultStrictMode = false;
	static constexpr std::string_view kDefaultTimeZone = "Etc/UTC";
	static constexpr std::string_view kDefaultCommitMessage =
		"{log}\n\nThis commit was converted from revision r{rev} by svn-lfs-export.";
};
