#pragma once
#include <boost/asio.hpp>
#include <boost/process.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

static const std::filesystem::path sGitExe = boost::process::environment::find_executable("git");

struct FastImportProcess
{
	explicit FastImportProcess(std::filesystem::path repoPath);
	boost::asio::io_context context;
	boost::asio::writable_pipe pipe;
	boost::process::process process;
};

class Writer
{
public:
	Writer() = default;
	~Writer();

	Writer(const Writer&) = delete;
	Writer& operator=(const Writer&) = delete;

	Writer(Writer&&) = delete;
	Writer& operator=(Writer&&) = delete;

	void WriteToFastImport(std::string_view repo, std::string_view content);
	std::filesystem::path GetLFSRoot(std::string_view repo);
	bool DoesBranchAlreadyExistOnDisk(std::string_view repo, std::string_view branch);
	bool DoesRepoExist(std::string_view repo);

private:
	void CreateRepo(std::string_view repo);
	void StartProcess(std::string_view repo);

	std::unordered_map<std::string, FastImportProcess> mRunningProcesses;
};
