#pragma once
#include <cstdio>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

struct BeginCommitArgInfo
{
	std::string_view branch;
	std::string_view mark;
	long int revision;
	std::string_view committer;
	std::string_view time;
	std::string_view message;
	std::string_view from;
};

class IFastImport
{
public:
	virtual ~IFastImport() = default;

	void BeginCommit(BeginCommitArgInfo args);
	void Delete(const std::string_view path);
	void Modify(int mode, const std::string_view path, const std::string_view data);
	void Done();
	virtual void WriteToGitDirectory(std::filesystem::path path, const std::string_view data) = 0;

protected:
	virtual void Write(std::string_view content) = 0;
};

class FastImportProcess : public IFastImport
{
public:
	FastImportProcess(FILE* input, std::filesystem::path root) :
		mInput(input),
		mRoot(std::move(root)) {};

	void WriteToGitDirectory(std::filesystem::path path, const std::string_view data) final;

	void SaveLastWrittenRevision(long int rev);

	std::expected<std::optional<long int>, std::string> GetLastWrittenRevision();

	bool Flush();

private:
	void Write(std::string_view content) final;

	FILE* mInput;
	std::filesystem::path mRoot;
};

class FastImportBuffer : public IFastImport
{
public:
	FastImportBuffer();

	void WriteToGitDirectory(std::filesystem::path path, const std::string_view data) final;

	const std::string& GetBuffer() const { return mBuffer; };

private:
	void Write(std::string_view content) final;

	std::string mBuffer;
};