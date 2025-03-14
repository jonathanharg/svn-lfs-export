#include "argparse/argparse.hpp"
#include "fmt/base.h"
#include "re2/re2.h"
#include "toml++/toml.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>

struct Rule
{
	std::unique_ptr<RE2> svn_path;
	std::string repository;
	std::string branch;
	std::string git_path;
	std::optional<long int> min_revision;
	std::optional<long int> max_revision;
};

int main(int argc, char* argv[])
{
	argparse::ArgumentParser program("svn-lfs-export");
	const toml::parse_result config_result = toml::parse_file("config.toml");

	if (!config_result)
	{
		std::cerr << "ERROR: Failed to parse config.toml - " << config_result.error()
			  << '\n';
		return 1;
	}

	const toml::table& config = config_result.table();

	const auto svn_repository = config["svn_repository"].value<std::string>();
	const auto min_revision = config["min_revision"].value<long int>();
	const auto max_revision = config["max_revision"].value<long int>();
	const auto override_domain = config["domain"].value<std::string>();
	const auto create_base_commit = config["create_base_commit"].value_or(false);
	const auto strict_mode = config["strict_mode"].value_or(false);
	const auto commit_message_template =
		config["commit_message"].value_or("{original_message}\n\nThis commit was converted "
						  "from revision r{revision} by svn-lfs-export.\n");

	if (!svn_repository)
	{
		std::cerr << "ERROR: Failed to parse the SVN repository string. Make sure a valid "
			     "path to a on-disk SVN repository is provided.\n";
		return 1;
	}

	if (!std::filesystem::is_directory(*svn_repository))
	{
		std::cerr << "ERROR: Repository path \"" << *svn_repository
			  << "\" is not a directory that can be found.\n";
		return 1;
	}

	const toml::table* identity_table = config["identity_map"].as_table();
	std::unordered_map<std::string, std::string> identity_map;

	if (identity_table)
	{
		const RE2 valid_name_re(R"(^([^\n<>]+\ )*<[^<>\n]+>$)");
		for (auto&& [key, value] : *identity_table)
		{
			const auto git_identity = value.value<std::string>();
			if (!git_identity || !RE2::FullMatch(*git_identity, valid_name_re))
			{
				std::cerr << "ERROR: Git identity for SVN user \"" << key.str()
					  << "\" should be in the format \"Firstname Lastname "
					     "<email@domain.com>\"\n";
				return 1;
			}
			identity_map[std::string(key.str())] = *git_identity;
		}
	}

	if (identity_map.size() == 0 && !override_domain)
	{
		std::cerr << "ERROR: Please provide an identity map or a domain.\n";
		return 1;
	}

	if (identity_map.size() == 0)
	{
		std::cerr << "WARNING: No identity map provided. Git author information will be "
			     "inaccurate.\n";
	}

	if (!override_domain)
	{
		std::cerr << "WARNING: No domain provided. Any SVN users not present in the "
			     "identity map will cause the program to terminate with an error.\n";
	}

	const toml::array* lfs_config = config["LFS"].as_array();
	std::vector<std::unique_ptr<RE2>> lfs_rules;

	if (lfs_config)
	{
		for (const toml::node& rule : *lfs_config)
		{
			const auto expression = rule.value<std::string_view>();
			if (!expression)
			{
				std::cerr << "ERROR: LFS must be defined as an array of regular "
					     "expressions.\n";
				return 1;
			}

			lfs_rules.emplace_back(std::make_unique<RE2>(*expression));

			if (!lfs_rules.back()->ok())
			{
				std::cerr << "ERROR: LFS regex is not valid: "
					  << lfs_rules.back()->error() << "\n";
				return 1;
			}
		}
	}

	std::vector<Rule> rules;
	const toml::array* rules_config = config["rule"].as_array();

	if (!rules_config || rules_config->size() == 0)
	{
		std::cerr << "ERROR: Expected rules to be an array of tables defined using "
			     "multiple [[rule]] statements.\n";
		return 1;
	}

	for (const toml::node& rule : *rules_config)
	{
		if (!rule.as_table())
		{
			std::cerr << "ERROR: Expected rules to be an array of tables defined using "
				     "multiple [[rule]] statements.\n";
			return 1;
		}
		const toml::table& table = *rule.as_table();

		const auto svn_path = table["svn_path"].value<std::string_view>();
		const auto repository = table["repository"].value<std::string>();
		const auto branch = table["branch"].value<std::string>();
		const std::string git_path = table["git_path"].value_or("");

		if (!svn_path || !repository || !branch)
		{
			std::cerr << "ERROR: Provide svn_path, repository and branch for each "
				     "rule.\n";
			return 1;
		}

		const auto min_revision = table["min_revision"].value<long int>();
		const auto max_revision = table["max_revision"].value<long int>();

		rules.emplace_back(std::make_unique<RE2>(*svn_path), *repository, *branch, git_path,
				   min_revision, max_revision);

		if (!rules.back().svn_path->ok())
		{
			std::cerr << "ERROR: SVN path regex is not valid: "
				  << rules.back().svn_path->error() << "\n";
			return 1;
		}
	}

	for (std::string input_line; std::getline(std::cin, input_line);)
	{
		long int input_revision = 0;
		std::string_view input_path;

		if (!RE2::FullMatch(input_line, R"(([0-9]+)\s+(.*))", &input_revision, &input_path))
		{
			std::cerr << "ERROR: Input test path does not match the revision path "
				     "format \"1234 your/path\".\n";
			return 1;
		}

		for (const Rule& rule : rules)
		{
			// Given a RULE, takes an INPUT REVISION and INPUT SVN PATH as input
			// 1. If not MIN REVISION <= INPUT REVISION <= MAX REVISION continue
			//    to next rule
			// 2. If not INPUT SVN PATH starts with and matches against
			//    RULE SVN PATH continue, recording any non-captured suffix
			// 3. If no REPOSITORY ignore path, break and skip all other rules
			// 4. Rewrite GIT REPO with substitutions from SVN PATH match
			// 5. Rewrite BRANCH with substitutions from SVN PATH match
			// 6. Rewrite GIT PATH with substitutions from SVN PATH match
			// 7. Append GIT PATH with the non-captured suffix being careful of
			//    duplicate path separators (e.g. //)
			// 8. Check if GIT PATH full matches with a rule in LFS RULES
			// 9. Output GIT REPO, GIT BRANCH and a GIT PATH

			if (rule.min_revision && *rule.min_revision > input_revision)
			{
				// TODO: Add verbose logging
				continue;
			}
			if (rule.max_revision && *rule.max_revision < input_revision)
			{
				// TODO: Add verbose logging
				continue;
			}
		}
	}

	return 0;
}
