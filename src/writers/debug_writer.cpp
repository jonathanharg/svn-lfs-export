#include "writer.hpp"

#include <filesystem>
#include <string>
#include <string_view>

void DebugWriter::WriteToFastImport(std::string_view repo, std::string_view content)
{
	mOutputs[std::string(repo)].append(content);
}

std::filesystem::path DebugWriter::GetLFSRoot(std::string_view /*repo*/)
{
	return std::filesystem::current_path();
}

bool DebugWriter::
	DoesBranchAlreadyExistOnDisk(std::string_view /*repo*/, std::string_view /*branch*/)
{
	return false;
}
