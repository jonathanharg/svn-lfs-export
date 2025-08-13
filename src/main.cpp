#include "config.hpp"
#include "git.hpp"
#include "svn.hpp"
#include "utils.hpp"
#include <apr_general.h>
#include <argparse/argparse.hpp>
#include <cstdlib>
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
#include <vector>

int main()
{
	argparse::ArgumentParser program("svn-lfs-export");
	const auto maybeConfig = Config::FromFile("config.toml");

	if (!maybeConfig)
	{
		std::cerr << maybeConfig.error();
		return EXIT_FAILURE;
	}

	const Config& config = maybeConfig.value();

	apr_initialize();
	svn::Repository repository = svn::Repository(config.svnRepo);

	long int youngestRev = repository.GetYoungestRevision();

	long int startRev = config.minRevision.value_or(1);
	long int stopRev = config.maxRevision.value_or(youngestRev);

	LogInfo("Running from r{} to r{}", startRev, stopRev);

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

	for (long int revNum = startRev; revNum <= stopRev; revNum++)
	{
		LogInfo("Converting r{}", revNum);
		svn::Revision rev = repository.GetRevision(revNum);

		std::string committer = git::GetGitAuthor(config, rev.GetAuthor());
		std::string message = git::GetCommitMessage(config, rev.GetLog(), rev.GetAuthor(), revNum);
		std::string time = git::GetGitTime(config, rev.GetDate());

		std::string ref = "refs/heads/main";
		Output("commit {}", ref);
		Output("committer {} {}", committer, time);
		Output("data {}\n{}", message.length(), message);

		for (const auto& file : rev.GetFiles())
		{
			std::optional<git::OutputLocation> destination = git::MapPathToOutput(config, revNum, file.path);

			if (destination)
			{
				LogError("{} -> {}/{} {} (LFS {})", file.path, destination->repo, destination->branch,
						 destination->path, destination->lfs);
			}

			// TODO: are we sure we can skip over directories here?
			if (file.isDirectory)
			{
				continue;
			}

			if (file.changeType == svn::File::Change::Delete)
			{
				Output("D {}", destination->path);
			}
			else
			{
				std::string_view buff{file.buffer.get(), file.size};

				Output("M {} inline {}", static_cast<int>(git::Mode::Normal), destination->path);
				Output("data {}\n{}", file.size, buff);
			}
		}
	}
}
