#include <apr_pools.h>
#include <svn_pools.h>

class SVNPool
{
	apr_pool_t* ptr = nullptr;

public:
	SVNPool() : ptr(svn_pool_create(nullptr)) {}
	~SVNPool() { svn_pool_destroy(ptr); }

	SVNPool(const SVNPool&) = delete;
	SVNPool(SVNPool&&) = delete;

	SVNPool& operator=(const SVNPool&) = delete;
	SVNPool& operator=(SVNPool&&) = delete;

	[[nodiscard]] operator apr_pool_t*() const { return ptr; }

	void clear() { svn_pool_clear(ptr); }
};
