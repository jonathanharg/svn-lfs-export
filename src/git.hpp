#pragma once
#include "config.hpp"
#include "svn.hpp"

#include <string>

std::expected<void, std::string> WriteGitCommit(const Config& config, const svn::Revision& rev);
