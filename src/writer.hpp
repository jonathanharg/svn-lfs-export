#pragma once
#include "subprocess.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

class Writer
{
public:
	Writer(const std::string& repo);
	~Writer();

	Writer(const Writer&) = delete;
	Writer& operator=(const Writer&) = delete;

	Writer(Writer&&) = delete;
	Writer& operator=(Writer&&) = delete;

	FILE* GetFastImportStream();
	std::filesystem::path GetLFSRoot() { return mGitRootPath; };
	bool DoesBranchAlreadyExistOnDisk(const std::string& branch);
	bool IsRepoEmpty() const { return mIsEmpty; };
	long int GetLastWrittenRevision();
	void WriteLastRevision(long int rev);

private:
	bool mIsEmpty = false;
	subprocess_s mProcess{};
	std::filesystem::path mRepoPath;
	std::filesystem::path mGitRootPath;
	std::vector<std::string> mExistingBranches;
	static constexpr const char* kLastRevPath = "svn_lfs_export_lastrev";
};
