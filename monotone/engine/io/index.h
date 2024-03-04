#pragma once

//
// monotone
//
// time-series storage
//

typedef struct IndexRegion IndexRegion;
typedef struct Index       Index;
typedef struct IndexEof    IndexEof;

#define INDEX_MAGIC 0x20849615

struct IndexRegion
{
	uint32_t offset;
	uint32_t size;
	uint32_t size_origin;
	uint32_t size_key_min;
	uint32_t size_key_max;
	uint32_t events;
	uint32_t crc;
	uint32_t reserved[4];
} packed;

struct Index
{
	uint32_t crc;
	Id       id;
	uint32_t size;
	uint64_t size_regions;
	uint64_t size_regions_origin;
	uint32_t regions;
	uint64_t events;
	uint64_t time;
	uint64_t lsn;
	uint8_t  compression;
	uint32_t reserved[4];
	// u32 offset[]
	// data
} packed;

struct IndexEof
{
	uint32_t magic;
	uint32_t size;
} packed;

static inline IndexRegion*
index_get(Index* self, int pos)
{
	assert(pos < (int)self->regions);
	auto start  = (char*)self + sizeof(Index);
	auto offset = (uint32_t*)start;
	return (IndexRegion*)(start + (sizeof(uint32_t) * self->regions) +
	                      offset[pos]);
}

static inline Event*
index_region_min(Index* self, int pos)
{
	auto region = index_get(self, pos);
	return (Event*)((char*)region + sizeof(IndexRegion));
}

static inline Event*
index_region_max(Index* self, int pos)
{
	auto region = index_get(self, pos);
	return (Event*)((char*)region + sizeof(IndexRegion) +
	                region->size_key_min);
}

static inline Event*
index_min(Index* self)
{
	return index_region_min(self, 0);
}

static inline Event*
index_max(Index* self)
{
	return index_region_max(self, self->regions - 1);
}
