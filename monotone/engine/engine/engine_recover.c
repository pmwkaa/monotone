
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

static inline int
engine_recover_id(const char* path, uint64_t* id)
{
	// <min>
	// <min>.incomplete
	// <min>.cloud
	// <min>.cloud.incomplete
	*id = 0;
	while (*path && *path != '.')
	{
		if (unlikely(! isdigit(*path)))
			return -1;
		*id = (*id * 10) + *path - '0';
		path++;
	}

	int state = -1;
	if (! *path)
		state = PART_FILE;
	else
	if (! strcmp(path, ".incomplete"))
		state = PART_FILE_INCOMPLETE;
	else
	if (! strcmp(path, ".cloud"))
		state = PART_FILE_CLOUD;
	else
	if (! strcmp(path, ".cloud.incomplete"))
		state = PART_FILE_CLOUD_INCOMPLETE;
	return state;
}

static void
engine_recover_storage(Engine* self, Storage* storage)
{
	auto source = storage->source;

	// read storage directory
	char path[PATH_MAX];
	source_pathfmt(source, path, sizeof(path), "");

	DIR* dir = opendir(path);
	if (unlikely(dir == NULL))
		error("storage: directory '%s' open error", path);
	guard(dir_guard, closedir, dir);
	for (;;)
	{
		auto entry = readdir(dir);
		if (entry == NULL)
			break;
		if (entry->d_name[0] == '.')
			continue;

		// <min>
		// <min>.incomplete
		// <min>.cloud
		// <min>.cloud.incomplete
		uint64_t min;
		auto state = engine_recover_id(entry->d_name, &min);
		if (state == -1)
		{
			log("storage: skipping unknown file: '%s%s'",
			    path, entry->d_name);
			continue;
		}

		// find or create partition object
		auto part = storage_find(storage, min);
		if (part == NULL)
		{
			Id id =
			{
				.min = min,
				.max = min + config_interval(),
				.psn = 0
			};
			part = part_allocate(self->comparator, storage->source, &id);
			storage_add(storage, part);
		}
		part_set(part, state);
	}
}

static void
engine_recover_validate(Engine* self, Storage* storage)
{
	list_foreach_safe(&storage->list)
	{
		auto part = list_at(Part, link);

		// remove incomplete cloud file
		if (part_has(part, PART_FILE_CLOUD_INCOMPLETE))
		{
			// crash during upload
			part_file_cloud_delete(part, false);
			part_unset(part, PART_FILE_CLOUD_INCOMPLETE);
		}

		switch (part->state) {
		case PART_FILE:
		case PART_FILE | PART_FILE_CLOUD:
		case PART_FILE_CLOUD:
			// normal state
			break;

		// crash recovery cases
		case PART_FILE | PART_FILE_INCOMPLETE:
			// crash during refresh on the same storage
			part_file_delete(part, false);
			part_unset(part, PART_FILE_INCOMPLETE);
			break;

		case PART_FILE_INCOMPLETE:
		{
			// crash during refresh on the same storage or move

			// ensure partition does not exists on other storage
			auto ref = storage_mgr_find_part(&self->storage_mgr, storage, part->id.min);
			if (ref)
			{
				if (! part_has(ref, PART_FILE))
					error("partition <%" PRIu64"> has unexpected state: %d",
					      ref->id.min, ref->state);

				// parent still exists, remove incomplete partition
				storage_remove(storage, part);
				part_file_delete(part, false);
				part_free(part);
				continue;
			}

			// rename
			part_file_complete(part);
			part_unset(part, PART_FILE_INCOMPLETE);
			break;
		}

		case PART_FILE_INCOMPLETE | PART_FILE_CLOUD:
			// crash during download
			part_file_delete(part, false);
			part_unset(part, PART_FILE_INCOMPLETE);
			break;

		default:
			error("partition <%" PRIu64"> has unexpected state: %d",
			      part->id.min, part->state);
			break;
		}
	}
}

void
engine_recover(Engine* self)
{
	auto storage_mgr = &self->storage_mgr;

	// read partitions
	list_foreach(&storage_mgr->list)
	{
		auto storage = list_at(Storage, link);
		engine_recover_storage(self, storage);
	}

	// validate partitions per storage
	list_foreach(&storage_mgr->list)
	{
		auto storage = list_at(Storage, link);
		engine_recover_validate(self, storage);
	}

	// open partitions
	list_foreach(&storage_mgr->list)
	{
		auto storage = list_at(Storage, link);
		list_foreach(&storage->list)
		{
			auto part = list_at(Part, link);

			// open partition or cloud file, read index
			if (part_has(part, PART_FILE))
				part_file_open(part, true);
			else
			if (part_has(part, PART_FILE_CLOUD))
				part_file_cloud_open(part);
			else
				abort();

			// sync psn
			part->id.psn = part->index->id.psn;
			config_psn_follow(part->id.psn);

			// register partition reference
			auto ref = ref_allocate(part->id.min, part->id.max);
			ref_prepare(ref, &self->lock, &self->cond_var, part);
			mapping_add(&self->mapping, &ref->slice);
		}
	}
}
