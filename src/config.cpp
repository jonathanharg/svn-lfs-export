#include "config.hpp"
#include "utils.hpp"
#include <date/tz.h>
#include <filesystem>
#include <fmt/base.h>
#include <fmt/ostream.h>
#include <iostream>
#include <re2/re2.h>
#include <toml++/toml.hpp>
#include <utility>

std::optional<Config> Config::FromFile(const std::string_view& path)
{
	const toml::parse_result fileRead = toml::parse_file(path);

	if (!fileRead)
	{
		LogError("ERROR: Failed to parse config.toml - {}", fileRead.error().description());
		return std::nullopt;
	}

	const toml::table& root = fileRead.table();

	Config result;
	result.minRev = root["min_revision"].value<long int>();
	result.maxRev = root["max_revision"].value<long int>();
	result.overrideDomain = root["domain"].value<std::string>();
	result.createBaseCommit = root["create_base_commit"].value_or(kDefaultCreateBaseCommit);
	result.strictMode = root["strict_mode"].value_or(kDefaultStrictMode);
	result.timezone = root["time_zone"].value_or(kDefaultTimeZone);
	result.commitMessage = root["commit_message"].value_or(kDefaultCommitMessage);

	const auto repositoryValue = root["svn_repository"].value<std::string>();

	if (!repositoryValue)
	{
		LogError("ERROR: Failed to parse the SVN repository string. Make sure a valid "
			 "path to a on-disk SVN repository is provided.");
		return std::nullopt;
	}

	result.svnRepo = *repositoryValue;

	const toml::table* identityTable = root["identity_map"].as_table();

	if (identityTable)
	{
		for (auto&& [key, value] : *identityTable)
		{
			const auto gitIdentity = value.value<std::string>();
			if (!gitIdentity)
			{
				LogError("ERROR: Git identity for SVN user {:?} is invalid.",
					 key.str());
				return std::nullopt;
			}
			result.identityMap[std::string(key.str())] = *gitIdentity;
		}
	}

	const toml::array* lfsConfig = root["LFS"].as_array();

	if (lfsConfig)
	{
		for (const toml::node& rule : *lfsConfig)
		{
			const auto expression = rule.value<std::string_view>();
			if (!expression)
			{
				LogError("ERROR: LFS must be defined as an array of regular "
					 "expressions.");
				return std::nullopt;
			}

			result.lfsRules.emplace_back(std::make_unique<RE2>(*expression));
		}
	}

	const toml::array* rulesConfig = root["rule"].as_array();

	if (!rulesConfig)
	{
		LogError("ERROR: Expected rules to be an array of tables defined using one or "
			 "more [[rule]] statements.");
		return std::nullopt;
	}

	for (const toml::node& rule : *rulesConfig)
	{
		if (!rule.as_table())
		{
			LogError("ERROR: Expected rules to be an array of tables defined using "
				 "one or more [[rule]] statements.");
			return std::nullopt;
		}
		const toml::table& table = *rule.as_table();

		const auto svnPath = table["svn_path"].value<std::string_view>();
		const auto repository = table["repository"].value<std::string>();
		const auto branch = table["branch"].value<std::string>();
		const std::string gitPath = table["git_path"].value_or("");

		if (!svnPath)
		{
			LogError("ERROR: Provide an svn_path for each rule.");
			return std::nullopt;
		}
		if (repository.has_value() != branch.has_value())
		{
			LogError("ERROR: For {} both a repository and a branch must be provided, "
				 "or neither should be provided .",
				 *svnPath);
			return std::nullopt;
		}

		std::optional<Rule::RepoBranch> repoBranch = {};
		if (repository && branch)
		{
			repoBranch = Rule::RepoBranch{*repository, *branch};
		}

		const auto minRev = table["min_revision"].value<long int>();
		const auto maxRev = table["max_revision"].value<long int>();

		result.rules.emplace_back(std::make_unique<RE2>(*svnPath), repoBranch, gitPath,
					  minRev, maxRev);
	}

	return result;
}

bool Config::IsValid() const
{
	if (!std::filesystem::is_directory(svnRepo))
	{
		LogError("ERROR: Repository path {:?} is not a directory that can be found.",
			 svnRepo);
		return false;
	}

	try
	{
		(void)fmt::format(fmt::runtime(commitMessage), fmt::arg("log", "log msg"),
				  fmt::arg("usr", "sean"), fmt::arg("rev", 1));
	}
	catch (fmt::v11::format_error& err)
	{
		LogError("ERROR: Invalid commit_message template (fmtlib error {:?}).", err.what());
		return false;
	}

	static const RE2 kValidNameRe(R"(^([^\n<>]+\ )*<[^<>\n]+>$)");
	for (auto&& [key, value] : identityMap)
	{
		if (!RE2::FullMatch(value, kValidNameRe))
		{
			LogError("ERROR: Git identity for SVN user {:?} should be in the format "
				 "\"Firstname Lastname <email@domain.com>\"",
				 key);
			return false;
		}
	}

	if (identityMap.size() == 0 && !overrideDomain)
	{
		LogError("ERROR: Please provide an identity map or a domain.");
		return false;
	}

	if (identityMap.size() == 0)
	{
		LogError("WARNING: No identity map provided. Git author information will be "
			 "inaccurate.");
	}

	try
	{
		date::locate_zone(timezone);
	}
	catch (const std::exception& e)
	{
		LogError("ERROR: Timezone {:?} is not valid.", timezone);
		return false;
	}

	if (!overrideDomain)
	{
		LogError("WARNING: No domain provided. Any SVN users not present in the identity "
			 "map will cause the program to terminate with an error.");
	}

	for (const auto& rule : lfsRules)
	{
		if (!rule->ok())
		{
			LogError("ERROR: LFS regex {:?} is not valid: {}", rule->pattern(),
				 rule->error());
			return false;
		}
	}

	if (rules.size() == 0)
	{
		LogError("ERROR: Provide one or more rules.");
		return false;
	}

	for (const auto& rule : rules)
	{
		if (!rule.svnPath->ok())
		{
			LogError("ERROR: SVN path {:?} is not valid: {}", rule.svnPath->pattern(),
				 rule.svnPath->error());
			return false;
		}

		std::string error;
		static constexpr const char* errorMsg =
			R"(ERROR: Could not rewrite "{}" with the regex "{}" - {})";

		if (rule.repoBranch &&
		    !rule.svnPath->CheckRewriteString(rule.repoBranch->repository, &error))
		{
			LogError(errorMsg, rule.repoBranch->repository, rule.svnPath->pattern(),
				 error);
			return false;
		}
		if (rule.repoBranch &&
		    !rule.svnPath->CheckRewriteString(rule.repoBranch->repository, &error))
		{
			LogError(errorMsg, rule.repoBranch->repository, rule.svnPath->pattern(),
				 error);
			return false;
		}
		if (!rule.svnPath->CheckRewriteString(rule.gitPath, &error))
		{
			LogError(errorMsg, rule.gitPath, rule.svnPath->pattern(), error);
			return false;
		}
	}
	return true;
}
