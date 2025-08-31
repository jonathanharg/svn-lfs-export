#include "config.hpp"
#include "git.hpp"
#include "svn.hpp"
#include "utils.hpp"

#include <apr_general.h>
#include <argparse/argparse.hpp>

#include <cstdlib>
#include <iostream>

int main()
{
	argparse::ArgumentParser program("svn-lfs-export");
	const auto maybeConfig = Config::FromFile("config.toml");

	if (!maybeConfig)
	{
		std::cerr << maybeConfig.error() << '\n';
		return EXIT_FAILURE;
	}

	const Config& config = maybeConfig.value();

	apr_initialize();
	svn::Repository repository = svn::Repository(config.svnRepo);

	long int youngestRev = repository.GetYoungestRevision();

	long int startRev = config.minRevision.value_or(1);
	long int stopRev = config.maxRevision.value_or(youngestRev);

	LogInfo("Running from r{} to r{}", startRev, stopRev);

	for (long int revNum = startRev; revNum <= stopRev; revNum++)
	{
		svn::Revision rev = repository.GetRevision(revNum);
		auto result = git::WriteCommit(config, rev);

		if (!result.has_value())
		{
			std::cerr << result.error() << '\n';
			return EXIT_FAILURE;
		}
	}
}
