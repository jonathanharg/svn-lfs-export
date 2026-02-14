#include "subprocess.h"
#include "utils.hpp"
#include "writer.hpp"

#include <fmt/ranges.h>
#include <git2.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

void Writer::StartProcess(const std::string& repo)
{
	auto rootPath = std::filesystem::current_path();
	auto repoPath = rootPath / repo;

	chdir(repoPath.c_str());

	const std::array args{
		"git",
		"fast-import",
		"--export-marks=./.git/svn_lfs_export_marks",
		"--import-marks-if-exists=./.git/svn_lfs_export_marks",
		static_cast<const char*>(nullptr),
	};
	int result = subprocess_create(
		args.data(), subprocess_option_search_user_path, &mRunningProcesses[repo]
	);
	if (result != 0)
	{
		Log("ERROR: Could not create git fast-import subprocess in {:?}", repoPath.c_str());
		std::exit(EXIT_FAILURE);
	}

	chdir(rootPath.c_str());

	// Load current branches

	git_repository* gitRepo = nullptr;
	git_repository_open(&gitRepo, repoPath.c_str());

	git_branch_iterator* iter = nullptr;
	git_branch_iterator_new(&iter, gitRepo, GIT_BRANCH_LOCAL);

	git_reference* ref = nullptr;
	git_branch_t type{};

	while (git_branch_next(&ref, &type, iter) == 0)
	{
		const char* name = nullptr;
		git_branch_name(&name, ref);

		mRepoBranches[repo].emplace_back(name);

		git_reference_free(ref);
	}

	git_branch_iterator_free(iter);
	git_repository_free(gitRepo);
}

FILE* Writer::GetFastImportStream(const std::string& repo)
{
	if (!mRunningProcesses.contains(repo))
	{
		if (!DoesRepoExist(repo))
		{
			CreateRepo(repo);
		}
		StartProcess(repo);
	}
	return subprocess_stdin(&mRunningProcesses[repo]);
}

std::filesystem::path Writer::GetLFSRoot(std::string_view repo)
{
	return std::filesystem::current_path() / repo / ".git";
}

bool Writer::DoesBranchAlreadyExistOnDisk(const std::string& repo, const std::string& branch)
{
	if (!mRepoBranches.contains(repo))
	{
		return false;
	}

	return std::ranges::find(mRepoBranches[repo], branch) != mRepoBranches[repo].end();
}

bool Writer::DoesRepoExist(std::string_view repo)
{
	std::filesystem::path path = std::filesystem::current_path() / repo;
	int err = git_repository_open(nullptr, path.c_str());

	if (err == GIT_ENOTFOUND)
	{
		return false;
	}

	if (err != GIT_OK)
	{
		Log("Unexpected error opening Git repository {:?}: {}", path.c_str(),
			git_error_last()->message);
		std::exit(EXIT_FAILURE);
	}

	return true;
}

bool Writer::IsRepoEmpty(std::string_view repo)
{
	std::filesystem::path path = std::filesystem::current_path() / repo;
	int err = git_repository_open(nullptr, path.c_str());

	git_repository* gitRepository = nullptr;
	git_repository_open(&gitRepository, ".");

	bool isEmpty = git_repository_is_empty(gitRepository);

	git_repository_free(gitRepository);
	return isEmpty;
}

void Writer::CreateRepo(std::string_view repo)
{
	git_repository* repoPtr = nullptr;
	std::filesystem::path path = std::filesystem::current_path() / repo;

	int err = git_repository_init(&repoPtr, path.c_str(), false);
	git_repository_free(repoPtr);

	if (err != GIT_OK)
	{
		Log("Unexpected error creating Git repository {:?}: {}", path.c_str(),
			git_error_last()->message);
		std::exit(EXIT_FAILURE);
	}
}

Writer::~Writer()
{
	for (auto& [_, process] : mRunningProcesses)
	{
		subprocess_destroy(&process);
	}
}
