// svn-branch-detect: a read-only prepass that scans an SVN repository and suggests
// [branch_origin] entries for svn-lfs-export's config.toml.
//
// It replays SVN history through the *same* path-to-branch mapping the converter uses
// (Git::MapPath) and reports, for every git branch the conversion would create, where
// that branch originated. The output is a TOML snippet printed to stdout for the user to
// review and copy into their config. This tool never writes to the git repository and
// never modifies any config file.
//
// Origins are derived strictly from SVN copyfrom metadata: when a branch is born by an
// `svn copy`, the copy's source path+revision is resolved to the source branch's git
// commit and offered as the origin. A branch with exactly one clean candidate is filled
// in; zero or several candidates, or a source that cannot be referenced, are left as
// commented blocks for the user to decide. SVN history is non-injective, so the tool never
// silently resolves doubt.

#include "Config.hpp"
#include "Git.hpp"
#include "Svn.hpp"
#include "Utils.hpp"
#include "Writer.hpp"

#include <apr_general.h>
#include <argparse/argparse.hpp>
#include <fmt/format.h>
#include <git2/global.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{

struct LibGit2Init
{
	LibGit2Init() { git_libgit2_init(); }
	~LibGit2Init() { git_libgit2_shutdown(); }

	LibGit2Init(const LibGit2Init&) = delete;
	LibGit2Init& operator=(const LibGit2Init&) = delete;
};

struct LibAprInit
{
	LibAprInit() { apr_initialize(); }
	~LibAprInit() { apr_terminate(); }

	LibAprInit(const LibAprInit&) = delete;
	LibAprInit& operator=(const LibAprInit&) = delete;
};

// A no-op fast-import sink. Git::MapPath needs a Git instance (which requires an
// IFastImport&), but the prepass only ever calls MapPath -- it never emits a commit -- so
// every sink method discards its input. (FastImportBuffer would serve the same purpose but
// its constructor is currently unlinked, so a local null sink is the lighter choice.)
class NullFastImport : public IFastImport
{
public:
	void WriteToGitDirectory(std::filesystem::path, const std::string_view) final {}

private:
	void Write(std::string_view) final {}
};

// A commit the converter would write to a branch. "markable" mirrors the converter's mark
// condition exactly: a revision is marked (and so referenceable as `:rev` in a branch
// origin) only when it is NOT a multi-commit revision -- i.e. all of its mapped files land
// on a single branch. See Git::WriteCommit in Git.cpp (the `isMultiCommit` check): if these
// two definitions ever drift, this tool will suggest `:rev` handles that do not exist.
struct Commit
{
	long int revision;
	bool markable;
};

// One candidate origin for a branch: an `svn copy` whose source resolves to a real commit.
struct Candidate
{
	std::string sourceBranch;
	std::string copyfromPath;
	long int copyfromRevision;
	long int resolvedRevision; // newest commit on sourceBranch at or before copyfromRevision
	bool resolvedMarkable;     // whether resolvedRevision can be referenced as ":rev"
	std::optional<long int> suggestedRevision; // newest *markable* commit <= copyfromRevision
};

// One git branch the conversion would create, in the order it is first written.
struct BranchInfo
{
	std::string name;
	long int birthRevision;
	std::vector<Candidate> candidates;
};

// The git branch a path maps to, or "" if it maps to none. Copy sources/targets are bare
// directory paths (e.g. "/trunk", "/branches/foo") while rules are written with a trailing
// slash (e.g. "/trunk/"), so a trailing slash is appended to make a directory match the
// same rule its flattened children match. Only the branch is needed, so the rewritten path
// is irrelevant.
std::string MapBranch(Git& git, long int revision, std::string path)
{
	if (path.empty() || path.back() != '/')
	{
		path.push_back('/');
	}
	std::optional<Git::Mapping> mapping = git.MapPath(revision, path);
	if (mapping && !mapping->skip)
	{
		return mapping->branch;
	}
	return "";
}

