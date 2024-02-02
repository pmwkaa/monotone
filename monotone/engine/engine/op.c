
//
// monotone
//
// time-series storage
//

#include <monotone_runtime.h>
#include <monotone_lib.h>
#include <monotone_config.h>
#include <monotone_io.h>
#include <monotone_storage.h>
#include <monotone_engine.h>
#include <malloc.h>

void
engine_refresh(Engine* self, Refresh* refresh, uint64_t min,
               bool if_exists)
{
	unused(self);
	refresh_reset(refresh);
	refresh_run(refresh, min, NULL, if_exists);
}

void
engine_refresh_range(Engine* self, Refresh* refresh, uint64_t min, uint64_t max)
{
	for (;;)
	{
		// get next ref >= min
		auto ref = engine_lock(self, min, LOCK_ACCESS, true, false);
		if (ref == NULL)
			return;
		uint64_t ref_min = ref->slice.min;
		uint64_t ref_max = ref->slice.max;
		engine_unlock(self, ref, LOCK_ACCESS);

		if (ref_min >= max)
			return;

		engine_refresh(self, refresh, ref_min, true);
		min = ref_max;
	}
}

void
engine_move(Engine* self, Refresh* refresh, uint64_t min, Str* storage,
            bool if_exists)
{
	unused(self);
	refresh_reset(refresh);
	refresh_run(refresh, min, storage, if_exists);
}

void
engine_drop(Engine* self, uint64_t min, bool if_exists)
{
	// take exclusive control lock
	control_lock_exclusive();

	// find partition = min
	auto slice = mapping_match(&self->mapping, min);
	if (! slice)
	{
		control_unlock();
		if (! if_exists)
			error("drop: partition <%" PRIu64 "> does not exists", min);
		return;
	}

	// remove partition from mapping
	mapping_remove(&self->mapping, slice);

	// remove partition from storage
	auto ref = ref_of(slice);
	auto storage = storage_mgr_find(&self->storage_mgr, &ref->part->source->name);
	storage_remove(storage, ref->part);

	control_unlock();

	// delete partition file and free
	part_delete(ref->part, true);
	ref_free(ref);
}

void
engine_move_range(Engine* self, Refresh* refresh, uint64_t min, uint64_t max,
                  Str* storage)
{
	for (;;)
	{
		// get next ref >= min
		auto ref = engine_lock(self, min, LOCK_ACCESS, true, false);
		if (ref == NULL)
			return;
		uint64_t ref_min = ref->slice.min;
		uint64_t ref_max = ref->slice.max;
		engine_unlock(self, ref, LOCK_ACCESS);

		if (ref_min >= max)
			return;

		engine_move(self, refresh, ref_min, storage, true);
		min = ref_max;
	}
}

void
engine_drop_range(Engine* self, uint64_t min, uint64_t max)
{
	for (;;)
	{
		// get next ref >= min
		auto ref = engine_lock(self, min, LOCK_ACCESS, true, false);
		if (ref == NULL)
			return;
		uint64_t ref_min = ref->slice.min;
		uint64_t ref_max = ref->slice.max;
		engine_unlock(self, ref, LOCK_ACCESS);

		if (ref_min >= max)
			return;

		engine_drop(self, ref_min, true);
		min = ref_max;
	}
}

static inline Part*
engine_rebalance_tier(Engine* self, Tier* tier, Str* storage)
{
	if (tier->storage->list_count <= tier->config->capacity)
		return NULL;

	// get oldest partition (by psn)
	auto oldest = storage_oldest(tier->storage);

	// schedule partition drop
	if (list_is_last(&self->conveyor.list, &tier->link))
		return oldest;

	// schedule partition move to the next tier storage
	auto next = container_of(tier->link.next, Tier, link);
	str_copy(storage, &next->storage->source->name);
	return oldest;
}

static bool
engine_rebalance_next(Engine* self, uint64_t* min, Str* storage)
{
	// take control shared lock
	control_lock_shared();
	guard(lock_guard, control_unlock_guard, NULL);

	if (conveyor_empty(&self->conveyor))
		return false;

	mutex_lock(&self->lock);

	list_foreach(&self->conveyor.list)
	{
		auto tier = list_at(Tier, link);
		auto part = engine_rebalance_tier(self, tier, storage);
		if (part)
		{
			// mark partition for refresh to avoid concurrent rebalance
			// calls for the same partition
			if (part->refresh)
				continue;
			part->refresh = true;

			*min = part->id.min;
			mutex_unlock(&self->lock);
			return true;
		}
	}

	mutex_unlock(&self->lock);
	return false;
}

void
engine_rebalance(Engine* self, Refresh* refresh)
{
	for (;;)
	{
		Str storage;
		str_init(&storage);
		guard(guard, str_free, &storage);
		uint64_t min;
		if (! engine_rebalance_next(self, &min, &storage))
			break;
		if (str_empty(&storage))
			engine_drop(self, min, true);
		else
			engine_move(self, refresh, min, &storage, true);
	}
}

