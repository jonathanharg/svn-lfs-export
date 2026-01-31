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
#include <tracy/Tracy.hpp>

#include <cassert>
#include <cstddef>
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

Repository::Repository(const std::string& path)
{
	ZoneScoped;

	Pool scratchPool;

	[[maybe_unused]]
	const svn_error_t* err = svn_repos_open3(&mRepos, path.c_str(), nullptr, mPool, scratchPool);
	mFs = svn_repos_fs(mRepos);

	assert(mFs);
}

long int Repository::GetYoungestRevision()
{
	ZoneScoped;

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

Revision::Revision(svn_fs_t* repositoryFs, long int revision) :
	mRevNum(revision)
{
	ZoneScoped;

	[[maybe_unused]]
	const svn_error_t* err = nullptr;

	Pool resultPool;
	Pool scratchPool;

	svn_fs_root_t* revisionFs = nullptr;
	err = svn_fs_revision_root(&revisionFs, repositoryFs, mRevNum, mRootPool);
	assert(!err);

	apr_hash_t* revProps = nullptr;
	err =
		svn_fs_revision_proplist2(&revProps, repositoryFs, mRevNum, false, resultPool, scratchPool);
	assert(!err);

	static constexpr const char* kEpoch = "1970-01-01T00:00:00Z";
	mAuthor = HashGet(revProps, SVN_PROP_REVISION_AUTHOR).value_or("");
	mLog = HashGet(revProps, SVN_PROP_REVISION_LOG).value_or("");
	mDate = HashGet(revProps, SVN_PROP_REVISION_DATE).value_or(kEpoch);

	svn_fs_path_change_iterator_t* changesIt = nullptr;
	err = svn_fs_paths_changed3(&changesIt, revisionFs, resultPool, scratchPool);
	assert(!err);

	svn_fs_path_change3_t* change = nullptr;
	while ((err = svn_fs_path_change_get(&change, changesIt)) == SVN_NO_ERROR && change)
	{
		assert(change->node_kind == svn_node_file || change->node_kind == svn_node_dir);

		const bool isDir = change->node_kind == svn_node_dir;
		const std::string path = {change->path.data, change->path.len};

		auto& file = mFiles.emplace_back(revisionFs, std::move(path), isDir);

		file.changeType = static_cast<File::Change>(change->change_kind);

		// The SVN API lies! copyfrom_known does not always imply copyfrom_path and copyfrom_rev are
		// valid!!!
		if (change->copyfrom_known && change->copyfrom_path && change->copyfrom_rev != -1)
		{
			file.copiedFrom = {.path = change->copyfrom_path, .rev = change->copyfrom_rev};
		}
	}
}

File::File(svn_fs_root_t* revisionFs, std::string path, bool isDirectory) :
	path(path),
	isDirectory(isDirectory),
	mRevisionFs(revisionFs)
{
	ZoneScoped;
	[[maybe_unused]] const svn_error_t* err = nullptr;
	svn::Pool fileMetadataPool;

	apr_hash_t* props = nullptr;
	err = svn_fs_node_proplist(&props, revisionFs, path.c_str(), fileMetadataPool);

	if (props)
	{
		for (apr_hash_index_t* hi = apr_hash_first(fileMetadataPool, props); hi;
			 hi = apr_hash_next(hi))
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
				LogError(
					"Warning: svn external {:?} in {} is not supported in git", propValue->data,
					path.c_str()
				);
				continue;
			}
		}
	}

	svn_filesize_t fileSize = 0;
	err = svn_fs_file_length(&fileSize, mRevisionFs, path.c_str(), fileMetadataPool);
	if (!err)
	{
		size = static_cast<size_t>(fileSize);
		assert(fileSize >= 0);
		assert(static_cast<uint64_t>(fileSize) <= std::numeric_limits<size_t>::max());
	}
}

std::unique_ptr<char[]> File::GetContents() const
{
	ZoneScoped;

	if (size == 0)
	{
		return nullptr;
	}
	[[maybe_unused]] const svn_error_t* err = nullptr;
	svn::Pool filePool;

	svn_stream_t* contentStream = nullptr;
	err = svn_fs_file_contents(&contentStream, mRevisionFs, path.c_str(), filePool);
	assert(!err);

	std::unique_ptr<char[]> fileBuffer = std::make_unique<char[]>(size);

	size_t readSize = size;
	err = svn_stream_read_full(contentStream, fileBuffer.get(), &readSize);
	assert(!err);
	assert(readSize == size);

	return fileBuffer;
}

} // namespace svn