using CommitsByBranch = std::unordered_map<std::string, std::vector<Commit>>;

// Newest commit on a branch at or before a given revision, if any. The branch's commit
// list is built in ascending order during the forward replay, and copyfrom always points
// at an earlier revision, so by the time a branch is born its sources are fully known.
std::optional<Commit> ResolveOrigin(
	const CommitsByBranch& branchCommits, const std::string& sourceBranch, long int copyfromRevision
)
{
	auto it = branchCommits.find(sourceBranch);
	if (it == branchCommits.end())
	{
		return std::nullopt;
	}
	const std::vector<Commit>& commits = it->second;
	auto upper = std::ranges::upper_bound(
		commits, copyfromRevision, {}, &Commit::revision
	);
	if (upper == commits.begin())
	{
		return std::nullopt;
	}
	return *(upper - 1);
}

// Newest *markable* commit on a branch at or before a given revision -- the best origin the
// user can actually reference when the resolved commit itself is ambiguous.
std::optional<long int> ResolveMarkable(
	const CommitsByBranch& branchCommits, const std::string& sourceBranch, long int copyfromRevision
)
{
	auto it = branchCommits.find(sourceBranch);
	if (it == branchCommits.end())
	{
		return std::nullopt;
	}
	const std::vector<Commit>& commits = it->second;
	auto upper = std::ranges::upper_bound(commits, copyfromRevision, {}, &Commit::revision);
	for (auto rit = std::make_reverse_iterator(upper); rit != commits.rend(); ++rit)
	{
		if (rit->markable)
		{
			return rit->revision;
		}
	}
	return std::nullopt;
}

