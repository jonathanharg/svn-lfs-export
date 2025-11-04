#include "svn.hpp"
#include "utils.hpp"

#include <apr_hash.h>
#include <svn_fs.h>
#include <svn_io.h>
#include <svn_props.h>
#include <svn_repos.h>
#include <svn_string.h>
#include <svn_types.h>
#include <svn_types_impl.h>

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace svn
{

File::File(svn_fs_path_change3_t* change, svn_fs_root_t* revisionFs) :
	path(change->path.data, change->path.len),
	isDirectory(change->node_kind == svn_node_dir),
	isExecutable(false),
	changeType(static_cast<File::Change>(change->change_kind))
{
	[[maybe_unused]] const svn_error_t* err = nullptr;
	svn::Pool propsPool;

	apr_hash_t* props = nullptr;
	err = svn_fs_node_proplist(&props, revisionFs, change->path.data, propsPool);

	if (props)
	{
		for (apr_hash_index_t* hi = apr_hash_first(propsPool, props); hi; hi = apr_hash_next(hi))
		{
			const void* key = nullptr;
			void* val = nullptr;
			apr_hash_this(hi, &key, nullptr, &val);

			std::string_view propName{static_cast<const char*>(key)};
			auto* propValue = static_cast<svn_string_t*>(val);

			if (propName == "svn:executable")
			{
				isExecutable = true;
				continue;
			}
			else if (propName == "svn:externals")
			{
				LogError(
					"Warning: svn external {:?} in {} is not supported in git", propValue->data,
					change->path.data
				);
				continue;
			}
			else if (propName == "svn:mergeinfo" || propName == "svn:keywords" ||
					 propName == "svn:eol-style" || propName == "svn:mime-type" ||
					 propName == "svn:ignore")
			{
				continue;
			}

			LogError(
				"Warning: Ignoring prop {} = {:?} ({})", propName, propValue->data,
				change->path.data
			);
		}
	}

	assert(change->node_kind == svn_node_file || change->node_kind == svn_node_dir);

	// The SVN API lies! copyfrom_known does not always imply copyfrom_path and copyfrom_rev are
	// valid!!!
	if (change->copyfrom_known && change->copyfrom_path && change->copyfrom_rev != -1)
	{
		copiedFrom = {.path = change->copyfrom_path, .rev = change->copyfrom_rev};
	}

	if (!isDirectory && changeType != File::Change::Delete)
	{
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
	const svn_error_t* err = svn_fs_revision_root(&revisionFs, repositoryFs, mRevision, rootPool);
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
	const svn_error_t* err = svn_fs_revision_proplist2(
		&revProps, repositoryFs, mRevision, false, resultPool, scratchPool
	);
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
	[[maybe_unused]] const svn_error_t* err = nullptr;

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
	const svn_error_t* err = svn_repos_open3(&mRepos, path.c_str(), nullptr, mPool, scratchPool);
	mFs = svn_repos_fs(mRepos);

	assert(mFs);
}

long int Repository::GetYoungestRevision()
{
	Pool scratchPool;
	long int youngestRev = 1;

	[[maybe_unused]]
	const svn_error_t* err = svn_fs_youngest_rev(&youngestRev, mFs, scratchPool);
	assert(!err);

	return youngestRev;
}

Revision Repository::GetRevision(long int revision)
{
	return {mFs, revision};
}
} // namespace svn
