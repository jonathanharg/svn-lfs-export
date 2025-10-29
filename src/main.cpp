#include "config.hpp"
#include "example_config.hpp"
#include "git.hpp"
#include "svn.hpp"
#include "utils.hpp"

#include <apr_general.h>
#include <argparse/argparse.hpp>
#include <fmt/ostream.h>
#include <git2.h>
#include <git2/global.h>
#include <re2/re2.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

int main(int argc, char* argv[])
{
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
		std::exit(1);
	}

	if (program["--example-config"] == true)
	{
		std::cout << kExampleConfig << '\n';
		return EXIT_SUCCESS;
	}

	LibGit2Init libGit;
	const auto maybeConfig = Config::FromFile(configPath);

	if (!maybeConfig)
	{
		std::cerr << maybeConfig.error() << '\n';
		return EXIT_FAILURE;
	}

	const Config& config = maybeConfig.value();

	LibAprInit libApr;
	auto repository = svn::Repository(config.svnRepo);

	long int youngestRev = repository.GetYoungestRevision();

	auto revString = program.present<std::string>("--revision");
	long int startRev{};
	long int stopRev{};

	if (!revString.has_value())
	{
		startRev = 1;
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
		fmt::println(
			std::cerr,
			"Unknown revision range {:?}. Use the format -r 1234, -r 1234:5678 or -r 1234:HEAD",
			*revString
		);
		return EXIT_FAILURE;
	}

	Git git(config);

	LogInfo("Running from r{} to r{}", startRev, stopRev);
	std::ofstream filestream{"output.txt"};

	for (long int revNum = startRev; revNum <= stopRev; revNum++)
	{
		svn::Revision rev = repository.GetRevision(revNum);
		auto result = git.WriteCommit(rev, filestream, std::filesystem::current_path());

		if (revNum % 500 == 0)
		{
			float percent = 100.0F * (static_cast<float>(revNum) / static_cast<float>(stopRev));
			LogInfo("Converting {}% [{}/{}]", percent, revNum, stopRev);
			LogError("Converting {}% [{}/{}]", percent, revNum, stopRev);
		}

		if (!result.has_value())
		{
			std::cerr << result.error() << '\n';
			return EXIT_FAILURE;
		}
	}
}
