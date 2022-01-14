#ifndef ML64_FIXUPS
#define ML64_FIXUPS

#define MAX_PAGE (RDRAM_MAX_SIZE / 0x1000)

#ifdef NOT_MODLOADER
#define VADDR_MASK 0x00FFFFFF
#define MAX_VADDR (0x80000000 + RDRAM_8MB_SIZE)
#define MLEVENT(ML, VANILLA) VANILLA
#else
#define VADDR_MASK 0x0FFFFFFF // so that we can index an additional nibble of memory
#define MAX_VADDR (0x80000000 + RDRAM_MAX_SIZE)
#define MLEVENT(ML, VANILLA) NotifyEvent(ML)
#endif

#endif
