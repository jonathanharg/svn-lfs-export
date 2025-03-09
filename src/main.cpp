#include "argparse/argparse.hpp"
#include "fmt/format.h"
#include "re2/re2.h"
#include "toml++/toml.hpp"
#include <filesystem>
#include <iostream>
#include <optional>
#include <unordered_map>

int main(int argc, char* argv[])
{
	argparse::ArgumentParser program("svn-lfs-export");
	const toml::parse_result config_result = toml::parse_file("config.toml");

	if (!config_result)
	{
		std::cerr << "Failed to parse config.toml - " << config_result.error() << '\n';
		return 1;
	}

	const toml::table& config = config_result.table();

	const auto svn_path = config["repository"].value<std::string>();
	const auto min_revision = config["min_revision"].value<long int>();
	const auto max_revision = config["max_revision"].value<long int>();
	const auto override_domain = config["domain"].value<std::string>();
	const auto create_base_commit = config["create_base_commit"].value_or(false);

	if (!svn_path)
	{
		std::cerr << "Failed to parse the SVN repository string. Make sure a "
			     "valid path to a on-disk SVN repository is provided.\n";
		return 1;
	}

	if (!std::filesystem::is_directory(*svn_path))
	{
		std::cerr << fmt::format(
		    "Repository path \"{}\" is not a directory that can be found.\n", *svn_path);
		return 1;
	}

	const auto identity_table = config["identity_map"].as_table();
	std::unordered_map<std::string, std::string> identity_map;

	if (identity_table)
	{
		const RE2 valid_name_re(R"(^[^\n<>]+<[^<>\n]+>$)");
		for (auto&& [key, value] : *identity_table)
		{
			const auto git_identity = value.value<std::string>();
			if (!git_identity || !RE2::FullMatch(*git_identity, valid_name_re))
			{
				std::cerr << fmt::format(
				    "Git identity for SVN user \"{}\" should be in the format "
				    "\"Firstname Lastname <email@domain.com>\"\n",
				    key.str());
				return 1;
			}
			identity_map[std::string(key.str())] = *git_identity;
		}
	}

	if (identity_map.size() == 0 && !override_domain)
	{
		std::cerr << "Please provide an identity map or a domain.\n";
		return 1;
	}

	if (identity_map.size() == 0)
	{
		std::cerr << "Warning, no identity map provided. Git author information will be "
			     "inaccurate.\n";
	}

	if (!override_domain)
	{
		std::cerr << "Warning, no domain provided. Any SVN users not present in the "
			     "identity map will cause the program to terminate with an error.\n";
	}

	return 0;
}
