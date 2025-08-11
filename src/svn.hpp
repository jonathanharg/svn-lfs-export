#include <apr_pools.h>
#include <svn_pools.h>

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
} // namespace svn
