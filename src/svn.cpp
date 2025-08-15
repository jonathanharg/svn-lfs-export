#include "svn.hpp"
#include <cassert>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/std.h>
#include <svn_props.h>
#include <vector>

namespace svn
{

File::File(svn_fs_path_change3_t* change, svn_fs_root_t* revisionFs) :
	path(change->path.data, change->path.len),
	isDirectory(change->node_kind == svn_node_dir),
	changeType(static_cast<File::Change>(change->change_kind))
{
	// TODO: Read all properties and notify on anything important that may get lost
	// svn_fs_node_proplist

	assert(change->node_kind == svn_node_file || change->node_kind == svn_node_dir);

	// The SVN API lies! copyfrom_known does not always imply copyfrom_path and copyfrom_rev are
	// valid!!!
	if (change->copyfrom_known && change->copyfrom_path && change->copyfrom_rev != -1)
	{
		copiedFrom = {.path = change->copyfrom_path, .rev = change->copyfrom_rev};
	}

	if (!isDirectory && changeType != File::Change::Delete)
	{
		[[maybe_unused]] svn_error_t* err = nullptr;
		svn::Pool filePool;

		svn_filesize_t signedFileSize = 0;
		err = svn_fs_file_length(&signedFileSize, revisionFs, path.c_str(), filePool);
		assert(!err);

		// NOTE: This will overflow for files > 4GB on 32bit systems, don't care
		size = static_cast<size_t>(signedFileSize);

		if (size > 0)
		{
			svn_stream_t* content = nullptr;
			err = svn_fs_file_contents(&content, revisionFs, path.c_str(), filePool);
			assert(!err);

			buffer = std::make_unique<char[]>(size);

			size_t readSize = size;
			err = svn_stream_read_full(content, buffer.get(), &readSize);
			assert(!err);
		}
	}
}

Revision::Revision(svn_fs_t* repositoryFs, long int revision) :
	mRevision(revision)
{
	Pool rootPool;
	svn_fs_root_t* revisionFs = nullptr;

	[[maybe_unused]]
	svn_error_t* err = svn_fs_revision_root(&revisionFs, repositoryFs, mRevision, rootPool);
	assert(!err);

	SetupProperties(repositoryFs);
	SetupFiles(revisionFs);
}

std::optional<std::string> GetRevisionProp(apr_hash_t* hash, const char* prop)
{
	auto* value = static_cast<svn_string_t*>(apr_hash_get(hash, prop, APR_HASH_KEY_STRING));
	if (value)
	{
		return std::string(value->data, value->len);
	}
	return {};
}

void Revision::SetupProperties(svn_fs_t* repositoryFs)
{
	Pool resultPool;
	Pool scratchPool;

	apr_hash_t* revProps = nullptr;
	[[maybe_unused]]
	svn_error_t* err = svn_fs_revision_proplist2(&revProps, repositoryFs, mRevision, false,
												 resultPool, scratchPool);
	assert(!err);

	static constexpr const char* kEpoch = "1970-01-01T00:00:00Z";
	mAuthor = GetRevisionProp(revProps, SVN_PROP_REVISION_AUTHOR).value_or("");
	mLog = GetRevisionProp(revProps, SVN_PROP_REVISION_LOG).value_or("");
	mDate = GetRevisionProp(revProps, SVN_PROP_REVISION_DATE).value_or(kEpoch);
	// TODO: Read all properties and notify on anything important that may get lost
	// apr_hash_first / apr_hash_next
}

void Revision::SetupFiles(svn_fs_root_t* revisionFs)
{
	Pool pathsPool;
	Pool scratchPool;
	[[maybe_unused]] svn_error_t* err = nullptr;

	svn_fs_path_change_iterator_t* it = nullptr;
	err = svn_fs_paths_changed3(&it, revisionFs, pathsPool, scratchPool);
	assert(!err);

	svn_fs_path_change3_t* changes = nullptr;
	err = svn_fs_path_change_get(&changes, it);
	assert(!err);

	while (changes)
	{
		mFiles.emplace_back(changes, revisionFs);

		err = svn_fs_path_change_get(&changes, it);
		assert(!err);
	}
}

Repository::Repository(const std::string& path)
{
	Pool scratchPool;

	[[maybe_unused]]
	svn_error_t* err = svn_repos_open3(&mRepos, path.c_str(), nullptr, mPool, scratchPool);
	mFs = svn_repos_fs(mRepos);

	assert(mFs);
}

long int Repository::GetYoungestRevision()
{
	Pool scratchPool;
	long int youngestRev = 1;

	[[maybe_unused]]
	svn_error_t* err = svn_fs_youngest_rev(&youngestRev, mFs, scratchPool);
	assert(!err);

	return youngestRev;
}

Revision Repository::GetRevision(long int revision)
{
	return {mFs, revision};
}
} // namespace svn
