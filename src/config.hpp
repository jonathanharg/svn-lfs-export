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
	std::unique_ptr<RE2> svn_path;
	std::optional<RepoBranch> repo_branch;
	std::string git_path;
	std::optional<long int> min_revision;
	std::optional<long int> max_revision;
};

struct Config
{
	bool create_base_commit = false;
	bool strict_mode = false;
	std::optional<long int> min_revision;
	std::optional<long int> max_revision;
	std::string svn_repository;
	std::optional<std::string> override_domain;
	std::string time_zone;
	std::string commit_message_template;
	std::vector<Rule> rules;
	std::vector<std::unique_ptr<RE2>> lfs_rules;
	std::unordered_map<std::string, std::string> identity_map;

	[[nodiscard]] bool is_valid() const;
	static std::optional<Config> from_file(const std::string_view&);
};
