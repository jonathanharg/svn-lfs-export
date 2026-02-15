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
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

Writer::Writer(const std::string& repo) :
	mRepoPath(std::filesystem::weakly_canonical(repo))
{
	if (!std::filesystem::exists(mRepoPath))
	{
		std::filesystem::create_directories(mRepoPath);
	}

	chdir(mRepoPath.c_str());

	git_repository* gitRepo = nullptr;
	int err = git_repository_open(&gitRepo, mRepoPath.c_str());

	if (err == GIT_ENOTFOUND)
	{
		git_repository_init(&gitRepo, mRepoPath.c_str(), false);
		mIsEmpty = true;
		mGitRootPath = mRepoPath / ".git";
	}
	else
	{
		mGitRootPath = git_repository_path(gitRepo);
		mIsEmpty = git_repository_is_empty(gitRepo);

		git_branch_iterator* branchIt = nullptr;
		git_branch_iterator_new(&branchIt, gitRepo, GIT_BRANCH_LOCAL);

		git_reference* ref = nullptr;
		git_branch_t type{};

		while (git_branch_next(&ref, &type, branchIt) == 0)
		{
			const char* name = nullptr;
			git_branch_name(&name, ref);

			mExistingBranches.emplace_back(name);

			git_reference_free(ref);
		}
		git_branch_iterator_free(branchIt);
	}
	git_repository_free(gitRepo);

	const std::array args{
		"git",
		"fast-import",
		"--export-marks=./.git/svn_lfs_export_marks",
		"--import-marks-if-exists=./.git/svn_lfs_export_marks",
		static_cast<const char*>(nullptr),
	};
	int result = subprocess_create(args.data(), subprocess_option_search_user_path, &mProcess);
	if (result != 0)
	{
		Log("ERROR: Could not create git fast-import subprocess in {:?}", mRepoPath.c_str());
		std::exit(EXIT_FAILURE);
	}
}

FILE* Writer::GetFastImportStream()
{
	return subprocess_stdin(&mProcess);
}

bool Writer::DoesBranchAlreadyExistOnDisk(const std::string& branch)
{
	return std::ranges::find(mExistingBranches, branch) != mExistingBranches.end();
}

long int Writer::GetLastWrittenRevision()
{
	long int read_value = 0;
	std::ifstream in(mGitRootPath / kLastRevPath);
	in >> read_value;
	in.close();
	return read_value;
}

void Writer::WriteLastRevision(long int rev)
{
	std::ofstream out(mGitRootPath / kLastRevPath);
	out << rev;
	out.close();
}

Writer::~Writer()
{
	subprocess_destroy(&mProcess);
}
