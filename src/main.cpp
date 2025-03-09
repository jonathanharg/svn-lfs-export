#include <argparse/argparse.hpp>
#include <fmt/core.h>
#include <optional>
#include <toml++/toml.hpp>

int main(int argc, char* argv[])
{
	argparse::ArgumentParser program("svn-lfs-export");
	toml::parse_result config_result = toml::parse_file("config.toml");

	if (!config_result)
	{
		std::cerr << "Failed to parse config.toml - " << config_result.error() << '\n';
		return 1;
	}

	const toml::table& config = config_result.table();

	auto svn_path = config["repository"].value<std::string>();
	auto min_revision = config["min_revision"].value<long int>();
	auto max_revision = config["max_revision"].value<long int>();
	auto override_domain = config["domain"].value<std::string>();

	if (!svn_path)
	{
		std::cerr << "Failed to parse the SVN repository string. Make sure a valid path to a on-disk SVN repository is provided.\n";
		return 1;
	}

        return 0;
}