void
engine_checkpoint(Engine* self)
{
	// take exclusive control lock
	control_lock_exclusive();
	guard(guard, control_unlock_guard, NULL);

	// schedule refresh
	auto slice = mapping_min(&self->mapping);
	while (slice)
	{
		auto ref = ref_of(slice);
		if (!ref->part->refresh && ref->part->memtable->size > 0)
		{
			service_refresh(self->service, slice->min);
			ref->part->refresh = true;
		}
		slice = mapping_next(&self->mapping, slice);
	}

	malloc_trim(0);
}

void
engine_download(Engine* self, uint64_t min, bool if_exists)
{
	// find the original partition
	auto ref = engine_lock(self, min, LOCK_SERVICE, false, false);
	if (unlikely(! ref))
	{
		if (! if_exists)
			error("download: partition <%" PRIu64 "> not found", min);
		return;
	}
	auto part = ref->part;

	// get the original partition storage
	auto storage = storage_mgr_find(&self->storage_mgr, &part->source->name);
	assert(storage);

	if (! storage->cloud)
	{
		engine_unlock(self, ref, LOCK_SERVICE);
		error("download: partition <%" PRIu64 "> storage has no associated cloud", min);
	}

	// partition file exists locally
	if (part->in_storage)
	{
		engine_unlock(self, ref, LOCK_SERVICE);
		return;
	}

	// download and open partition file
	Exception e;
	if (try(&e)) {
		part_download(part, storage->cloud);
	}
	if (catch(&e))
	{
		engine_unlock(self, ref, LOCK_SERVICE);
		rethrow();
	}

	// update partition state
	mutex_lock(&self->lock);
	ref_lock(ref, LOCK_ACCESS);

	part->in_storage = true;

	ref_unlock(ref, LOCK_ACCESS);
	mutex_unlock(&self->lock);

	// complete
	engine_unlock(self, ref, LOCK_SERVICE);
}

void
engine_upload(Engine* self, uint64_t min, bool if_exists)
{
	// find the original partition
	auto ref = engine_lock(self, min, LOCK_SERVICE, false, false);
	if (unlikely(! ref))
	{
		if (! if_exists)
			error("upload: partition <%" PRIu64 "> not found", min);
		return;
	}
	auto part = ref->part;

	// get the original partition storage
	auto storage = storage_mgr_find(&self->storage_mgr, &part->source->name);
	assert(storage);

	if (! storage->cloud)
	{
		engine_unlock(self, ref, LOCK_SERVICE);
		error("upload: partition <%" PRIu64 "> storage has no associated cloud", min);
	}

	// partition already on cloud
	if (part->in_cloud)
	{
		engine_unlock(self, ref, LOCK_SERVICE);
		return;
	}

	// download and open partition file
	Exception e;
	if (try(&e)) {
		part_upload(part, storage->cloud);
	}
	if (catch(&e))
	{
		engine_unlock(self, ref, LOCK_SERVICE);
		rethrow();
	}

	// update partition state
	mutex_lock(&self->lock);
	ref_lock(ref, LOCK_ACCESS);

	part->in_cloud = true;

	ref_unlock(ref, LOCK_ACCESS);
	mutex_unlock(&self->lock);

	// complete
	engine_unlock(self, ref, LOCK_SERVICE);
}

void
engine_offload(Engine* self, uint64_t min, bool from_storage, bool if_exists)
{
	// find the original partition
	auto ref = engine_lock(self, min, LOCK_SERVICE, false, false);
	if (unlikely(! ref))
	{
		if (! if_exists)
			error("offload: partition <%" PRIu64 "> not found", min);
		return;
	}
	auto part = ref->part;

	// get the original partition storage
	auto storage = storage_mgr_find(&self->storage_mgr, &part->source->name);
	assert(storage);

	if (! storage->cloud)
	{
		engine_unlock(self, ref, LOCK_SERVICE);
		error("offload: partition <%" PRIu64 "> storage has no associated cloud", min);
	}

	// remove from storage or cloud
	if (from_storage)
	{
		// already removed
		if (! part->in_storage)
		{
			engine_unlock(self, ref, LOCK_SERVICE);
			return;
		}

		// partition not in cloud
		if (! part->in_cloud)
		{
			engine_unlock(self, ref, LOCK_SERVICE);
			error("offload: partition <%" PRIu64 "> must uploaded first", min);
		}

	} else
	{
		// already removed
		if (! part->in_cloud)
		{
			engine_unlock(self, ref, LOCK_SERVICE);
			return;
		}

		// partition not in storage
		if (! part->in_storage)
		{
			engine_unlock(self, ref, LOCK_SERVICE);
			error("offload: partition <%" PRIu64 "> must downloaded first", min);
		}
	}

	// execute removal
	Exception e;
	if (try(&e)) {
		part_offload(part, storage->cloud, from_storage);
	}
	if (catch(&e))
	{
		engine_unlock(self, ref, LOCK_SERVICE);
		rethrow();
	}

	// update partition state
	mutex_lock(&self->lock);
	ref_lock(ref, LOCK_ACCESS);

	if (from_storage)
		part->in_storage = false;
	else
		part->in_cloud = false;

	ref_unlock(ref, LOCK_ACCESS);
	mutex_unlock(&self->lock);

	// complete
	engine_unlock(self, ref, LOCK_SERVICE);
}
