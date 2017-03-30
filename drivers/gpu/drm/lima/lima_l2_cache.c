#include "lima.h"

#define LIMA_L2_CACHE_SIZE		 0x0004
#define LIMA_L2_CACHE_STATUS		 0x0008
#define   LIMA_L2_CACHE_STATUS_COMMAND_BUSY  (1 << 0)
#define   LIMA_L2_CACHE_STATUS_DATA_BUSY     (1 << 1)
#define LIMA_L2_CACHE_COMMAND		 0x0010
#define   LIMA_L2_CACHE_COMMAND_CLEAR_ALL    (1 << 0)
#define LIMA_L2_CACHE_CLEAR_PAGE	 0x0014
#define LIMA_L2_CACHE_MAX_READS		 0x0018
#define LIMA_L2_CACHE_ENABLE		 0x001C
#define   LIMA_L2_CACHE_ENABLE_ACCESS        (1 << 0)
#define   LIMA_L2_CACHE_ENABLE_READ_ALLOCATE (1 << 1)
#define LIMA_L2_CACHE_PERFCNT_SRC0	 0x0020
#define LIMA_L2_CACHE_PERFCNT_VAL0	 0x0024
#define LIMA_L2_CACHE_PERFCNT_SRC1	 0x0028
#define LIMA_L2_CACHE_ERFCNT_VAL1	 0x002C

#define l2_cache_write(reg, data) writel(data, l2_cache->ip.iomem + LIMA_L2_CACHE_##reg)
#define l2_cache_read(reg) readl(l2_cache->ip.iomem + LIMA_L2_CACHE_##reg)

int lima_l2_cache_init(struct lima_l2_cache *l2_cache)
{
	u32 size;
	struct lima_device *dev = l2_cache->ip.dev;
	int timeout;

	size = l2_cache_read(SIZE);
	dev_info(dev->dev, "l2 cache %uK, %u-way, %ubyte cache line, %ubit external bus\n",
		 1 << (((size >> 16) & 0xff) - 10),
		 1 << ((size >> 8) & 0xff),
		 1 << (size & 0xff),
		 1 << ((size >> 24) & 0xff));

	l2_cache_write(COMMAND, LIMA_L2_CACHE_COMMAND_CLEAR_ALL);
	for (timeout = 100000; timeout > 0; timeout--) {
	    if (!(l2_cache_read(STATUS) & LIMA_L2_CACHE_STATUS_COMMAND_BUSY))
		break;
	}
	if (!timeout) {
	    dev_err(dev->dev, "l2 cache wait command timeout\n");
	    return -ETIMEDOUT;
	}

	l2_cache_write(ENABLE, LIMA_L2_CACHE_ENABLE_ACCESS | LIMA_L2_CACHE_ENABLE_READ_ALLOCATE);
	l2_cache_write(MAX_READS, 0x1c);

	return 0;
}

void lima_l2_cache_fini(struct lima_l2_cache *l2_cache)
{

}
