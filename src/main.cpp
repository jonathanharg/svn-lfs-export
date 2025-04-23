#include "config.hpp"
#include <apr_general.h>
#include <apr_hash.h>
#include <apr_pools.h>
#include <argparse/argparse.hpp>
#include <chrono>
#include <cstdlib>
#include <date/date.h>
#include <date/tz.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/std.h>
#include <iostream>
#include <memory>
#include <optional>
#include <re2/re2.h>
#include <sstream>
#include <string>
#include <string_view>
#include <svn_pools.h>
#include <svn_props.h>
#include <svn_repos.h>
#include <utility>
#include <vector>

template <typename... T>
inline void log_error(fmt::format_string<T...> fmt, T&&... args)
{
	fmt::println(std::cerr, fmt, std::forward<T>(args)...);
}

template <typename... T>
inline void log_info(fmt::format_string<T...> fmt, T&&... args)
{
	fmt::println("progress {}", fmt::format(fmt, std::forward<T>(args)...));
}

template <typename... T>
inline void output(fmt::format_string<T...> fmt, T&&... args)
{
	fmt::println(fmt, std::forward<T>(args)...);
}

constexpr std::array<std::string_view, 5> path_change_strings = {
	"Modified", "Add", "Delete", "Replace", "Reset" /* Unused */
};

constexpr std::array<std::string_view, 5> node_kind_strings = {
	"None", "File", "Directory", "Unknown", "Symlink" /* Unused */
};

enum class GitMode
{
	Normal = 100644,
	Executable = 100755,
	// Symlink = 120000,
	// GitLink = 160000,
	// Subdirectory = 040000,
};

struct OutputLocation
{
	std::string repository;
	std::string branch;
	std::string path;
	bool lfs = false;
};

class SVNPool
{
	apr_pool_t* pool = nullptr;

public:
	explicit SVNPool(apr_pool_t* parent = nullptr) : pool(svn_pool_create(parent)) {}
	~SVNPool() { svn_pool_destroy(pool); }

	SVNPool(const SVNPool&) = delete;
	SVNPool(SVNPool&&) = delete;

	SVNPool& operator=(const SVNPool&) = delete;
	SVNPool& operator=(SVNPool&&) = delete;

	void clear() { svn_pool_clear(pool); }

	apr_pool_t* operator*() { return pool; };
	operator apr_pool_t*() const { return pool; }
};

std::optional<std::string> get_prop(apr_hash_t* hash, const char* prop)
{
	auto* value = static_cast<svn_string_t*>(apr_hash_get(hash, prop, APR_HASH_KEY_STRING));
	if (value)
	{
		return std::string(value->data, value->len);
	}
	return {};
};

