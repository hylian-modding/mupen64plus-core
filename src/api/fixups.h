#ifndef ML64_FIXUPS
#define ML64_FIXUPS

#ifdef NOT_MODLOADER
#define VADDR_MASK 0x00FFFFFF
#define MLEVENT(ML, VANILLA) VANILLA
#else
#define VADDR_MASK 0x0FFFFFFF // so that we can index an additional nibble of memory
#define MLEVENT(ML, VANILLA) NotifyEvent(ML)
#endif

#endif
