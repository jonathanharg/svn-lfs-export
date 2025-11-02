#include "utils.hpp"
#include "writer.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

void stdoutWriter::WriteToFastImport(std::string_view repo, std::string_view content)
{
	if (!mOutputRepository.has_value())
	{
		mOutputRepository = repo;
	}

	if (mOutputRepository != repo)
	{
		LogError(
			"ERROR: Cannot write to repository {:?} using stdoutWriter, since it's currently writing {:?}",
			repo, *mOutputRepository
		);
	}

	std::cout << content;
}

std::filesystem::path stdoutWriter::GetLFSRoot(std::string_view /*repo*/)
{
	return std::filesystem::current_path();
}

bool stdoutWriter::
	DoesBranchAlreadyExistOnDisk(std::string_view /*repo*/, std::string_view /*branch*/)
{
	return false;
}