// Replay the whole SVN history through the converter's mapping rules. Records, for each git
// branch, its birth revision and -- gathered at that birth revision -- the copyfrom-derived
// candidate origins.
std::expected<std::vector<BranchInfo>, std::string>
CollectBranches(svn::Repository& repository, long int youngestRevision, Git& git)
{
	std::vector<BranchInfo> branches;
	std::unordered_map<std::string, size_t> indexByName;
	CommitsByBranch branchCommits;

	for (long int revNum = 1; revNum <= youngestRevision; revNum++)
	{
		auto revision = repository.GetRevision(revNum);
		if (!revision)
		{
			return std::unexpected(
				fmt::format("Error reading r{}:\n{}", revNum, revision.error())
			);
		}
		std::span<const svn::File> files = revision->GetFiles();

		// Map every path once. A branch is "touched" in a revision when any of its files
		// maps to it (the same condition that makes the converter open a commit on it).
		// Reading metadata only -- file contents are never fetched.
		std::vector<std::string> fileBranch(files.size());
		std::unordered_set<std::string> touched;
		for (size_t i = 0; i < files.size(); i++)
		{
			std::optional<Git::Mapping> mapping = git.MapPath(revNum, files[i].path);
			if (mapping && !mapping->skip && !mapping->branch.empty())
			{
				fileBranch[i] = mapping->branch;
				touched.insert(mapping->branch);
			}
		}

		// A revision is "markable" exactly when the converter would write a single commit
		// for it (only one branch touched); a multi-branch revision gets no mark on any of
		// its commits. This must match Git::WriteCommit's isMultiCommit rule.
		const bool markable = touched.size() == 1;

		// Record a commit on each touched branch and note any born this revision.
		std::unordered_set<std::string> bornThisRevision;
		for (const std::string& branch : touched)
		{
			branchCommits[branch].push_back({.revision = revNum, .markable = markable});
			if (!indexByName.contains(branch))
			{
				indexByName.emplace(branch, branches.size());
				branches.push_back({.name = branch, .birthRevision = revNum, .candidates = {}});
				bornThisRevision.insert(branch);
			}
		}

		if (bornThisRevision.empty())
		{
			continue;
		}

		// Gather candidate origins for branches born here from the revision's copy metadata.
		for (const svn::File& dir : files)
		{
			if (!dir.isDirectory || !dir.copiedFrom)
			{
				continue;
			}

			// Which born branches does this directory copy's subtree produce? Normally the
			// target path maps directly to the branch; only if it doesn't (a sub-path with a
			// more specific rule) do we fall back to the copied child files.
			std::unordered_set<std::string> produced;
			std::string target = MapBranch(git, revNum, dir.path);
			if (!target.empty())
			{
				if (bornThisRevision.contains(target))
				{
					produced.insert(target);
				}
			}
			else
			{
				const std::string prefix = dir.path + "/";
				for (size_t j = 0; j < files.size(); j++)
				{
					if (fileBranch[j].empty() || !bornThisRevision.contains(fileBranch[j]))
					{
						continue;
					}
					if (std::string_view{files[j].path}.starts_with(prefix))
					{
						produced.insert(fileBranch[j]);
					}
				}
			}

			if (produced.empty())
			{
				continue;
			}

			// Identify the source branch and resolve the peg revision to a real commit.
			const std::string sourceBranch =
				MapBranch(git, dir.copiedFrom->rev, dir.copiedFrom->path);
			std::optional<Commit> resolved =
				ResolveOrigin(branchCommits, sourceBranch, dir.copiedFrom->rev);

			for (const std::string& branch : produced)
			{
				// Filters: an untracked source (maps to no branch / no commit yet) is not a
				// usable origin, and a self-copy is not a birth.
				if (sourceBranch.empty() || !resolved || sourceBranch == branch)
				{
					continue;
				}

				BranchInfo& info = branches[indexByName.at(branch)];
				const bool duplicate = std::ranges::any_of(
					info.candidates,
					[&](const Candidate& c)
					{
						return c.sourceBranch == sourceBranch &&
							c.copyfromPath == dir.copiedFrom->path &&
							c.copyfromRevision == dir.copiedFrom->rev;
					}
				);
				if (!duplicate)
				{
					info.candidates.push_back(
						{.sourceBranch = sourceBranch,
						 .copyfromPath = dir.copiedFrom->path,
						 .copyfromRevision = dir.copiedFrom->rev,
						 .resolvedRevision = resolved->revision,
						 .resolvedMarkable = resolved->markable,
						 .suggestedRevision = ResolveMarkable(
							 branchCommits, sourceBranch, dir.copiedFrom->rev
						 )}
					);
				}
			}
		}
	}

	return branches;
}

// A TOML key, rendered bare when it is safe to and quoted otherwise (branch names can
// contain characters like '/' from rule rewrites). Mirrors the e2e harness' key logic.
std::string TomlKey(const std::string& key)
{
	const bool bare = !key.empty() &&
		std::ranges::all_of(
			key,
			[](char c)
			{
				return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '-' || c == '_';
			}
		);
	if (bare)
	{
		return key;
	}
	return fmt::format("\"{}\"", key);
}

// The distinct *markable* resolved revisions among a branch's candidates -- the origins the
// tool could actually fill in. One => an unambiguous fill; several => the user must choose.
std::vector<long int> DistinctMarkable(const std::vector<Candidate>& candidates)
{
	std::vector<long int> revisions;
	for (const Candidate& c : candidates)
	{
		if (c.resolvedMarkable && !std::ranges::contains(revisions, c.resolvedRevision))
		{
			revisions.push_back(c.resolvedRevision);
		}
	}
	return revisions;
}

// One candidate, rendered as a "=> branch = ":rev"" suggestion line for the deferral blocks.
std::string CandidateLine(const std::string& key, const Candidate& c)
{
	if (c.resolvedMarkable)
	{
		return fmt::format(
			"#   svn copy {}@{} -> {} r{}  =>  {} = \":{}\"\n", c.copyfromPath, c.copyfromRevision,
			c.sourceBranch, c.resolvedRevision, key, c.resolvedRevision
		);
	}
	if (c.suggestedRevision)
	{
		return fmt::format(
			"#   svn copy {}@{} -> {} r{} is ambiguous; nearest usable is r{}  =>  {} = \":{}\"\n",
			c.copyfromPath, c.copyfromRevision, c.sourceBranch, c.resolvedRevision,
			*c.suggestedRevision, key, *c.suggestedRevision
		);
	}
	return fmt::format(
		"#   svn copy {}@{} -> {} r{} is ambiguous and has no usable earlier commit\n",
		c.copyfromPath, c.copyfromRevision, c.sourceBranch, c.resolvedRevision
	);
}

