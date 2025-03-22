#include "argparse/argparse.hpp"
#include "fmt/base.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"
#include "fmt/std.h"
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

template <typename... T>
inline void log_error(fmt::format_string<T...> fmt, T&&... args)
{
	fmt::println(std::cerr, fmt, std::forward<T>(args)...);
}

struct OutputLocation
{
	std::string repository;
	std::string branch;
	std::string path;
};

std::optional<OutputLocation> map_path_to_output(const std::vector<Rule>& rules,
						 const long int revision,
						 const std::string_view& path)
{
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
		// 9. Output GIT REPO, GIT BRANCH, a GIT PATH, and if LFS

		if (rule.min_revision && *rule.min_revision > revision)
		{
			continue;
		}
		if (rule.max_revision && *rule.max_revision < revision)
		{
			continue;
		}

		int captures_groups = rule.svn_path->NumberOfCapturingGroups();

		std::vector<std::string_view> captures_views(captures_groups);

		// Use two vectors so we can "spoof" a const Arg* const args[]
		// C-style array for ConsumeN that's dynamically sized based on
		// the number of captures in the provided regex.
		std::vector<RE2::Arg> args(captures_groups);
		std::vector<RE2::Arg*> arg_ptrs(captures_groups);

		for (int i = 0; i < captures_groups; ++i)
		{
			args[i] = RE2::Arg(&captures_views[i]);
			arg_ptrs[i] = &args[i];
		}

		std::string_view consume_ptr(path);
		if (!RE2::ConsumeN(&consume_ptr, *rule.svn_path, arg_ptrs.data(), captures_groups))
		{
			continue;
		}

		// Insert the whole capture so the \0 substitution can be used properly
		auto whole_capture_begin = path.begin();
		auto whole_capture_end = consume_ptr.begin();
		captures_views.emplace(captures_views.begin(), whole_capture_begin,
				       whole_capture_end);

		if (!rule.repo_branch)
		{
			break;
		}

		OutputLocation result;

		rule.svn_path->Rewrite(&result.repository, rule.repo_branch->repository,
				       captures_views.data(), captures_groups + 1);

		rule.svn_path->Rewrite(&result.branch, rule.repo_branch->branch,
				       captures_views.data(), captures_groups + 1);

		rule.svn_path->Rewrite(&result.path, rule.git_path, captures_views.data(),
				       captures_groups + 1);

		// Append any of the non-captured SVN path to the output git path
		result.path.append(consume_ptr);

		return result;
	}
	return std::nullopt;
}

