
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

#include <monotone_runtime.h>
#include <monotone_lib.h>
#include <monotone_config.h>
#include <monotone_cloud.h>
#include <monotone_io.h>
#include <monotone_storage.h>
#include <monotone_wal.h>
#include <monotone_engine.h>
#include <monotone_command.h>
#include <monotone_main.h>

static void
main_control_save_config(void* arg)
{
	Main* self = arg;
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/config.json",
	         config_directory());
	config_save(&self->config, path);
}

static void
main_control_lock(void* arg, bool shared)
{
	Main* self = arg;
	rwlock_lock(&self->lock, shared);
}

static void
main_control_unlock(void* arg)
{
	Main* self = arg;
	rwlock_unlock(&self->lock);
}

void
main_init(Main* self)
{
	// control
	auto control = &self->control;
	control->save_config = main_control_save_config;
	control->lock        = main_control_lock;
	control->unlock      = main_control_unlock;
	control->arg         = self;

	// global
	self->global.config          = &self->config;
	self->global.control         = &self->control;
	self->global.compression_mgr = &self->compression_mgr;
	self->global.encryption_mgr  = &self->encryption_mgr;
	self->global.random          = &self->random;
	self->global.memory_mgr      = &self->memory_mgr;
	if (crc32_sse_supported())
		self->global.crc = crc32_sse;
	else
		self->global.crc = crc32;

	// runtime
	self->context.log     = (LogFunction)logger_write;
	self->context.log_arg = &self->logger;
	self->context.global  = &self->global;

	comparator_init(&self->comparator);
	logger_init(&self->logger);
	random_init(&self->random);
	memory_mgr_init(&self->memory_mgr);
	compression_mgr_init(&self->compression_mgr);
	encryption_mgr_init(&self->encryption_mgr);
	config_init(&self->config);
	file_init(&self->lock_directory);

	rwlock_init(&self->lock);
	service_init(&self->service);
	cron_init(&self->cron, &self->service);

	cloud_mgr_init(&self->cloud_mgr);

	wal_init(&self->wal);
	engine_init(&self->engine, &self->comparator,
	            &self->wal,
	            &self->service,
	            &self->cloud_mgr);
	worker_mgr_init(&self->worker_mgr);
}

void
main_free(Main* self)
{
	engine_free(&self->engine);
	wal_free(&self->wal);
	service_free(&self->service);
	cron_free(&self->cron);
	cloud_mgr_free(&self->cloud_mgr);
	config_free(&self->config);
	compression_mgr_free(&self->compression_mgr);
	encryption_mgr_free(&self->encryption_mgr);
	memory_mgr_free(&self->memory_mgr);
	rwlock_free(&self->lock);
	file_close(&self->lock_directory);
}

void
main_prepare(Main* self)
{
	// set default configuration
	config_prepare(&self->config);
}

static void
main_deploy(Main* self, Str* directory)
{
	if (config_online())
		log("environment is already open");

	// prepare random manager
	random_open(&self->random);

	// generate uuid, unless it is set
	if (! var_string_is_set(&config()->uuid))
	{
		Uuid uuid;
		uuid_generate(&uuid, global()->random);
		char uuid_sz[UUID_SZ];
		uuid_to_string(&uuid, uuid_sz, sizeof(uuid_sz));
		var_string_set_raw(&config()->uuid, uuid_sz, sizeof(uuid_sz) - 1);
	}

	// set directory
	var_string_set(&config()->directory, directory);

	// create directory if not exists
	auto bootstrap = !fs_exists("%s", str_of(directory));
	if (bootstrap)
		fs_mkdir(0755, "%s", str_of(directory));

	// get exclusive directory lock
	file_open_directory(&self->lock_directory, str_of(directory));
	if (! file_lock(&self->lock_directory))
		error("in use by another process");

	// read or create config file
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/config.json", config_directory());
	config_open(&self->config, path);

	// configure logger
	auto logger = &self->logger;
	logger_set_enable(logger, var_int_of(&config()->log));
	logger_set_to_stdout(logger, var_int_of(&config()->log_to_stdout));
	if (var_int_of(&config()->log_to_file))
	{
		snprintf(path, sizeof(path), "%s/log", config_directory());
		logger_open(logger, path);
	}

	// configure memory manager
	memory_mgr_set(&self->memory_mgr,
	               var_int_of(&config()->mm_page_size),
	               var_int_of(&config()->mm_cache),
	               var_int_of(&config()->mm_limit),
	               &config()->mm_limit_behaviour.string,
	               var_int_of(&config()->mm_limit_wm));
}