void RenderBranch(const BranchInfo& branch)
{
	const std::string key = TomlKey(branch.name);

	if (branch.candidates.empty())
	{
		fmt::print(
			"# {} (born r{}): origin could not be determined automatically.\n"
			"#   No SVN copy was found creating this branch; set it manually, e.g.\n"
			"#   {} = \":<revision>\"\n",
			key, branch.birthRevision, key
		);
		return;
	}

	const bool anyAmbiguous = std::ranges::any_of(
		branch.candidates, [](const Candidate& c) { return !c.resolvedMarkable; }
	);

	if (!anyAmbiguous)
	{
		// Every candidate resolves to a referenceable commit. If they all agree on one
		// origin -- even when the branch was assembled from several copies of the same
		// source -- there is no doubt, so fill it in (showing provenance for audit).
		const std::vector<long int> fillable = DistinctMarkable(branch.candidates);
		if (fillable.size() == 1)
		{
			const Candidate& c = branch.candidates.front();
			fmt::print(
				"{} = \":{}\"  # born r{}; svn copy {}@{} -> {} r{}\n", key, c.resolvedRevision,
				branch.birthRevision, c.copyfromPath, c.copyfromRevision, c.sourceBranch,
				c.resolvedRevision
			);
			return;
		}
	}
	else if (branch.candidates.size() == 1)
	{
		// A single copy whose resolved commit is ambiguous: the obvious answer (:resolved)
		// does not exist as a mark, so it cannot be filled in. Name the offending revision
		// and suggest the nearest revision that can actually be referenced.
		const Candidate& c = branch.candidates.front();
		fmt::print(
			"# {} (born r{}): cannot use r{} as origin -- r{} is ambiguous (maps to multiple commits).\n",
			key, branch.birthRevision, c.resolvedRevision, c.resolvedRevision
		);
		if (c.suggestedRevision)
		{
			fmt::print(
				"#   Last non-ambiguous commit on source branch {:?} was r{}. Suggestion:\n"
				"#   {} = \":{}\"\n",
				c.sourceBranch, *c.suggestedRevision, key, *c.suggestedRevision
			);
		}
		else
		{
			fmt::print(
				"#   No non-ambiguous commit exists on source branch {:?} at or before r{}; set it manually.\n",
				c.sourceBranch, c.resolvedRevision
			);
		}
		return;
	}

	// What's left is genuine doubt: several candidates that disagree on the origin, or a mix
	// of clean and ambiguous copies. SVN history does not point at a single origin, so defer
	// to the user with every option laid out.
	fmt::print(
		"# {} (born r{}): multiple candidate origins -- choose one:\n", key, branch.birthRevision
	);
	for (const Candidate& c : branch.candidates)
	{
		fmt::print("{}", CandidateLine(key, c));
	}
}

constexpr const char* kHeader =
	"# Branch origins suggested by svn-branch-detect.\n"
	"#\n"
	"# Each entry below is a git branch that the conversion will create and that needs a\n"
	"# branch origin. Filled-in lines are this tool's best single-candidate guess from SVN\n"
	"# copy history -- review them before trusting them. Commented blocks could not be\n"
	"# resolved automatically and need a decision from you before conversion.\n"
	"#\n"
	"# Copy the entries you want into the [branch_origin] section of your config.toml.\n";

} // namespace

