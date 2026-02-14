#pragma once
#include "subprocess.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Writer
{
public:
	Writer() = default;
	~Writer();

	Writer(const Writer&) = delete;
	Writer& operator=(const Writer&) = delete;

	Writer(Writer&&) = delete;
	Writer& operator=(Writer&&) = delete;

	FILE* GetFastImportStream(const std::string& repo);
	std::filesystem::path GetLFSRoot(std::string_view repo);
	bool DoesBranchAlreadyExistOnDisk(const std::string& repo, const std::string& branch);
	bool IsRepoEmpty(std::string_view repo);

private:
	bool DoesRepoExist(std::string_view repo);
	void CreateRepo(std::string_view repo);
	void StartProcess(const std::string& repo);

	std::unordered_map<std::string, subprocess_s> mRunningProcesses;
	std::unordered_map<std::string, std::vector<std::string>> mRepoBranches;
};
