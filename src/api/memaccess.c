#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "device/memory/memory.h"
#include "main/main.h"

#ifdef WIN32
#define bswap32(x) ((uint32_t)_byteswap_ulong(x))
#define bswap64(x) ((uint64_t)_byteswap_uint64(x))
#else
#define bswap32(x) ((uint32_t)__builtin_bswap32(x))
#define bswap64(x) ((uint32_t)__builtin_bswap64(x))
#endif

static inline uint32_t RDRAMAddrAlign(uint32_t addr)
{
	return (addr & 0x0fffffff) >> 2;
}

static inline uint32_t ROMAddrAlign(uint32_t addr)
{
	return addr / 4;
}

EXPORT uint8_t CALL ExtRDRAMRead8(uint32_t addr)
{
	const size_t offset = RDRAMAddrAlign(addr) * 4 + (3 - (addr & 3));
	return ((uint8_t*)g_dev.rdram.dram)[offset];
}

EXPORT uint16_t CALL ExtRDRAMRead16(uint32_t addr)
{
	return ((uint16_t)ExtRDRAMRead8(addr) << 8) | (uint16_t)ExtRDRAMRead8(addr + 1);
}

EXPORT uint32_t CALL ExtRDRAMRead32(uint32_t addr)
{
	return g_dev.rdram.dram[RDRAMAddrAlign(addr)];
}

EXPORT uint64_t CALL ExtRDRAMRead64(uint32_t addr)
{
	const size_t offset = RDRAMAddrAlign(addr);
	return *(uint64_t*)(&g_dev.rdram.dram[offset]);
}

EXPORT uint8_t* CALL ExtRDRAMReadBuffer(uint32_t addr, size_t len)
{
	uint8_t* buf = malloc(len);

	for (size_t i = 0; i < len; ++i)
		buf[i] = ExtRDRAMRead8(addr + i);

	return buf;
}

EXPORT void CALL ExtRDRAMWrite8(uint32_t addr, uint8_t val)
{
	const size_t offset = RDRAMAddrAlign(addr) * 4 + (3 - (addr & 3));
	((uint8_t*)g_dev.rdram.dram)[offset] = val;
}

EXPORT void CALL ExtRDRAMWrite16(uint32_t addr, uint16_t val)
{
	ExtRDRAMWrite8(addr, val >> 8);
	ExtRDRAMWrite8(addr + 1, val & 0xff);
}

EXPORT void CALL ExtRDRAMWrite32(uint32_t addr, uint32_t val)
{
	g_dev.rdram.dram[RDRAMAddrAlign(addr)] = val;
}

EXPORT void CALL ExtRDRAMWrite64(uint32_t addr, uint64_t val)
{
	const uint32_t hi = val >> 32;
	const uint32_t lo = val & 0x00000000FFFFFFFF;
	g_dev.rdram.dram[RDRAMAddrAlign(addr)] = hi;
	g_dev.rdram.dram[RDRAMAddrAlign(addr + 4)] = lo;
}

EXPORT void CALL ExtRDRAMWriteBuffer(uint32_t addr, uint8_t* buf, size_t len)
{
	for (size_t i = 0; i < len; ++i)
		ExtRDRAMWrite8(addr + i, buf[i]);
}

EXPORT uint8_t CALL ExtROMRead8(uint32_t addr)
{
	const size_t offset = ROMAddrAlign(addr) * 4 + (3 - (addr & 3));
	return ((uint8_t*)mem_base_u32(g_mem_base, MM_CART_ROM))[!g_EmulatorRunning ? addr : offset];
}

EXPORT uint16_t CALL ExtROMRead16(uint32_t addr)
{
	return ((uint16_t)ExtROMRead8(addr) << 8) | (uint16_t)ExtROMRead8(addr + 1);
}

EXPORT uint32_t CALL ExtROMRead32(uint32_t addr)
{
	uint32_t val = mem_base_u32(g_mem_base, MM_CART_ROM)[ROMAddrAlign(addr)];
	return !g_EmulatorRunning ? bswap32(val) : val;
}

EXPORT uint8_t* CALL ExtROMReadBuffer(uint32_t addr, size_t len)
{
	uint8_t* buf = malloc(len);

	if (!g_EmulatorRunning) {
		memcpy(buf, mem_base_u32(g_mem_base, MM_CART_ROM) + addr, len);
	}
	else {
		for (size_t i = 0; i < len; ++i)
			buf[i] = ExtROMRead8(addr + i);
	}

	return buf;
}

EXPORT void CALL ExtROMWrite8(uint32_t addr, uint8_t val)
{
	const size_t offset = ROMAddrAlign(addr) * 4 + (3 - (addr & 3));
	((uint8_t*)mem_base_u32(g_mem_base, MM_CART_ROM))[!g_EmulatorRunning ? addr : offset] = val;
}

EXPORT void CALL ExtROMWrite16(uint32_t addr, uint16_t val)
{
	ExtROMWrite8(addr, val >> 8);
	ExtROMWrite8(addr + 1, val & 0xff);
}

EXPORT void CALL ExtROMWrite32(uint32_t addr, uint32_t val)
{
	mem_base_u32(g_mem_base, MM_CART_ROM)[ROMAddrAlign(addr)] = !g_EmulatorRunning ? bswap32(val) : val;
}

EXPORT void CALL ExtROMWriteBuffer(uint32_t addr, uint8_t* buf, size_t len)
{
	for (size_t i = 0; i < len; ++i)
		ExtROMWrite8(addr + i, buf[i]);
}
