#pragma once
#include <git2.h>
#include <git2/pathspec.h>
#include <re2/re2.h>
#include <toml++/toml.hpp>

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// A rule to map SVN revisions to git commits. The rules to apply to all files in all commits.
struct Rule
{
	/// Ignore this revision, implies gitRepo and gitBranch are empty.
	bool skipRevision;
	/// Regular expression to match input SVN path
	std::unique_ptr<RE2> svnPath;
	/// Output git repository
	std::string gitRepository;
	/// Output git branch
	std::string gitBranch;
	/// Output git file location. Can be empty. To be used as a prefix for files.
	std::string gitFilePath;
	/// Minimum revision the rule applies to.
	std::optional<long int> minRevision;
	/// Maximum revision the rule applies to.
	std::optional<long int> maxRevision;
};

struct Config
{
	using pathspec_ptr =
		std::unique_ptr<git_pathspec, decltype([](git_pathspec* ps) { git_pathspec_free(ps); })>;

	Config() :
		createBaseCommit(kDefaultCreateBaseCommit),
		strictMode(kDefaultStrictMode),
		timezone(kDefaultTimeZone),
		commitMessage(kDefaultCommitMessage) {}

	static std::expected<Config, std::string> Parse(const toml::table& root);
	static std::expected<Config, std::string> FromFile(const std::string_view&);

	bool createBaseCommit;
	bool strictMode;
	std::optional<long int> minRevision;
	std::optional<long int> maxRevision;
	std::string svnRepo;
	std::optional<std::string> domain;
	std::string timezone;
	std::string commitMessage;
	std::vector<Rule> rules;
	std::vector<std::string> lfsRuleStrs;
	pathspec_ptr lfsPathspec;
	std::unordered_map<std::string, std::string> identityMap;

private:
	std::expected<void, std::string> IsValid() const;

	static constexpr bool kDefaultCreateBaseCommit = false;
	static constexpr bool kDefaultStrictMode = false;
	static constexpr std::string_view kDefaultTimeZone = "Etc/UTC";
	static constexpr std::string_view kDefaultCommitMessage =
		"{log}\n\nThis commit was converted from revision r{rev} by svn-lfs-export.";
};
