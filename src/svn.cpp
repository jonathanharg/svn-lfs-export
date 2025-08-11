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

Revision::Revision(svn_fs_t* repositoryFs, long int revision)
	: mRevision(revision), mRepositoryFs(repositoryFs), mRevisionFs(nullptr)
{
	[[maybe_unused]]
	svn_error_t* err = svn_fs_revision_root(&mRevisionFs, mRepositoryFs, mRevision, mPool);
	assert(!err);

	SetupProperties();
	SetupFiles();
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

void Revision::SetupProperties()
{
	Pool resultPool;
	Pool scratchPool;

	apr_hash_t* revProps = nullptr;
	[[maybe_unused]]
	svn_error_t* err = svn_fs_revision_proplist2(&revProps, mRepositoryFs, mRevision, false,
						     resultPool, scratchPool);
	assert(!err);

	static constexpr const char* kEpoch = "1970-01-01T00:00:00Z";
	mAuthor = GetRevisionProp(revProps, SVN_PROP_REVISION_AUTHOR).value_or("");
	mLog = GetRevisionProp(revProps, SVN_PROP_REVISION_LOG).value_or("");
	mDate = GetRevisionProp(revProps, SVN_PROP_REVISION_DATE).value_or(kEpoch);
}

void Revision::SetupFiles()
{
	svn::Pool scratchPool;
	[[maybe_unused]] svn_error_t* err = nullptr;

	svn_fs_path_change_iterator_t* it = nullptr;
	err = svn_fs_paths_changed3(&it, mRevisionFs, mPool, scratchPool);
	assert(!err);

	svn_fs_path_change3_t* changes = nullptr;
	err = svn_fs_path_change_get(&changes, it);
	assert(!err);

	while (changes)
	{
		std::string path{changes->path.data, changes->path.len};

		assert(changes->node_kind == svn_node_file || changes->node_kind == svn_node_dir);
		bool isDirectory = changes->node_kind == svn_node_dir;

		assert(changes->change_kind == svn_fs_path_change_modify ||
		       changes->change_kind == svn_fs_path_change_add ||
		       changes->change_kind == svn_fs_path_change_delete);

		auto changeType = static_cast<File::Change>(changes->change_kind);

		// Don't care for now
		// changes->text_mod;
		// changes->prop_mod;
		// changes->copyfrom_known;
		// changes->mergeinfo_mod;

		svn_filesize_t signedFileSize = 0;
		err = svn_fs_file_length(&signedFileSize, mRevisionFs, path.c_str(), mPool);
		assert(!err);

		// NOTE: This will overflow for files > 4GB on 32bit systems, don't care
		auto fileSize = static_cast<size_t>(signedFileSize);

		std::unique_ptr<char[]> buffer;

		if (fileSize > 0)
		{
			svn_stream_t* content = nullptr;
			err = svn_fs_file_contents(&content, mRevisionFs, path.c_str(), mPool);
			assert(!err);

			buffer = std::make_unique<char[]>(fileSize);

			size_t readSize = fileSize;
			err = svn_stream_read_full(content, buffer.get(), &readSize);
			assert(!err);
		}

		mFiles.emplace_back(std::move(path), isDirectory, changeType, fileSize,
				    std::move(buffer));

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
