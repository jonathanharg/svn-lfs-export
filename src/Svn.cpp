#include "Svn.hpp"
#include "Utils.hpp"

#include <apr_hash.h>
#include <apr_pools.h>
#include <fmt/format.h>
#include <svn_dirent_uri.h>
#include <svn_error.h>
#include <svn_fs.h>
#include <svn_io.h>
#include <svn_path.h>
#include <svn_props.h>
#include <svn_repos.h>
#include <svn_string.h>
#include <svn_types.h>

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
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

std::string
FormatSvnError(svn_error_t* err, std::source_location loc = std::source_location::current())
{
	char buf[256];
	const char* message = svn_err_best_message(err, buf, sizeof(buf));
	std::string result =
		fmt::format("{} ({}:{} in {})", message, loc.file_name(), loc.line(), loc.function_name());
	svn_error_clear(err);
	return result;
}

using FileCallback = std::function<std::expected<void, std::string>(const char* path)>;

std::expected<void, std::string> WalkAllChildren(
	svn_fs_root_t* root, const char* path, apr_pool_t* pool, const FileCallback& callback
)
{
	apr_hash_t* entries = nullptr;
	svn_error_t* err = svn_fs_dir_entries(&entries, root, path, pool);
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
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
			if (auto r = WalkAllChildren(root, childPath, pool, callback); !r)
			{
				return r;
			}
		}
		else
		{
			if (auto r = callback(childPath); !r)
			{
				return r;
			}
		}
	}
	return {};
}

std::expected<Repository, std::string> Repository::Open(const std::string& path)
{
	Repository repo;
	svn_error_t* err = svn_repos_open3(
		&repo.mRepos, path.c_str(), nullptr, repo.mRepositoryPool, repo.mRepositoryPool
	);
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
	}
	repo.mFs = svn_repos_fs(repo.mRepos);

	return repo;
}

std::expected<long int, std::string> Repository::GetYoungestRevision()
{
	Pool pool;
	long int youngestRev = 1;

	svn_error_t* err = svn_fs_youngest_rev(&youngestRev, mFs, pool);
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
	}

	return youngestRev;
}

std::expected<Revision, std::string> Repository::GetRevision(long int revision)
{
	return Revision::Create(mFs, revision);
}

std::expected<Revision, std::string> Revision::Create(svn_fs_t* repositoryFs, long int revision)
{
	Revision rev(revision);
	svn_error_t* err = nullptr;

	svn_fs_root_t* revisionFs = nullptr;
	err = svn_fs_revision_root(&revisionFs, repositoryFs, rev.mRevNum, rev.mRevisionPool);
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
	}

	apr_hash_t* revProps = nullptr;
	err = svn_fs_revision_proplist2(
		&revProps, repositoryFs, rev.mRevNum, false, rev.mRevisionPool, rev.mRevisionPool
	);
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
	}

	static constexpr const char* kEpoch = "1970-01-01T00:00:00Z";
	rev.mAuthor = HashGet(revProps, SVN_PROP_REVISION_AUTHOR).value_or("");
	rev.mLog = HashGet(revProps, SVN_PROP_REVISION_LOG).value_or("");
	rev.mDate = HashGet(revProps, SVN_PROP_REVISION_DATE).value_or(kEpoch);

	svn_fs_path_change_iterator_t* changesIt = nullptr;
	err = svn_fs_paths_changed3(&changesIt, revisionFs, rev.mRevisionPool, rev.mRevisionPool);
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
	}

	svn_fs_path_change3_t* change = nullptr;
	while ((err = svn_fs_path_change_get(&change, changesIt)) == SVN_NO_ERROR && change)
	{
		if (change->node_kind != svn_node_file && change->node_kind != svn_node_dir)
		{
			return std::unexpected(
				fmt::format(
					"Unexpected node kind {} for path {}", static_cast<int>(change->node_kind),
					std::string_view{change->path.data, change->path.len}
				)
			);
		}

		const bool isDir = change->node_kind == svn_node_dir;
		const std::string path = {change->path.data, change->path.len};

		auto maybeFile =
			File::Create(revisionFs, path, isDir, static_cast<File::Change>(change->change_kind));
		if (!maybeFile)
		{
			return std::unexpected(maybeFile.error());
		}
		auto& file = rev.mFiles.emplace_back(std::move(*maybeFile));

		// The SVN API lies! copyfrom_known does not always imply copyfrom_path and copyfrom_rev are
		// valid!!!
		if (change->copyfrom_known && change->copyfrom_path && change->copyfrom_rev != -1)
		{
			file.copiedFrom = {.path = change->copyfrom_path, .rev = change->copyfrom_rev};
		}

		if (file.copiedFrom.has_value() && file.isDirectory)
		{
			auto walk = WalkAllChildren(
				revisionFs, path.c_str(), rev.mRevisionPool,
				[&](const char* subFilePath) -> std::expected<void, std::string>
				{
					auto maybeChild =
						File::Create(revisionFs, subFilePath, false, File::Change::Add);
					if (!maybeChild)
					{
						return std::unexpected(maybeChild.error());
					}
					rev.mFiles.emplace_back(std::move(*maybeChild));
					return {};
				}
			);
			if (!walk)
			{
				return std::unexpected(walk.error());
			}
		}
	}
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
	}

	return rev;
}

std::expected<File, std::string> File::Create(
	svn_fs_root_t* revisionFs, const std::string& path, bool isDirectory, Change changeType
)
{
	File self;
	self.mRevisionFs = revisionFs;
	self.path = path;
	self.isDirectory = isDirectory;
	self.changeType = changeType;
	svn_error_t* err = nullptr;
	svn::Pool pool;

	if (self.changeType == Change::Delete)
	{
		return self;
	}

	apr_hash_t* props = nullptr;
	err = svn_fs_node_proplist(&props, revisionFs, path.c_str(), pool);
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
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
				self.isExecutable = true;
				continue;
			}
			else if (propName == SVN_PROP_MIME_TYPE)
			{
				self.isBinary = svn_mime_type_is_binary(propValue->data);
			}
			else if (propName == SVN_PROP_SPECIAL)
			{
				self.isSymlink = true;
			}
			else if (propName == SVN_PROP_EXTERNALS)
			{
				Log("WARNING: svn external {:?} in {} is not supported in git", propValue->data,
					path.c_str());
				continue;
			}
		}
	}

	if (!isDirectory)
	{
		svn_filesize_t fileSize = 0;
		err = svn_fs_file_length(&fileSize, self.mRevisionFs, path.c_str(), pool);
		if (err)
		{
			return std::unexpected(FormatSvnError(err));
		}
		self.size = static_cast<size_t>(fileSize);
	}

	return self;
}

std::expected<std::unique_ptr<char[]>, std::string> File::GetContents() const
{
	if (size == 0)
	{
		return nullptr;
	}
	svn_error_t* err = nullptr;
	svn::Pool pool;

	svn_stream_t* contentStream = nullptr;
	err = svn_fs_file_contents(&contentStream, mRevisionFs, path.c_str(), pool);
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
	}

	std::unique_ptr<char[]> fileBuffer = std::make_unique<char[]>(size);

	size_t readSize = size;
	err = svn_stream_read_full(contentStream, fileBuffer.get(), &readSize);
	if (err)
	{
		return std::unexpected(FormatSvnError(err));
	}
	if (readSize != size)
	{
		return std::unexpected(
			fmt::format("Short read of {}: expected {} bytes, got {}", path, size, readSize)
		);
	}

	return fileBuffer;
}

} // namespace svn
