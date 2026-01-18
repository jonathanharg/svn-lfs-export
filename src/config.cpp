#include "config.hpp"

#include <date/tz.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <git2.h>
#include <git2/errors.h>
#include <git2/pathspec.h>
#include <git2/strarray.h>
#include <re2/re2.h>
#include <toml++/toml.h>

#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

std::expected<Config, std::string> Config::FromFile(const std::string_view& path)
{
	const toml::parse_result fileRead = toml::parse_file(path);

	if (!fileRead)
	{
		return std::unexpected(
			fmt::format("ERROR: Failed to parse config.toml - {}", fileRead.error().description())
		);
	}

	const toml::table& root = fileRead.table();
	return Config::Parse(root);
}

std::expected<Config, std::string> Config::Parse(const toml::table& root)
{
	Config result;

	// Optional or default values
	result.domain = root["domain"].value<std::string>();
	result.strictMode = root["strict_mode"].value_or(kDefaultStrictMode);
	result.timezone = root["time_zone"].value_or(kDefaultTimeZone);
	result.commitMessage = root["commit_message"].value_or(kDefaultCommitMessage);

	const auto repositoryValue = root["svn_repository"].value<std::string>();

	if (!repositoryValue)
	{
		return std::unexpected(
			"ERROR: No SVN repository path, provide a path to a valid on-disk SVN repository."
		);
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
				return std::unexpected(
					fmt::format("ERROR: Git identity for SVN user {:?} is invalid.", key.str())
				);
			}
			result.identityMap[std::string(key.str())] = *gitIdentity;
		}
	}

	const toml::table* branchTable = root["branch_origin"].as_table();

	if (branchTable)
	{
		for (auto&& [key, value] : *branchTable)
		{
			const auto fromValue = value.value<std::string>();
			if (!fromValue)
			{
				return std::unexpected(
					fmt::format("ERROR: Branch mapping for {:?} is invalid.", key.str())
				);
			}
			// TODO: Add validators
			result.branchMap[std::string(key.str())] = *fromValue;
		}
	}

	const toml::array* lfsConfig = root["LFS"].as_array();

	if (lfsConfig)
	{
		for (const toml::node& rule : *lfsConfig)
		{
			const auto expression = rule.value<std::string>();
			if (!expression)
			{
				return std::unexpected("ERROR: LFS must be defined as an array of git pathspecs.");
			}

			result.lfsRuleStrs.push_back(*expression);
		}

		std::vector<char*> cPtrs;
		cPtrs.reserve(result.lfsRuleStrs.size());
		for (const std::string& rule : result.lfsRuleStrs)
		{
			cPtrs.push_back(const_cast<char*>(rule.data()));
		}

		const git_strarray paths{cPtrs.data(), cPtrs.size()};
		int err = git_pathspec_new(std::out_ptr(result.lfsPathspec), &paths);

		if (err)
		{
			std::string msg =
				fmt::format("ERROR: Could not compile pathspec: {}", git_error_last()->message);
			return std::unexpected(msg);
		}
	}

	const toml::array* rulesConfig = root["rule"].as_array();

	if (!rulesConfig)
	{
		return std::unexpected(
			"ERROR: Expected rules to be an array of tables defined using one or more [[rule]] statements."
		);
	}

	for (const toml::node& rule : *rulesConfig)
	{
		if (!rule.as_table())
		{
			return std::unexpected(
				"ERROR: Expected rules to be an array of tables defined using one or more [[rule]] statements."
			);
		}
		const toml::table& table = *rule.as_table();

		const auto svnPath = table["svn_path"].value<std::string_view>();
		const auto repository = table["repository"].value<std::string>();
		const auto branch = table["branch"].value<std::string>();
		const std::string gitPath = table["git_path"].value_or("");

		if (!svnPath)
		{
			return std::unexpected("ERROR: Provide an svn_path for each rule.");
		}
		if (repository.has_value() != branch.has_value())
		{
			return std::unexpected(
				fmt::format(
					"ERROR: For {} both a repository and a branch must be provided, or neither should be provided .",
					*svnPath
				)
			);
		}

		const bool ignore = !repository.has_value() || !branch.has_value();

		const auto minRev = table["min_revision"].value<long int>();
		const auto maxRev = table["max_revision"].value<long int>();

		result.rules.emplace_back(
			ignore, std::make_unique<RE2>(*svnPath), repository.value_or(""), branch.value_or(""),
			gitPath, minRev, maxRev
		);
	}

	auto valid = result.IsValid();
	if (!valid)
	{
		return std::unexpected(valid.error());
	}

	return result;
}

