#pragma once
#include "config.hpp"
#include "svn.hpp"

#include <expected>
#include <string>

std::string GetGitAuthor(const Config& config, const std::string& username);
std::expected<void, std::string> WriteGitCommit(const Config& config, const svn::Revision& rev);
