#include "config.hpp"
#include <date/tz.h>
#include <filesystem>
#include <fmt/base.h>
#include <fmt/ostream.h>
#include <iostream>
#include <re2/re2.h>
#include <toml++/toml.hpp>
#include <utility>

template <typename... T>
inline void log_error(fmt::format_string<T...> fmt, T&&... args)
{
	fmt::println(std::cerr, fmt, std::forward<T>(args)...);
}

std::optional<Config> Config::from_file(const std::string_view& path)
{
	const toml::parse_result file_read = toml::parse_file(path);

	if (!file_read)
	{
		log_error("ERROR: Failed to parse config.toml - {}",
			  file_read.error().description());
		return std::nullopt;
	}

	const toml::table& root = file_read.table();

	Config result;
	result.min_revision = root["min_revision"].value<long int>();
	result.max_revision = root["max_revision"].value<long int>();
	result.override_domain = root["domain"].value<std::string>();
	result.create_base_commit = root["create_base_commit"].value_or(false);
	result.strict_mode = root["strict_mode"].value_or(false);
	result.time_zone = root["time_zone"].value_or("Etc/UTC");
	result.commit_message_template = root["commit_message"].value_or(
		"{log}\n\nThis commit was converted from revision r{rev} by svn-lfs-export.");

	const auto repository_value = root["svn_repository"].value<std::string>();

	if (!repository_value)
	{
		log_error("ERROR: Failed to parse the SVN repository string. Make sure a valid "
			  "path to a on-disk SVN repository is provided.");
		return std::nullopt;
	}

	result.svn_repository = *repository_value;

	const toml::table* identity_table = root["identity_map"].as_table();

	if (identity_table)
	{
		for (auto&& [key, value] : *identity_table)
		{
			const auto git_identity = value.value<std::string>();
			if (!git_identity)
			{
				log_error("ERROR: Git identity for SVN user {:?} is invalid.",
					  key.str());
				return std::nullopt;
			}
			result.identity_map[std::string(key.str())] = *git_identity;
		}
	}

	const toml::array* lfs_config = root["LFS"].as_array();

	if (lfs_config)
	{
		for (const toml::node& rule : *lfs_config)
		{
			const auto expression = rule.value<std::string_view>();
			if (!expression)
			{
				log_error("ERROR: LFS must be defined as an array of regular "
					  "expressions.");
				return std::nullopt;
			}

			result.lfs_rules.emplace_back(std::make_unique<RE2>(*expression));
		}
	}

	const toml::array* rules_config = root["rule"].as_array();

	if (!rules_config)
	{
		log_error("ERROR: Expected rules to be an array of tables defined using one or "
			  "more [[rule]] statements.");
		return std::nullopt;
	}

	for (const toml::node& rule : *rules_config)
	{
		if (!rule.as_table())
		{
			log_error("ERROR: Expected rules to be an array of tables defined using "
				  "one or more [[rule]] statements.");
			return std::nullopt;
		}
		const toml::table& table = *rule.as_table();

		const auto svn_path = table["svn_path"].value<std::string_view>();
		const auto repository = table["repository"].value<std::string>();
		const auto branch = table["branch"].value<std::string>();
		const std::string git_path = table["git_path"].value_or("");

		if (!svn_path)
		{
			log_error("ERROR: Provide an svn_path for each rule.");
			return std::nullopt;
		}
		if (repository.has_value() != branch.has_value())
		{
			log_error("ERROR: For {} both a repository and a branch must be provided, "
				  "or neither should be provided .",
				  *svn_path);
			return std::nullopt;
		}

		std::optional<Rule::RepoBranch> repo_branch = {};
		if (repository && branch)
		{
			repo_branch = Rule::RepoBranch{*repository, *branch};
		}

		const auto min_revision = table["min_revision"].value<long int>();
		const auto max_revision = table["max_revision"].value<long int>();

		result.rules.emplace_back(std::make_unique<RE2>(*svn_path), repo_branch, git_path,
					  min_revision, max_revision);
	}

	return result;
}

bool Config::is_valid() const
{
	if (!std::filesystem::is_directory(svn_repository))
	{
		log_error("ERROR: Repository path {:?} is not a directory that can be found.",
			  svn_repository);
		return false;
	}

	try
	{
		(void)fmt::format(fmt::runtime(commit_message_template), fmt::arg("log", "log msg"),
				  fmt::arg("usr", "sean"), fmt::arg("rev", 1));
	}
	catch (fmt::v11::format_error& err)
	{
		log_error("ERROR: Invalid commit_message template (fmtlib error {:?}).",
			  err.what());
		return false;
	}

	static const RE2 valid_name_re(R"(^([^\n<>]+\ )*<[^<>\n]+>$)");
	for (auto&& [key, value] : identity_map)
	{
		if (!RE2::FullMatch(value, valid_name_re))
		{
			log_error("ERROR: Git identity for SVN user {:?} should be in the format "
				  "\"Firstname Lastname <email@domain.com>\"",
				  key);
			return false;
		}
	}

	if (identity_map.size() == 0 && !override_domain)
	{
		log_error("ERROR: Please provide an identity map or a domain.");
		return false;
	}

	if (identity_map.size() == 0)
	{
		log_error("WARNING: No identity map provided. Git author information will be "
			  "inaccurate.");
	}

	try
	{
		date::locate_zone(time_zone);
	}
	catch (const std::exception& e)
	{
		log_error("ERROR: Timezone {:?} is not valid.", time_zone);
		return false;
	}

	if (!override_domain)
	{
		log_error("WARNING: No domain provided. Any SVN users not present in the identity "
			  "map will cause the program to terminate with an error.");
	}

	for (const auto& rule : lfs_rules)
	{
		if (!rule->ok())
		{
			log_error("ERROR: LFS regex {:?} is not valid: {}", rule->pattern(),
				  rule->error());
			return false;
		}
	}

	if (rules.size() == 0)
	{
		log_error("ERROR: Provide one or more rules.");
		return false;
	}

	for (const auto& rule : rules)
	{
		if (!rule.svn_path->ok())
		{
			log_error("ERROR: SVN path {:?} is not valid: {}", rule.svn_path->pattern(),
				  rule.svn_path->error());
			return false;
		}

		std::string error;
		static constexpr const char* error_message =
			R"(ERROR: Could not rewrite "{}" with the regex "{}" - {})";

		if (rule.repo_branch &&
		    !rule.svn_path->CheckRewriteString(rule.repo_branch->repository, &error))
		{
			log_error(error_message, rule.repo_branch->repository,
				  rule.svn_path->pattern(), error);
			return false;
		}
		if (rule.repo_branch &&
		    !rule.svn_path->CheckRewriteString(rule.repo_branch->repository, &error))
		{
			log_error(error_message, rule.repo_branch->repository,
				  rule.svn_path->pattern(), error);
			return false;
		}
		if (!rule.svn_path->CheckRewriteString(rule.git_path, &error))
		{
			log_error(error_message, rule.git_path, rule.svn_path->pattern(), error);
			return false;
		}
	}
	return true;
}