std::expected<void, std::string> Config::IsValid() const
{
	if (!std::filesystem::is_directory(svnRepo))
	{
		return std::unexpected(
			fmt::format(
				"ERROR: Repository path {:?} is not a directory that can be found.", svnRepo
			)
		);
	}

	try
	{
		(void)fmt::format(
			fmt::runtime(commitMessage), fmt::arg("log", "log msg"), fmt::arg("usr", "sean"),
			fmt::arg("rev", 1)
		);
	}
	catch (fmt::v12::format_error& err)
	{
		return std::unexpected(
			fmt::format("ERROR: Invalid commit_message template (fmtlib error {:?}).", err.what())
		);
	}

	static const RE2 kValidNameRe(R"(^([^\n<>]+\ )*<[^<>\n]+>$)");
	for (auto&& [key, value] : identityMap)
	{
		if (!RE2::FullMatch(value, kValidNameRe))
		{
			return std::unexpected(
				fmt::format(
					"ERROR: Git identity for SVN user {:?} should be in the format \"Firstname Lastname <email@domain.com>\"",
					key
				)
			);
		}
	}

	if (identityMap.size() == 0 && !domain)
	{
		return std::unexpected("ERROR: Please provide an identity map or a domain.");
	}

	if (identityMap.size() == 0)
	{
		return std::unexpected(
			"WARNING: No identity map provided. Git author information will be inaccurate."
		);
	}

	try
	{
		date::locate_zone(timezone);
	}
	catch (const std::exception& e)
	{
		return std::unexpected(
			fmt::format("ERROR: Timezone {:?} is not valid. {}", timezone, e.what())
		);
	}

	if (!domain)
	{
		return std::unexpected(
			"WARNING: No domain provided. Any SVN users not present in the identity map will cause the program to terminate with an error."
		);
	}

	if (rules.size() == 0)
	{
		return std::unexpected("ERROR: Provide one or more rules.");
	}

	for (const auto& rule : rules)
	{
		if (!rule.svnPath->ok())
		{
			return std::unexpected(
				fmt::format(
					"ERROR: SVN path {:?} is not valid: {}", rule.svnPath->pattern(),
					rule.svnPath->error()
				)
			);
		}

		if (rule.gitRepository.empty() != rule.gitBranch.empty())
		{
			return std::unexpected("ERROR: Provide an output repository and branch, or neither");
		}

		std::string error;
		static constexpr const char* errorMsg =
			"ERROR: Could not rewrite {:?} with the regex {:?} - {}";

		if (!rule.skipRevision && !rule.svnPath->CheckRewriteString(rule.gitRepository, &error))
		{
			return std::unexpected(
				fmt::format(errorMsg, rule.gitRepository, rule.svnPath->pattern(), error)
			);
		}
		if (!rule.skipRevision && !rule.svnPath->CheckRewriteString(rule.gitRepository, &error))
		{
			return std::unexpected(
				fmt::format(errorMsg, rule.gitRepository, rule.svnPath->pattern(), error)
			);
		}
		if (!rule.svnPath->CheckRewriteString(rule.gitFilePath, &error))
		{
			return std::unexpected(
				fmt::format(errorMsg, rule.gitFilePath, rule.svnPath->pattern(), error)
			);
		}
	}
	return {};
}