std::optional<OutputLocation> map_path_to_output(const Config& config, const long int revision,
						 const std::string_view& path)
{
	const std::vector<Rule>& rules = config.rules;

	for (const Rule& rule : rules)
	{
		// Given a RULE, takes an INPUT REVISION and INPUT SVN PATH
		// 1. If not MIN REVISION <= INPUT REVISION <= MAX REVISION continue
		//    to next rule
		// 2. If not INPUT SVN PATH starts with and matches against
		//    RULE SVN PATH continue, recording any non-captured suffix
		// 3. If no REPOSITORY, ignore path, break and skip all other rules
		// 4. Rewrite GIT REPO with substitutions from SVN PATH match
		// 5. Rewrite BRANCH with substitutions from SVN PATH match
		// 6. Rewrite GIT PATH with substitutions from SVN PATH match
		// 7. Append GIT PATH with the non-captured suffix being careful of
		//    duplicate path separators (e.g. //)
		//    [NOTE. This is now a user skill issue]
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

std::string get_git_author(const Config& config, const std::optional<std::string>& svn_username)
{
	const std::string& domain = config.override_domain.value_or("localhost");

	if (!svn_username.has_value())
	{
		return fmt::format("Unknown User <unknown@{}>", domain);
	}
	if (config.identity_map.contains(*svn_username))
	{
		return config.identity_map.at(*svn_username);
	}

	return fmt::format("{} <{}@{}>", *svn_username, *svn_username, domain);
}

std::string get_commit_message(const Config& config, const std::string& svn_log,
			       const std::string& svn_username, long int revision)
{
	return fmt::format(fmt::runtime(config.commit_message_template), fmt::arg("log", svn_log),
			   fmt::arg("usr", svn_username), fmt::arg("rev", revision));
}

std::string get_git_time(const Config& config, const std::string& svn_time)
{
	// It looks like SVN stores dates in UTC time
	// https://svn.haxx.se/users/archive-2003-09/0322.shtml
	// This is good, because we don't have to mess with time zones when converting
	// to Unix Epoch time (which git uses). We might however, want to apply a local
	// UTC offset based on the location of the server.

	std::istringstream date_stream{svn_time};
	std::chrono::sys_time<std::chrono::milliseconds> utc_time;
	date_stream >> date::parse("%FT%T%Ez", utc_time);

	// I'm 90% sure SVN stores UTC with 6 decimal places / microseconds
	// But add a fail-safe to parse 0 decimal places / seconds
	if (date_stream.fail())
	{
		date_stream.clear();
		date_stream.exceptions(std::ios::failbit);
		date_stream.str(svn_time);
		date_stream >> date::parse("%FT%TZ", utc_time);
	}
	static const date::time_zone* tz = date::get_tzdb().locate_zone(config.time_zone);

	date::zoned_time<std::chrono::milliseconds> zoned_time{tz, utc_time};
	std::string formatted_offset = date::format("%z", zoned_time);

	auto unix_epoch =
		std::chrono::duration_cast<std::chrono::seconds>(utc_time.time_since_epoch())
			.count();
	return fmt::format("{} {}", unix_epoch, formatted_offset);
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

	apr_initialize();
	SVNPool root;
	SVNPool scratch;
	svn_repos_t* repository = nullptr;
	svn_error_t* err = nullptr;

	err = svn_repos_open3(&repository, config.svn_repository.c_str(), nullptr, root, scratch);
	SVN_INT_ERR(err);

	svn_fs_t* fs = svn_repos_fs(repository);
	if (!fs)
	{
		log_error("ERROR: SVN failed to open fs.");
		return EXIT_FAILURE;
	}

	long int youngest_revision = 1;
	err = svn_fs_youngest_rev(&youngest_revision, fs, scratch);
	SVN_INT_ERR(err);

	long int start_revision = config.min_revision.value_or(1);
	long int stop_revision = config.max_revision.value_or(youngest_revision);

	log_info("Running from revision {} to {}", start_revision, stop_revision);

	// 1. For each revision, get revision properties
	//     - Author
	//     - Date
	//     - Log message
	// 2. Gather all changes in a revision, grouping by output repo & branch
	//    storing metadata and a lazy file stream.
	//     - Output path -> output repo, branch, path & LFS
	//     - Change type added/removed/modified
	//     - Copy from?
	//     - Merge info?
	//     - File size
	//     - File data stream
	// 3. For each repo & branch, commit files.

	// THINK ABOUT
	//  - Failure / cancelling mid process
	//  - Saving marks
	//  - Continuing from where you left off
	//  - Branch creation, working out "from" commit
	//  - Merging?

	for (long int rev = start_revision; rev <= stop_revision; rev++)
	{
		log_info("Converting r{}", rev);
		SVNPool rev_pool(*root);

		svn_fs_root_t* rev_fs = nullptr;
		err = svn_fs_revision_root(&rev_fs, fs, rev, rev_pool);
		SVN_INT_ERR(err);

		apr_hash_t* rev_props = nullptr;
		err = svn_fs_revision_proplist2(&rev_props, fs, rev, false, rev_pool, scratch);
		SVN_INT_ERR(err);

		static constexpr const char* epoch = "1970-01-01T00:00:00Z";

		auto author_prop = get_prop(rev_props, SVN_PROP_REVISION_AUTHOR);
		auto log_prop = get_prop(rev_props, SVN_PROP_REVISION_LOG).value_or("");
		auto date_prop = get_prop(rev_props, SVN_PROP_REVISION_DATE).value_or(epoch);

		std::string git_author = get_git_author(config, author_prop);
		std::string git_message =
			get_commit_message(config, log_prop, author_prop.value_or("unknown"), rev);
		std::string git_time = get_git_time(config, date_prop);

		svn_fs_path_change_iterator_t* it = nullptr;
		err = svn_fs_paths_changed3(&it, rev_fs, rev_pool, scratch);
		SVN_INT_ERR(err);

		svn_fs_path_change3_t* changes = nullptr;
		err = svn_fs_path_change_get(&changes, it);
		SVN_INT_ERR(err);

		// TODO: Make the commit after first gathering files
		std::string ref = "refs/heads/main";
		output("commit {}", ref);
		output("committer {} {}", git_author, git_time);
		output("data {}\n{}", git_message.length(), git_message);

		while (changes)
		{
			std::string path{changes->path.data, changes->path.len};
			std::string_view change_kind = path_change_strings.at(changes->change_kind);
			std::string_view node_kind = node_kind_strings.at(changes->node_kind);

			bool text_mod = changes->text_mod;
			bool prop_mod = changes->prop_mod;

			// TODO: Get Copy from and mergeinfo
			// => Copy from might help performance / eliminate unnecessary duplication
			// => merge info might help us create branches

			std::optional<OutputLocation> destination =
				map_path_to_output(config, rev, path);

			log_error("{} {}: {:?} (mod text {}, props {})", change_kind, node_kind,
				  path, text_mod, prop_mod);

			if (destination)
			{
				log_error("> {}/{} {} (LFS {})", destination->repository,
					  destination->branch, destination->path, destination->lfs);
			}

			// TODO: are we sure we can skip over directories here?
			if (changes->node_kind != svn_node_kind_t::svn_node_file)
			{
				err = svn_fs_path_change_get(&changes, it);
				SVN_INT_ERR(err);
				continue;
			}

			svn_stream_t* content = nullptr;
			// TODO: Should probably free these pools early?
			// or have a file level pool
			err = svn_fs_file_contents(&content, rev_fs, path.c_str(), rev_pool);
			SVN_INT_ERR(err);

			svn_filesize_t file_size = 0;
			// TODO: I'm not sure if we want/need to be doing this
			err = svn_fs_file_length(&file_size, rev_fs, path.c_str(), rev_pool);
			SVN_INT_ERR(err);

			std::unique_ptr<char[]> buffer(new char[file_size]);

			// WARNING: This will probably overflow
			apr_size_t read_size = file_size;
			err = svn_stream_read_full(content, buffer.get(), &read_size);

			std::string_view file{buffer.get(), static_cast<size_t>(file_size)};

			output("M {} inline {}", static_cast<int>(GitMode::Normal),
			       &path.c_str()[1]);
			output("data {}\n{}", file.length(), file);

			err = svn_fs_path_change_get(&changes, it);
			SVN_INT_ERR(err);
		}

		scratch.clear();
	}
}