static void
main_recover(WalCursor* cursor)
{
	Buf msg;
	buf_init(&msg);
	guard(buf_free, &msg);
	buf_printf(&msg, "wal: file '%s' has invalid record at offset %" PRIu64
	           " (last valid lsn is %" PRIu64 ")",
	           str_of(&cursor->file->file.path), cursor->file_offset,
	           config_lsn());

	if (! var_int_of(&config()->wal_recover))
		error("%.*s", buf_size(&msg), buf_cstr(&msg));

	// recover wal up to the last valid position
	log("%.*s", buf_size(&msg), buf_cstr(&msg));
	log("wal: begin recover");

	auto id = cursor->file->id;
	wal_file_truncate(cursor->file, cursor->file_offset);

	// remove all wal files after it
	for (;;)
	{
		id = wal_id_next(&cursor->wal->list, id);
		if (id == UINT64_MAX)
			break;
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/wal/%020" PRIu64 ".wal",
		         config_directory(),
		         id);
		fs_unlink("%s", path);
		log("wal/%" PRIu64 " (file removed)", id);
	}

	log("wal: done");
}

static bool
main_replay(Main* self)
{
	WalCursor cursor;
	wal_cursor_init(&cursor);
	guard(wal_cursor_close, &cursor);

	auto status = wal_cursor_open(&cursor, &self->wal, 0);
	if (status == WAL_CORRUPTED)
		goto corrupted;

	uint64_t total = 0;
	for (;;)
	{
		auto status = wal_cursor_next(&cursor);
		if (status == WAL_EOF)
			break;
		if (status == WAL_CORRUPTED)
			goto corrupted;

		// replay operation
		auto write = wal_cursor_at(&cursor);
		switch (write->type) {
		case LOG_WRITE:
			engine_write_replay(&self->engine, write);
			break;
		case LOG_DROP:
		{
			auto drop = (LogDrop*)write;
			engine_drop(&self->engine, drop->id, false, true, ID|ID_CLOUD);
			break;
		}
		default:
			error("wal: unrecognized operation: %d (lsn: %" PRIu64 ")",
			      write->type, write->lsn);
			break;
		}

		total += write->count;
	}
	log("wal: %.1f million records processed",
	    total / 1000000.0);
	return false;

corrupted:
	main_recover(&cursor);
	return true;
}

void
main_start(Main* self, const char* directory)
{
	// create base directory and setup logger
	Str path;
	str_init(&path);
	str_set_cstr(&path, directory);
	main_deploy(self, &path);

	// welcome
	log("");
	log("monotone.");
	log("");
	config_print(config());
	log("");

	// unset comparator for serial mode
	if (config_serial())
		comparator_init(&self->comparator);

	// recover clouds
	cloud_mgr_open(&self->cloud_mgr);

	// recover storages
	engine_open(&self->engine);

	// open wal and replay
	if (var_int_of(&config()->wal))
	{
		auto wal = &self->wal;
		wal_open(wal);
		if (main_replay(self))
		{
			// reopen wal manager after recover
			wal_free(wal);
			wal_init(wal);
			wal_open(wal);
		}
	}

	// restore serial number
	if (config_serial())
		engine_set_serial(&self->engine);

	// reschedule background operations after restart
	engine_resume(&self->engine);

	// start compaction workers
	worker_mgr_start(&self->worker_mgr, &self->engine);

	// start interval worker
	cron_start(&self->cron);

	// done
	var_int_set(&config()->online, true);
}

void
main_stop(Main* self)
{
	// shutdown interval worker
	cron_stop(&self->cron);

	// shutdown workers
	service_shutdown(&self->service);

	worker_mgr_stop(&self->worker_mgr);

	// close partitions
	engine_close(&self->engine);

	logger_close(&self->logger);
}
