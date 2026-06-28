#include "Utils.hpp"
#include "Writer.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <git2.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>

void IFastImport::BeginCommit(BeginCommitArgInfo args)
{
	std::string command = fmt::format(
		"commit refs/heads/{}\n"
		"{}"
		"original-oid r{}\n"
		"committer {} {}\n"
		"data {}\n"
		"{}\n"
		"{}",
		args.branch, args.mark, args.revision, args.committer, args.time, args.message.length(),
		args.message, args.from
	);
	Write(command);
}

void IFastImport::Delete(const std::string_view path)
{
	Write(fmt::format("D {}\n", path));
}

void IFastImport::Modify(int mode, const std::string_view path, const std::string_view data)
{
	Write(fmt::format("M {} inline {}\ndata {}\n", mode, path, data.size()));
	Write(data);
}

void IFastImport::Done()
{
	Write("done\n");
}

void FastImportProcess::WriteToGitDirectory(std::filesystem::path path, const std::string_view data)
{
	std::filesystem::path writePath = mRoot / path;
	if (!std::filesystem::exists(writePath))
	{
		std::filesystem::create_directories(writePath.parent_path());
		std::ofstream file{writePath};
		file << data;
	}
}

void FastImportProcess::Write(std::string_view content)
{
	size_t written = std::fwrite(content.data(), 1, content.size(), mInput);
	if (written != content.size())
	{
		Log("ERROR: Failed to write to fast-import stream!");
	}
}

void FastImportProcess::SaveLastWrittenRevision(long int rev)
{
	std::ofstream file{mRoot / "svn_lfs_export_revision"};
	file << rev << '\n';
}

std::expected<std::optional<long int>, std::string> FastImportProcess::GetLastWrittenRevision()
{
	std::filesystem::path path = mRoot / "svn_lfs_export_revision";
	if (!std::filesystem::exists(path))
	{
		return std::nullopt;
	}

	std::ifstream file{path};
	long int rev = 0;
	file >> rev;
	if (file.fail())
	{
		return std::unexpected(
			fmt::format("Resume marker {:?} exists but could not be parsed", path.c_str())
		);
	}
	return rev;
}

bool FastImportProcess::Flush()
{
	return std::fflush(mInput) == 0 && std::ferror(mInput) == 0;
}

void FastImportBuffer::WriteToGitDirectory(std::filesystem::path, const std::string_view)
{
	// no op
}

void FastImportBuffer::Write(std::string_view content)
{
	mBuffer.append(content);
}
