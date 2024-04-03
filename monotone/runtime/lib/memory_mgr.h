#pragma once

//
// monotone
//
// time-series storage
//

typedef struct Page      Page;
typedef struct MemoryMgr MemoryMgr;

struct Page
{
	int   used;
	Page* next;
	char  data[];
};

struct MemoryMgr
{
	Spinlock lock;
	int      count;
	Page*    free_list;
	int      free_list_count;
	int      page_size;
	bool     limit;
	bool     limit_error;
	uint64_t limit_wm;
};

static inline void
memory_mgr_init(MemoryMgr* self)
{
	self->count           = 0;
	self->free_list       = NULL;
	self->free_list_count = 0;
	self->page_size       = 2097152 - sizeof(Page);
	self->limit           = false;
	self->limit_error     = false;
	self->limit_wm        = 8589934592; // 8GiB
	spinlock_init(&self->lock);
}

static inline void
memory_mgr_set(MemoryMgr* self, int page_size,
               bool       limit,
               Str*       limit_behaviour,
               uint64_t   limit_wm)
{
	self->page_size   = page_size - sizeof(Page);
	self->limit       = limit;
	self->limit_wm    = limit_wm;
	if (str_compare_raw(limit_behaviour, "block", 5))
		self->limit_error = false;
	else
	if (str_compare_raw(limit_behaviour, "error", 5))
		self->limit_error = true;
	else
		error("mm: unrecognized memory limit behaviour");
}

static inline void
memory_mgr_reset(MemoryMgr* self)
{
	spinlock_lock(&self->lock);
	auto page = self->free_list;
	self->count          -= self->free_list_count;
	self->free_list       = NULL;
	self->free_list_count = 0;
	spinlock_unlock(&self->lock);
	while (page)
	{
		auto next = page->next;
		vfs_munmap(page, sizeof(Page) + self->page_size);
		page = next;
	}
}

static inline void
memory_mgr_free(MemoryMgr* self)
{
	auto page = self->free_list;
	while (page)
	{
		auto next = page->next;
		vfs_munmap(page, sizeof(Page) + self->page_size);
		page = next;
	}
	self->free_list       = NULL;
	self->free_list_count = 0;

	spinlock_free(&self->lock);
}

static inline Page*
memory_mgr_pop(MemoryMgr* self)
{
	spinlock_lock(&self->lock);
	if (likely(self->free_list))
	{
		auto page = self->free_list;
		self->free_list_count--;
		self->free_list = page->next;
		spinlock_unlock(&self->lock);

		page->used = 0;
		page->next = NULL;
		return page;
	}
	self->count++;
	spinlock_unlock(&self->lock);

	Page* page;
	page = vfs_mmap(-1, sizeof(Page) + self->page_size);
	if (unlikely(page == NULL))
		error_system();
	page->used = 0;
	page->next = NULL;
	return page;
}

static inline void
memory_mgr_push(MemoryMgr* self, Page* page, Page* page_tail, int count)
{
	spinlock_lock(&self->lock);
	page_tail->next = self->free_list;
	self->free_list = page;
	self->free_list_count += count;
	spinlock_unlock(&self->lock);
}

static inline void
memory_mgr_show(MemoryMgr* self, Buf* buf)
{
	spinlock_lock(&self->lock);
	int pages      = self->count;
	int pages_used = self->count - self->free_list_count;
	int pages_free = self->free_list_count;
	int page_size  = self->page_size + sizeof(Page);
	spinlock_unlock(&self->lock);

	// map
	encode_map(buf, 4);

	// pages
	encode_raw(buf, "pages", 5);
	encode_integer(buf, pages);

	// pages_used
	encode_raw(buf, "pages_used", 10);
	encode_integer(buf, pages_used);

	// pages_free
	encode_raw(buf, "pages_free", 10);
	encode_integer(buf, pages_free);

	// page_size
	encode_raw(buf, "page_size", 9);
	encode_integer(buf, page_size);
}
