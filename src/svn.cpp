#include "svn.hpp"
#include "utils.hpp"

#include <apr_hash.h>
#include <apr_pools.h>
#include <svn_dirent_uri.h>
#include <svn_error.h>
#include <svn_fs.h>
#include <svn_io.h>
#include <svn_path.h>
#include <svn_props.h>
#include <svn_repos.h>
#include <svn_string.h>
#include <svn_types.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

std::optional<std::string> HashGet(apr_hash_t* hash, const char* key)
{
	auto* value = static_cast<svn_string_t*>(apr_hash_get(hash, key, APR_HASH_KEY_STRING));
	if (value)
	{
		return std::string(value->data, value->len);
	}
	return {};
}

namespace svn
{

using FileCallback = std::function<void(const char* path)>;

void WalkAllChildren(
	svn_fs_root_t* root, const char* path, apr_pool_t* pool, const FileCallback& callback
)
{
	apr_hash_t* entries = nullptr;
	svn_error_t* err = nullptr;
	err = svn_fs_dir_entries(&entries, root, path, pool);
	if (err)
	{
		svn_error_clear(err);
	}

	for (apr_hash_index_t* hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
	{
		const char* name = nullptr;
		svn_fs_dirent_t* dirent = nullptr;
		apr_hash_this(
			hi, reinterpret_cast<const void**>(&name), nullptr, reinterpret_cast<void**>(&dirent)
		);

		const char* childPath = svn_dirent_join(path, name, pool);

		if (dirent->kind == svn_node_dir)
		{
			WalkAllChildren(root, childPath, pool, callback);
		}
		else
		{
			callback(childPath);
		}
	}
}

Repository::Repository(const std::string& path)
{
	const svn_error_t* err =
		svn_repos_open3(&mRepos, path.c_str(), nullptr, mRepositoryPool, mRepositoryPool);
	assert(!err);
	mFs = svn_repos_fs(mRepos);

	assert(mFs);
}

long int Repository::GetYoungestRevision()
{
	Pool pool;
	long int youngestRev = 1;

	const svn_error_t* err = svn_fs_youngest_rev(&youngestRev, mFs, pool);
	assert(!err);

	return youngestRev;
}

Revision Repository::GetRevision(long int revision)
{
	return {mFs, revision};
}

Revision::Revision(svn_fs_t* repositoryFs, long int revision) :
	mRevNum(revision)
{
	svn_error_t* err = nullptr;

	svn_fs_root_t* revisionFs = nullptr;
	err = svn_fs_revision_root(&revisionFs, repositoryFs, mRevNum, mRevisionPool);
	assert(!err);

	apr_hash_t* revProps = nullptr;
	err = svn_fs_revision_proplist2(
		&revProps, repositoryFs, mRevNum, false, mRevisionPool, mRevisionPool
	);
	assert(!err);

	static constexpr const char* kEpoch = "1970-01-01T00:00:00Z";
	mAuthor = HashGet(revProps, SVN_PROP_REVISION_AUTHOR).value_or("");
	mLog = HashGet(revProps, SVN_PROP_REVISION_LOG).value_or("");
	mDate = HashGet(revProps, SVN_PROP_REVISION_DATE).value_or(kEpoch);

	svn_fs_path_change_iterator_t* changesIt = nullptr;
	err = svn_fs_paths_changed3(&changesIt, revisionFs, mRevisionPool, mRevisionPool);
	assert(!err);

	svn_fs_path_change3_t* change = nullptr;
	while ((err = svn_fs_path_change_get(&change, changesIt)) == SVN_NO_ERROR && change)
	{
		assert(change->node_kind == svn_node_file || change->node_kind == svn_node_dir);

		const bool isDir = change->node_kind == svn_node_dir;
		const std::string path = {change->path.data, change->path.len};

		auto& file = mFiles.emplace_back(revisionFs, path, isDir);

		file.changeType = static_cast<File::Change>(change->change_kind);

		// The SVN API lies! copyfrom_known does not always imply copyfrom_path and copyfrom_rev are
		// valid!!!
		if (change->copyfrom_known && change->copyfrom_path && change->copyfrom_rev != -1)
		{
			file.copiedFrom = {.path = change->copyfrom_path, .rev = change->copyfrom_rev};
		}

		if (file.copiedFrom.has_value() && file.isDirectory)
		{
			WalkAllChildren(
				revisionFs, path.c_str(), mRevisionPool, [&](const char* subFilePath)
				{ mFiles.emplace_back(revisionFs, subFilePath, false); }
			);
		}
	}
	if (err)
	{
		svn_error_clear(err);
	}
}

File::File(svn_fs_root_t* revisionFs, const std::string& path, bool isDirectory) :
	path(path),
	isDirectory(isDirectory),
	mRevisionFs(revisionFs)
{
	svn_error_t* err = nullptr;
	svn::Pool pool;

	apr_hash_t* props = nullptr;
	err = svn_fs_node_proplist(&props, revisionFs, path.c_str(), pool);
	if (err)
	{
		svn_error_clear(err);
	}

	if (props)
	{
		for (apr_hash_index_t* hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
		{
			const void* key = nullptr;
			void* val = nullptr;
			apr_hash_this(hi, &key, nullptr, &val);

			std::string_view propName{static_cast<const char*>(key)};
			auto* propValue = static_cast<svn_string_t*>(val);

			if (propName == SVN_PROP_EXECUTABLE)
			{
				isExecutable = true;
				continue;
			}
			else if (propName == SVN_PROP_MIME_TYPE)
			{
				isBinary = svn_mime_type_is_binary(propValue->data);
			}
			else if (propName == SVN_PROP_SPECIAL)
			{
				isSymlink = true;
			}
			else if (propName == SVN_PROP_EXTERNALS)
			{
				Log("WARNING: svn external {:?} in {} is not supported in git", propValue->data,
					path.c_str());
				continue;
			}
		}
	}

	svn_filesize_t fileSize = 0;
	err = svn_fs_file_length(&fileSize, mRevisionFs, path.c_str(), pool);
	if (!err)
	{
		size = static_cast<size_t>(fileSize);
		assert(fileSize >= 0);
	}
	else
	{
		svn_error_clear(err);
	}
}

std::unique_ptr<char[]> File::GetContents() const
{
	if (size == 0)
	{
		return nullptr;
	}
	const svn_error_t* err = nullptr;
	svn::Pool pool;

	svn_stream_t* contentStream = nullptr;
	err = svn_fs_file_contents(&contentStream, mRevisionFs, path.c_str(), pool);
	assert(!err);

	std::unique_ptr<char[]> fileBuffer = std::make_unique<char[]>(size);

	size_t readSize = size;
	err = svn_stream_read_full(contentStream, fileBuffer.get(), &readSize);
	assert(!err);
	assert(readSize == size);

	return fileBuffer;
}

} // namespace svn
