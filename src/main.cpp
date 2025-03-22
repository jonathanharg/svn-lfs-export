#include "config.hpp"
#include <argparse/argparse.hpp>
#include <cstdlib>
#include <fmt/base.h>
#include <fmt/ostream.h>
#include <iostream>
#include <memory>
#include <optional>
#include <re2/re2.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
	bool lfs = false;
};

std::optional<OutputLocation> map_path_to_output(const Config& config, const long int revision,
						 const std::string_view& path)
{
	const std::vector<Rule>& rules = config.rules;

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

		for (const auto& lfs_rule : config.lfs_rules)
		{
			if (RE2::FullMatch(result.path, *lfs_rule))
			{
				result.lfs = true;
				break;
			}
		}

		return result;
	}
	return std::nullopt;
}

int main(int argc, char* argv[])
{
	argparse::ArgumentParser program("svn-lfs-export");
	const std::optional<Config> maybe_config = Config::from_file("config.toml");

	if (!maybe_config || !maybe_config->is_valid())
	{
		return EXIT_FAILURE;
	}

	const Config& config = maybe_config.value();

	for (std::string input_line; std::getline(std::cin, input_line);)
	{
		long int revision = 0;
		std::string_view path;

		if (!RE2::FullMatch(input_line, R"(([0-9]+)\s+(.*))", &revision, &path))
		{
			log_error("ERROR: Input test path does not match the revision path format "
				  "\"1234 your/path\".");
			return EXIT_FAILURE;
		}

		std::optional<OutputLocation> output = map_path_to_output(config, revision, path);

		if (output)
		{
			fmt::println("DEBUG: Mapped \"{}\"", path);
			fmt::println(" - Repository: {}", output->repository);
			fmt::println(" - Branch: {}", output->branch);
			fmt::println(" - Path: {}", output->path);
			fmt::println(" - LFS: {}", output->lfs);
		}
		else
		{
			fmt::println("DEBUG: Path \"{}\" did not match any rules!", path);
		}
	}
}
