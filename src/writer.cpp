#include "utils.hpp"
#include "writer.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

std::ostream& StdOutWriter::GetFastImportStream(std::string_view repo)
{
	if (!mOutputRepository.has_value())
	{
		mOutputRepository = repo;
	}

	if (mOutputRepository != repo)
	{
		LogError(
			"ERROR: Cannot write to repository {:?} using StdOutWriter, since it's currently writing {:?}",
			repo, *mOutputRepository
		);
	}

	return std::cout;
}

std::filesystem::path StdOutWriter::GetLFSRoot(std::string_view /*repo*/)
{
	return std::filesystem::current_path();
}

bool StdOutWriter::DoesBranchAlreadyExist(std::string_view /*repo*/, std::string_view /*branch*/)
{
	return false;
}

std::ostream& DebugWriter::GetFastImportStream(std::string_view repo)
{
	// Apple Clang seems to require the temporary std::string here
	// even though it supports C++20 heterogeneous lookup for unordered containers
	return mStreams[std::string(repo)];
}

std::filesystem::path DebugWriter::GetLFSRoot(std::string_view /*repo*/)
{
	return std::filesystem::current_path();
}

bool DebugWriter::DoesBranchAlreadyExist(std::string_view /*repo*/, std::string_view /*branch*/)
{
	return false;
}