int main(int argc, char* argv[])
{
	argparse::ArgumentParser program("svn-lfs-export");
	const toml::parse_result config_result = toml::parse_file("config.toml");

	if (!config_result)
	{
		log_error("ERROR: Failed to parse config.toml - {}",
			  config_result.error().description());
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
						  "from revision r{revision} by svn-lfs-export.");

	if (!svn_repository)
	{
		log_error("ERROR: Failed to parse the SVN repository string. Make sure a valid "
			  "path to a on-disk SVN repository is provided.");
		return 1;
	}

	if (!std::filesystem::is_directory(*svn_repository))
	{
		log_error("ERROR: Repository path \"{}\" is not a directory that can be found.",
			  *svn_repository);
		return 1;
	}

	const toml::table* identity_table = config["identity_map"].as_table();
	std::unordered_map<std::string, std::string> identity_map;

	if (identity_table)
	{
		static const RE2 valid_name_re(R"(^([^\n<>]+\ )*<[^<>\n]+>$)");
		for (auto&& [key, value] : *identity_table)
		{
			const auto git_identity = value.value<std::string>();
			if (!git_identity || !RE2::FullMatch(*git_identity, valid_name_re))
			{
				log_error("ERROR: Git identity for SVN user \"{}\" should be in "
					  "the format \"Firstname Lastname <email@domain.com>\"",
					  key.str());
				return 1;
			}
			identity_map[std::string(key.str())] = *git_identity;
		}
	}

	if (identity_map.size() == 0 && !override_domain)
	{
		log_error("ERROR: Please provide an identity map or a domain.");
		return 1;
	}

	if (identity_map.size() == 0)
	{
		log_error("WARNING: No identity map provided. Git author information will be "
			  "inaccurate.");
	}

	if (!override_domain)
	{
		log_error("WARNING: No domain provided. Any SVN users not present in the identity "
			  "map will cause the program to terminate with an error.");
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
				log_error("ERROR: LFS must be defined as an array of regular "
					  "expressions.");
				return 1;
			}

			lfs_rules.emplace_back(std::make_unique<RE2>(*expression));

			if (!lfs_rules.back()->ok())
			{
				log_error("ERROR: LFS regex \"{}\" is not valid: {}",
					  lfs_rules.back()->pattern(), lfs_rules.back()->error());
				return 1;
			}
		}
	}

	std::vector<Rule> rules;
	const toml::array* rules_config = config["rule"].as_array();

	if (!rules_config || rules_config->size() == 0)
	{
		log_error("ERROR: Expected rules to be an array of tables defined using multiple "
			  "[[rule]] statements.");
		return 1;
	}

	for (const toml::node& rule : *rules_config)
	{
		if (!rule.as_table())
		{
			log_error("ERROR: Expected rules to be an array of tables defined using "
				  "multiple [[rule]] statements.");
			return 1;
		}
		const toml::table& table = *rule.as_table();

		const auto svn_path = table["svn_path"].value<std::string_view>();
		const auto repository = table["repository"].value<std::string>();
		const auto branch = table["branch"].value<std::string>();
		const std::string git_path = table["git_path"].value_or("");

		if (!svn_path)
		{
			log_error("ERROR: Provide an svn_path for each rule.");
			return 1;
		}
		if (repository.has_value() != branch.has_value())
		{
			log_error("ERROR: For {} both a repository and a branch must be provided, "
				  "or neither should be provided .",
				  *svn_path);
			return 1;
		}

		std::optional<Rule::RepoBranch> repo_branch = {};
		if (repository && branch)
		{
			repo_branch = Rule::RepoBranch{*repository, *branch};
		}

		const auto min_revision = table["min_revision"].value<long int>();
		const auto max_revision = table["max_revision"].value<long int>();

		rules.emplace_back(std::make_unique<RE2>(*svn_path), repo_branch, git_path,
				   min_revision, max_revision);

		if (!rules.back().svn_path->ok())
		{
			log_error("ERROR: SVN path \"{}\" is not valid: {}", *svn_path,
				  rules.back().svn_path->error());
			return 1;
		}

		std::string error;
		static constexpr const char* error_message =
			R"(ERROR: Could not rewrite "{}" with the regex "{}" - {})";

		if (repository && !rules.back().svn_path->CheckRewriteString(*repository, &error))
		{
			log_error(error_message, *repository, *svn_path, error);
			return 1;
		}
		if (branch && !rules.back().svn_path->CheckRewriteString(*branch, &error))
		{
			log_error(error_message, *branch, *svn_path, error);
			return 1;
		}
		if (!rules.back().svn_path->CheckRewriteString(git_path, &error))
		{
			log_error(error_message, git_path, *svn_path, error);
			return 1;
		}
	}

	for (std::string input_line; std::getline(std::cin, input_line);)
	{
		long int revision = 0;
		std::string_view path;

		if (!RE2::FullMatch(input_line, R"(([0-9]+)\s+(.*))", &revision, &path))
		{
			log_error("ERROR: Input test path does not match the revision path format "
				  "\"1234 your/path\".");
			return 1;
		}

		std::optional<OutputLocation> output = map_path_to_output(rules, revision, path);

		if (output)
		{
			fmt::println("DEBUG: Mapped \"{}\"", path);
			fmt::println(" - Repository: {}", output->repository);
			fmt::println(" - Branch: {}", output->branch);
			fmt::println(" - Path: {}", output->path);
		}
		else
		{
			fmt::println("DEBUG: Path \"{}\" did not match any rules!", path);
		}
	}

	return 0;
}
