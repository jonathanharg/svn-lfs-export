#include "utils.hpp"
#include "writer.hpp"

#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/system.hpp>
#include <git2.h>
#include <tracy/Tracy.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace process = boost::process;
namespace asio = boost::asio;

FastImportProcess::FastImportProcess(std::filesystem::path repoPath) :
	pipe(context),
	process(
		context, sGitExe, {"fast-import", "--export-marks=marks"}, process::process_start_dir{std::move(repoPath)},
		process::process_stdio{.in = pipe, .out = stdout, .err = stdout}
	) {};

void MultiRepoWriter::WriteToFastImport(std::string_view repo, std::string_view content)
{
	ZoneScoped;

	std::string repoStr{repo};
	if (!mRunningProcesses.contains(repoStr))
	{
		if (!DoesRepoExist(repoStr))
		{
			CreateRepo(repoStr);
		}
		mRunningProcesses.emplace(repoStr, std::filesystem::current_path() / repo);
	}

	{
		ZoneScopedN("subprocess write");
		asio::write(mRunningProcesses.at(repoStr).pipe, asio::buffer(content));
	}
}

std::filesystem::path MultiRepoWriter::GetLFSRoot(std::string_view repo)
{
	return std::filesystem::current_path() / repo / ".git";
}

bool MultiRepoWriter::DoesBranchAlreadyExistOnDisk(std::string_view repo, std::string_view branch)
{
	ZoneScoped;

	if (!DoesRepoExist(repo))
	{
		return false;
	}

	asio::io_context ctx;
	asio::readable_pipe pipe{ctx};
	process::process_start_dir startDir{std::filesystem::current_path() / repo};

	process::process gitProcess(
		ctx, sGitExe, {"for-each-ref", "--format=%(refname:short)", "refs/heads"}, startDir,
		process::process_stdio{.in = {}, .out = pipe, .err = {}}
	);

	gitProcess.wait();

	std::string output;
	[[maybe_unused]] boost::system::error_code error;

	asio::read(pipe, asio::dynamic_buffer(output), error);
	assert(!error || (error == asio::error::eof));

	std::stringstream outputStrStream(output);
	std::string segment;
	std::vector<std::string> branchList;

	while (std::getline(outputStrStream, segment, '\n'))
	{
		if (segment == branch)
		{
			return true;
		}
	}
	return false;
}

bool MultiRepoWriter::DoesRepoExist(std::string_view repo)
{
	ZoneScoped;

	std::filesystem::path path = std::filesystem::current_path() / repo;
	int err = git_repository_open(nullptr, path.c_str());

	if (err == GIT_ENOTFOUND)
	{
		return false;
	}

	if (err != GIT_OK)
	{
		LogError(
			"Unexpected error opening Git repository {:?}: {}", path.c_str(),
			git_error_last()->message
		);
		std::exit(EXIT_FAILURE);
	}

	return true;
}

void MultiRepoWriter::CreateRepo(std::string_view repo)
{
	ZoneScoped;

	git_repository* repoPtr = nullptr;
	std::filesystem::path path = std::filesystem::current_path() / repo;

	int err = git_repository_init(&repoPtr, path.c_str(), false);
	git_repository_free(repoPtr);

	if (err != GIT_OK)
	{
		LogError(
			"Unexpected error creating Git repository {:?}: {}", path.c_str(),
			git_error_last()->message
		);
		std::exit(EXIT_FAILURE);
	}
}

MultiRepoWriter::~MultiRepoWriter()
{
	for (auto& [_, fastImportProcess] : mRunningProcesses)
	{
		fastImportProcess.pipe.close();
		fastImportProcess.process.wait();
	}
}