int main(int argc, char* argv[])
{
	argparse::ArgumentParser program("svn-branch-detect", PROJECT_VERSION);

	std::string configPath;
	program.add_argument("--config")
		.help("location of config.toml file")
		.metavar("FILE")
		.default_value(std::string{"config.toml"})
		.nargs(1)
		.store_into(configPath);

	try
	{
		program.parse_args(argc, argv);
	}
	catch (const std::exception& err)
	{
		std::cerr << err.what() << '\n';
		std::cerr << program;
		return EXIT_FAILURE;
	}

	LibGit2Init libGit;
	LibAprInit libApr;

	const auto maybeConfig = Config::FromFile(configPath);
	if (!maybeConfig)
	{
		std::cerr << maybeConfig.error() << '\n';
		return EXIT_FAILURE;
	}
	const Config& config = *maybeConfig;

	auto maybeRepository = svn::Repository::Open(config.svnRepo);
	if (!maybeRepository)
	{
		Log("ERROR: {}", maybeRepository.error());
		return EXIT_FAILURE;
	}
	svn::Repository& repository = *maybeRepository;

	auto maybeYoungest = repository.GetYoungestRevision();
	if (!maybeYoungest)
	{
		Log("ERROR: {}", maybeYoungest.error());
		return EXIT_FAILURE;
	}

	// A throwaway converter used purely to reach the public MapPath; it writes nothing.
	NullFastImport sink;
	Git git(config, sink, Git::StartingState{});

	auto maybeBranches = CollectBranches(repository, *maybeYoungest, git);
	if (!maybeBranches)
	{
		Log("ERROR: {}", maybeBranches.error());
		return EXIT_FAILURE;
	}
	std::vector<BranchInfo> branches = std::move(*maybeBranches);

	// Output order: birth revision ascending, ties broken by branch name.
	std::ranges::sort(
		branches,
		[](const BranchInfo& a, const BranchInfo& b)
		{
			return std::tie(a.birthRevision, a.name) < std::tie(b.birthRevision, b.name);
		}
	);

	// The root branch is the one the converter writes as the parent-less initial commit:
	// the lexicographically smallest branch among those born at the earliest revision
	// (matching the converter's per-revision sort + first-commit handling). It legitimately
	// has no origin and is omitted from the snippet.
	std::string rootBranch;
	bool ambiguousRoot = false;
	if (!branches.empty())
	{
		const long int firstBirth = branches.front().birthRevision;
		std::vector<std::string> bornFirst;
		for (const auto& branch : branches)
		{
			if (branch.birthRevision == firstBirth)
			{
				bornFirst.push_back(branch.name);
			}
		}
		rootBranch = *std::ranges::min_element(bornFirst);
		ambiguousRoot = bornFirst.size() > 1;
	}

	fmt::print("{}\n", kHeader);

	if (ambiguousRoot)
	{
		// Pathological: several branches are born in the very first revision, so which one
		// is the true parent-less root is genuinely ambiguous. Surface it loudly rather
		// than silently exempting them; the non-root ones still need origins below.
		Log("WARNING: multiple branches are born in the first revision (r{}); the parent-less root is ambiguous.",
			branches.front().birthRevision);
		fmt::print(
			"# WARNING: multiple branches are born in the first revision (r{}). The\n"
			"# parent-less root branch is ambiguous; svn-branch-detect assumed {:?}. Review\n"
			"# this -- the other first-revision branches below still need an origin.\n\n",
			branches.front().birthRevision, rootBranch
		);
	}

	fmt::print("[branch_origin]\n");

	bool emittedAny = false;
	for (const auto& branch : branches)
	{
		if (branch.name == rootBranch)
		{
			continue;
		}
		// Never overwrite an origin the user has already pinned; omit it entirely.
		if (config.branchMap.contains(branch.name))
		{
			continue;
		}

		emittedAny = true;
		RenderBranch(branch);
	}

	if (!emittedAny)
	{
		fmt::print("# (no branches need an origin)\n");
	}

	return EXIT_SUCCESS;
}
