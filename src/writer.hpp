#pragma once
#include <filesystem>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

class IWriter
{
public:
	IWriter() = default;
	IWriter(const IWriter&) = default;

	IWriter(IWriter&&) = delete;
	IWriter& operator=(const IWriter&) = default;
	IWriter& operator=(IWriter&&) = delete;
	virtual ~IWriter() = default;

	virtual std::ostream& GetFastImportStream(std::string_view repo) = 0;
	virtual std::filesystem::path GetLFSRoot(std::string_view repo) = 0;
	virtual bool DoesBranchAlreadyExist(std::string_view repo, std::string_view branch) = 0;
};

class StdOutWriter final : public IWriter
{
public:
	std::ostream& GetFastImportStream(std::string_view repo) override;
	std::filesystem::path GetLFSRoot(std::string_view repo) override;
	bool DoesBranchAlreadyExist(std::string_view repo, std::string_view branch) override;

private:
	std::optional<std::string> mOutputRepository;
};

class DebugWriter final : public IWriter
{
public:
	std::ostream& GetFastImportStream(std::string_view repo) override;
	std::filesystem::path GetLFSRoot(std::string_view repo) override;
	bool DoesBranchAlreadyExist(std::string_view repo, std::string_view branch) override;
	const std::unordered_map<std::string, std::ostringstream>& GetDebugOutput()
	{
		return mStreams;
	};

private:
	std::unordered_map<std::string, std::ostringstream> mStreams;
};
