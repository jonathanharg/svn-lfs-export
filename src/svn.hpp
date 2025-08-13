#pragma once
#include <apr_pools.h>
#include <optional>
#include <span>
#include <string>
#include <svn_fs.h>
#include <svn_pools.h>
#include <svn_repos.h>
#include <vector>

namespace svn
{

class Pool
{
	apr_pool_t* ptr = nullptr;

public:
	Pool() : ptr(svn_pool_create(nullptr)) {}
	~Pool() { svn_pool_destroy(ptr); }

	Pool(const Pool&) = delete;
	Pool(Pool&&) = delete;

	Pool& operator=(const Pool&) = delete;
	Pool& operator=(Pool&&) = delete;

	[[nodiscard]] operator apr_pool_t*() const { return ptr; }

	void clear() { svn_pool_clear(ptr); }
};

struct File
{
	enum class Change : std::uint8_t
	{
		Modify = svn_fs_path_change_modify,
		Add = svn_fs_path_change_add,
		Delete = svn_fs_path_change_delete
	};

	struct CopyFrom
	{
		std::string path;
		long int rev;
	};

	std::string path;
	bool isDirectory;
	Change changeType;
	size_t size;
	std::unique_ptr<char[]> buffer;
	std::optional<CopyFrom> copiedFrom;
};

class Revision
{
public:
	const std::string& GetAuthor() { return mAuthor; };
	const std::string& GetLog() { return mLog; };
	const std::string& GetDate() { return mDate; };
	const std::span<const File> GetFiles() { return mFiles; };

private:
	Revision(svn_fs_t* repositoryFs, long int revision);
	void SetupProperties();
	void SetupFiles();

	long int mRevision;
	Pool mPool;
	// The lifetime of this is a bit worrying, it's dependent on the lifetime of the parent
	// Repository. TODO: This should probably be some kind of shared pointer
	svn_fs_t* mRepositoryFs;
	svn_fs_root_t* mRevisionFs;
	std::string mAuthor;
	std::string mLog;
	std::string mDate;
	std::vector<File> mFiles;

	friend class Repository;
};

class Repository
{
public:
	Repository(const std::string& path);
	long int GetYoungestRevision();
	Revision GetRevision(long int revision);

private:
	Pool mPool;
	svn_repos_t* mRepos = nullptr;
	svn_fs_t* mFs = nullptr;
};
} // namespace svn
