#include "Config.hpp"
#include "ExampleConfig.hpp"
#include "Git.hpp"
#include "Svn.hpp"
#include "Utils.hpp"
#include "Writer.hpp"

#include <apr_general.h>
#include <argparse/argparse.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <git2.h>
#include <git2/branch.h>
#include <git2/errors.h>
#include <git2/global.h>
#include <git2/refs.h>
#include <git2/repository.h>
#include <git2/types.h>
#include <re2/re2.h>
#include <subprocess.h>

#include <array>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/signal.h>

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

std::filesystem::path GetExistingGitStatus(std::filesystem::path path, Git::StartingState* outState)
{
	std::filesystem::path gitRootPath;

	git_repository* gitRepo = nullptr;
	int err = git_repository_open(&gitRepo, path.c_str());

	if (err == GIT_ENOTFOUND)
	{
		git_repository_init(&gitRepo, path.c_str(), false);
		outState->isRepoEmpty = true;
		gitRootPath = path / ".git";
	}
	else
	{
		gitRootPath = git_repository_path(gitRepo);
		outState->isRepoEmpty = static_cast<bool>(git_repository_is_empty(gitRepo));

		git_branch_iterator* branchIt = nullptr;
		git_branch_iterator_new(&branchIt, gitRepo, GIT_BRANCH_LOCAL);

		git_reference* ref = nullptr;
		git_branch_t type{};

		while (git_branch_next(&ref, &type, branchIt) == 0)
		{
			const char* name = nullptr;
			git_branch_name(&name, ref);

			outState->existingBranches.emplace_back(name);

			git_reference_free(ref);
		}
		git_branch_iterator_free(branchIt);
	}
	git_repository_free(gitRepo);

	return gitRootPath;
}

int main(int argc, char* argv[])
{
	std::signal(SIGPIPE, SIG_IGN);
	argparse::ArgumentParser program("svn-lfs-export", PROJECT_VERSION);

	std::string configPath;

	program.add_argument("-r", "--revision")
		.help("start revision, or range of revisions FIRST:LAST, to operate on")
		.metavar("REV");
	program.add_argument("--config")
		.help("location of config.toml file")
		.metavar("FILE")
		.default_value(std::string{"config.toml"})
		.nargs(1)
		.store_into(configPath);
	program.add_argument("--example-config").help("output example config.toml file").flag();

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

	if (program["--example-config"] == true)
	{
		std::cout << kExampleConfig << '\n';
		return EXIT_SUCCESS;
	}

	LibGit2Init libGit;
	LibAprInit libApr;

	const auto maybeConfig = Config::FromFile(configPath);

	if (!maybeConfig)
	{
		std::cerr << maybeConfig.error() << '\n';
		return EXIT_FAILURE;
	}

	const Config& config = maybeConfig.value();

	Git::StartingState gitState;
	std::filesystem::path gitRoot = GetExistingGitStatus(config.gitRepo, &gitState);

	subprocess_s gitProcess{};
	{
		std::filesystem::path marksPath = gitRoot / "svn_lfs_export_marks";

		std::string gitDirFlag = fmt::format("--git-dir={}", gitRoot.c_str());
		std::string exportMarksFlag = fmt::format("--export-marks={}", marksPath.c_str());
		std::string importMarksFlag = fmt::format("--import-marks-if-exists={}", marksPath.c_str());

		const std::array subprocessArgs{
			"git",
			gitDirFlag.c_str(),
			"fast-import",
			"--done",
			exportMarksFlag.c_str(),
			importMarksFlag.c_str(),
			static_cast<const char*>(nullptr),
		};

		int result = subprocess_create(
			subprocessArgs.data(), subprocess_option_search_user_path, &gitProcess
		);
		if (result != 0)
		{
			Log("ERROR: Could not create git fast-import subprocess for {:?}",
				config.gitRepo.c_str());
			return EXIT_FAILURE;
		}
	}

	FastImportProcess writer(subprocess_stdin(&gitProcess), gitRoot);
	Git git(config, writer, gitState);

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
	long int youngestRev = *maybeYoungest;

	auto revString = program.present<std::string>("--revision");
	long int startRev{};
	long int stopRev{};

	if (!revString.has_value())
	{
		startRev = writer.GetLastWrittenRevision() + 1;
		stopRev = youngestRev;
	}
	else if (RE2::FullMatch(*revString, "(\\d+):(\\d+)", &startRev, &stopRev))
	{
	}
	else if (RE2::FullMatch(*revString, "(\\d+)(?::HEAD)?", &startRev))
	{
		stopRev = youngestRev;
	}
	else
	{
		Log("Unknown revision range {:?}. Use the format -r 1234, -r 1234:5678 or -r 1234:HEAD",
			*revString);
		return EXIT_FAILURE;
	}

	Log("Running from r{} to r{}", startRev, stopRev);

	for (long int revNum = startRev; revNum <= stopRev; revNum++)
	{
		auto rev = repository.GetRevision(revNum);
		if (!rev)
		{
			Log("Error converting r{}:\n{}", revNum, rev.error());
			break;
		}
		auto result = git.WriteCommit(*rev);

		if (revNum % 500 == 0)
		{
			float percent = 100.0F * (static_cast<float>(revNum) / static_cast<float>(stopRev));
			Log("Converting {}% [{}/{}]", percent, revNum, stopRev);
		}

		if (!result.has_value())
		{
			Log("Error converting r{}:\n{}", revNum, result.error());
			break;
		}
	}
    // TODO: Check conversion was successfull first
    writer.SaveLastWrittenRevision(stopRev);
	writer.Done();
	int processReturn = 0;
	int result = subprocess_join(&gitProcess, &processReturn);
	if (result != 0 || processReturn != 0)
	{
		Log("ERROR: An error occurred waiting for git fast-import!");
	}
	subprocess_destroy(&gitProcess);
}
