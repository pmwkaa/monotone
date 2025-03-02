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

typedef struct Global Global;

struct Global
{
	Config*         config;
	Control*        control;
	CompressionMgr* compression_mgr;
	EncryptionMgr*  encryption_mgr;
	CrcFunction     crc;
	Random*         random;
	MemoryMgr*      memory_mgr;
};

#define global() ((Global*)mn_runtime.context->global)
#define config()  global()->config

static inline const char*
config_directory(void)
{
	return var_string_of(&config()->directory);
}

static inline bool
config_online(void)
{
	return var_int_of(&config()->online);
}

static inline bool
config_sync(void)
{
	return var_int_of(&config()->sync);
}

static inline bool
config_serial(void)
{
	return var_int_of(&config()->serial);
}

static inline uint64_t
config_interval(void)
{
	return var_int_of(&config()->interval);
}

static inline uint64_t
config_interval_of(uint64_t time)
{
	return time - (time % config_interval());
}

static inline int
config_workers(void)
{
	return var_int_of(&config()->workers);
}

static inline int
config_workers_upload(void)
{
	return var_int_of(&config()->workers_upload);
}

// ssn
static inline uint64_t
config_ssn(void)
{
	return var_int_of(&config()->ssn);
}

static inline void
config_ssn_set(uint64_t value)
{
	var_int_set(&config()->ssn, value);
}

static inline uint64_t
config_ssn_next(void)
{
	return var_int_set_inc(&config()->ssn);
}

static inline void
config_ssn_follow(uint64_t value)
{
	var_int_follow(&config()->ssn, value);
}

// lsn
static inline uint64_t
config_lsn(void)
{
	return var_int_of(&config()->lsn);
}

static inline void
config_lsn_set(uint64_t value)
{
	var_int_set(&config()->lsn, value);
}

static inline uint64_t
config_lsn_next(void)
{
	return var_int_set_next(&config()->lsn);
}

static inline void
config_lsn_follow(uint64_t value)
{
	var_int_follow(&config()->lsn, value);
}

// control
static inline void
control_save_config(void)
{
	global()->control->save_config(global()->control->arg);
}

static inline void
control_lock_shared(void)
{
	global()->control->lock(global()->control->arg, true);
}

static inline void
control_lock_exclusive(void)
{
	global()->control->lock(global()->control->arg, false);
}

static inline void
control_unlock(void)
{
	global()->control->unlock(global()->control->arg);
}

static inline void
control_unlock_guard(void* _unused)
{
	unused(_unused);
	control_unlock();
}
