#pragma once

//
// monotone.
//
// embeddable cloud-native storage for events
// and time-series data.
//
// Copyright (c) 2023-2025 Dmitry Simonenko
//
// MIT Licensed.
//

typedef struct Slice   Slice;
typedef struct Mapping Mapping;

struct Slice
{
	uint64_t   min;
	uint64_t   max;
	RbtreeNode node;
};

struct Mapping
{
	Rbtree tree;
	int    tree_count;
};

static inline void
slice_init(Slice* self, uint64_t min, uint64_t max)
{
	self->min = min;
	self->max = max;
	rbtree_init_node(&self->node);
}

always_inline static inline bool
slice_in(Slice* self, uint64_t min)
{
	return self->min <= min && min <= self->max;
}

static inline void
mapping_init(Mapping* self)
{
	self->tree_count = 0;
	rbtree_init(&self->tree);
}

always_inline static inline Slice*
mapping_of(RbtreeNode* node)
{
	return container_of(node, Slice, node);
}

static inline Slice*
mapping_min(Mapping* self)
{
	if (self->tree_count == 0)
		return NULL;
	auto min = rbtree_min(&self->tree);
	return mapping_of(min);
}

static inline Slice*
mapping_max(Mapping* self)
{
	if (self->tree_count == 0)
		return NULL;
	auto max = rbtree_max(&self->tree);
	return mapping_of(max);
}

hot static inline
rbtree_get(mapping_find, compare_u64(mapping_of(n)->min, *(uint64_t*)key))

static inline void
mapping_add(Mapping* self, Slice* slice)
{
	RbtreeNode* node;
	int rc;
	rc = mapping_find(&self->tree, NULL, &slice->min, &node);
	if (rc == 0 && node)
		assert(0);
	rbtree_set(&self->tree, node, rc, &slice->node);
	self->tree_count++;
}

static inline void
mapping_remove(Mapping* self, Slice* slice)
{
	rbtree_remove(&self->tree, &slice->node);
	self->tree_count--;
	assert(self->tree_count >= 0);
}

static inline void
mapping_replace(Mapping* self, Slice* a, Slice* b)
{
	rbtree_replace(&self->tree, &a->node, &b->node);
}

static inline Slice*
mapping_next(Mapping* self, Slice* slice)
{
	auto node = rbtree_next(&self->tree, &slice->node);
	if (! node)
		return NULL;
	return mapping_of(node);
}

static inline Slice*
mapping_prev(Mapping* self, Slice* slice)
{
	auto node = rbtree_prev(&self->tree, &slice->node);
	if (! node)
		return NULL;
	return mapping_of(node);
}

hot static inline Slice*
mapping_seek(Mapping* self, uint64_t id)
{
	// slice[n].min >= id && key < slice[n + 1].id
	RbtreeNode* node = NULL;
	int rc = mapping_find(&self->tree, NULL, &id, &node);
	assert(node != NULL);
	if (rc > 0) {
		auto prev = rbtree_prev(&self->tree, node);
		if (prev)
			node = prev;
	}
	return mapping_of(node);
}

hot static inline Slice*
mapping_gte(Mapping* self, uint64_t id)
{
	if (unlikely(self->tree_count == 0))
		return NULL;
	auto slice = mapping_seek(self, id);
	if (slice->max < id)
		return mapping_next(self, slice);
	return slice;
}

hot static inline Slice*
mapping_match(Mapping* self, uint64_t id)
{
	// exact match
	if (unlikely(self->tree_count == 0))
		return NULL;
	auto slice = mapping_seek(self, id);
	if (slice_in(slice, id))
		return slice;
	return NULL;
}
