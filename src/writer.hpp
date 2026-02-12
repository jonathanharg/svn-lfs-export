#pragma once
#include <boost/asio.hpp>
#include <boost/process.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

static const std::filesystem::path sGitExe = boost::process::environment::find_executable("git");

class IWriter
{
public:
	IWriter() = default;

	IWriter(const IWriter&) = default;
	IWriter& operator=(const IWriter&) = default;

	IWriter(IWriter&&) = delete;
	IWriter& operator=(IWriter&&) = delete;

	virtual ~IWriter() = default;

	virtual void WriteToFastImport(std::string_view repo, std::string_view content) = 0;
	virtual std::filesystem::path GetLFSRoot(std::string_view repo) = 0;
	virtual bool DoesBranchAlreadyExistOnDisk(std::string_view repo, std::string_view branch) = 0;
	virtual bool DoesRepoExist(std::string_view) { return true; };
};

struct FastImportProcess
{
	explicit FastImportProcess(std::filesystem::path repoPath);
	boost::asio::io_context context;
	boost::asio::writable_pipe pipe;
	boost::process::process process;
};

class MultiRepoWriter final : public IWriter
{
public:
	MultiRepoWriter() = default;

	MultiRepoWriter(const MultiRepoWriter&) = default;
	MultiRepoWriter& operator=(const MultiRepoWriter&) = default;

	MultiRepoWriter(MultiRepoWriter&&) = delete;
	MultiRepoWriter& operator=(MultiRepoWriter&&) = delete;

	~MultiRepoWriter() override;

	void WriteToFastImport(std::string_view repo, std::string_view content) override;
	std::filesystem::path GetLFSRoot(std::string_view repo) override;
	bool DoesBranchAlreadyExistOnDisk(std::string_view repo, std::string_view branch) override;
	bool DoesRepoExist(std::string_view repo) override;

private:
	void CreateRepo(std::string_view repo);
	void StartProcess(std::string_view repo);

	std::unordered_map<std::string, FastImportProcess> mRunningProcesses;
};
