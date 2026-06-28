#pragma once
#include <apr_pools.h>
#include <svn_fs.h>
#include <svn_pools.h>
#include <svn_repos.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace svn
{

class Revision;

class Pool
{
	apr_pool_t* ptr = nullptr;

public:
	Pool() :
		ptr(svn_pool_create(nullptr))
	{
	}
	~Pool()
	{
		if (ptr)
		{
			svn_pool_destroy(ptr);
		}
	}

	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;

	Pool(Pool&& other) noexcept :
		ptr(other.ptr)
	{
		other.ptr = nullptr;
	}
	Pool& operator=(Pool&& other) noexcept
	{
		if (this != &other)
		{
			if (ptr)
			{
				svn_pool_destroy(ptr);
			}
			ptr = other.ptr;
			other.ptr = nullptr;
		}
		return *this;
	}

	operator apr_pool_t*() const { return ptr; }

	void clear() { svn_pool_clear(ptr); }
};

struct File
{
	enum class Change : std::uint8_t
	{
		Modify = svn_fs_path_change_modify,
		Add = svn_fs_path_change_add,
		Delete = svn_fs_path_change_delete,
		Replace = svn_fs_path_change_replace
	};

	struct CopyFrom
	{
		std::string path;
		long int rev;
	};

	static std::expected<File, std::string>
	Create(svn_fs_root_t* revisionFs, const std::string& path, bool isDirectory, Change changeType);

	std::expected<std::unique_ptr<char[]>, std::string> GetContents() const;

	std::string path;
	bool isDirectory = false;
	bool isExecutable = false;
	bool isSymlink = false;
	bool isBinary = false;
	Change changeType = Change::Add;
	size_t size = 0;
	std::optional<CopyFrom> copiedFrom;

	svn_fs_root_t* mRevisionFs = nullptr;

private:
	File() {}
};

class Repository
{
public:
	static std::expected<Repository, std::string> Open(const std::string& path);
	std::expected<long int, std::string> GetYoungestRevision();
	std::expected<Revision, std::string> GetRevision(long int revision);

private:
	Repository() = default;

	Pool mRepositoryPool;
	svn_repos_t* mRepos = nullptr;
	svn_fs_t* mFs = nullptr;
};

class Revision
{
public:
	const std::string& GetAuthor() const { return mAuthor; }
	const std::string& GetLog() const { return mLog; }
	const std::string& GetDate() const { return mDate; }
	long int GetNumber() const { return mRevNum; }
	std::span<const File> GetFiles() const { return mFiles; }

private:
	explicit Revision(long int revision) :
		mRevNum(revision)
	{
	}

	static std::expected<Revision, std::string> Create(svn_fs_t* repositoryFs, long int revision);

	long int mRevNum;
	std::string mAuthor;
	std::string mLog;
	std::string mDate;
	std::vector<File> mFiles;

	svn::Pool mRevisionPool;

	friend class Repository;
};

} // namespace svn
