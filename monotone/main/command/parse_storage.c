
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

static int
parse_storage_options(Lex* self, Source* config, char* command)
{
	// [(]
	int mask = 0;
	if (! lex_if(self, '(', NULL))
		return mask;

	// [)]
	if (lex_if(self, ')', NULL))
		return mask;

	for (;;)
	{
		// name
		Token name;
		lex_next(self, &name);
		switch (name.id) {
		case KCLOUD:
			name.id = KNAME;
			break;
		case KNAME:
			break;
		default:
			error("%s ('name' expected", command);
			break;
		}

		// value
		if (str_compare_raw(&name.string, "uuid", 4))
		{
			// uuid <string>
			parse_uuid(self, &name, &config->uuid);
			mask |= SOURCE_UUID;
		} else
		if (str_compare_raw(&name.string, "path", 4))
		{
			// path <string>
			parse_string(self, &name, &config->path);
			mask |= SOURCE_PATH;
		} else
		if (str_compare_raw(&name.string, "cloud", 5))
		{
			// cloud <string>
			parse_string(self, &name, &config->cloud);
			mask |= SOURCE_CLOUD;
		} else
		if (str_compare_raw(&name.string, "cloud_drop_local", 16))
		{
			// cloud_drop_local <bool>
			parse_bool(self, &name, &config->cloud_drop_local);
			mask |= SOURCE_CLOUD_DROP_LOCAL;
		} else
		if (str_compare_raw(&name.string, "sync", 4))
		{
			// sync <bool>
			parse_bool(self, &name, &config->sync);
			mask |= SOURCE_SYNC;
		} else
		if (str_compare_raw(&name.string, "crc", 3))
		{
			// crc <bool>
			parse_bool(self, &name, &config->crc);
			mask |= SOURCE_CRC;
		} else
		if (str_compare_raw(&name.string, "refresh_wm", 10))
		{
			// refresh_wm <int>
			parse_int(self, &name, &config->refresh_wm);
			mask |= SOURCE_REFRESH_WM;
		} else
		if (str_compare_raw(&name.string, "region_size", 11))
		{
			// region_size <int>
			parse_int(self, &name, &config->region_size);
			mask |= SOURCE_REGION_SIZE;
			if (config->region_size == 0)
				error("invalid region_size value");
		} else
		if (str_compare_raw(&name.string, "compression", 11))
		{
			// compression <string>
			parse_string(self, &name, &config->compression);
			mask |= SOURCE_COMPRESSION;

			// validate compression name
			int compression_id = compression_mgr_of(&config->compression);
			if (compression_id == -1)
				error("unknown compression: '%.*s'", str_size(&config->compression),
				      str_of(&config->compression));
		} else
		if (str_compare_raw(&name.string, "compression_level", 17))
		{
			// compression_level <int>
			parse_int(self, &name, &config->compression_level);
			mask |= SOURCE_COMPRESSION_LEVEL;

		} else
		if (str_compare_raw(&name.string, "encryption", 10))
		{
			// encryption <string>
			parse_string(self, &name, &config->encryption);
			mask |= SOURCE_ENCRYPTION;

			// validate encryption name
			int id = encryption_mgr_of(&config->encryption);
			if (id == -1)
				error("unknown encryption: '%.*s'", str_size(&config->encryption),
				      str_of(&config->encryption));

		} else
		if (str_compare_raw(&name.string, "encryption_key", 14))
		{
			// encryption_key <string>
			parse_string(self, &name, &config->encryption_key);
			mask |= SOURCE_ENCRYPTION_KEY;

			if (! str_empty(&config->encryption_key))
				if (str_size(&config->encryption_key) != 32)
					error("encryption key must be 256bit");

		} else
		{
			error("%s: unknown option: '%.*s'", command, str_size(&name.string),
			      str_of(&name.string));
		}

		// ,
		if (lex_if(self, ',', NULL))
			continue;

		// )
		if (! lex_if(self, ')', NULL))
			error("%s name (...')' expected", command);

		break;
	}

	return mask;
}

Cmd*
parse_storage_create(Lex* self)
{
	// CREATE STORAGE [IF NOT EXISTS] name (options)
	auto cmd = cmd_storage_create_allocate();
	guard(cmd_free, &cmd->cmd);

	// if not exists
	cmd->if_not_exists = parse_if_not_exists(self);

	// name
	Token name;
	if (! lex_if(self, KNAME, &name))
		error("CREATE STORAGE 'name' expected");

	// create storage config
	auto config = source_allocate();
	cmd->config = config;
	source_set_name(config, &name.string);

	/// generate uuid
	Uuid uuid;
	uuid_generate(&uuid, global()->random);
	source_set_uuid(config, &uuid);

	// set default compression
	auto default_compression = &config()->compression.string;
	if (! str_empty(default_compression))
	{
		source_set_compression(config, &config()->compression.string);
		auto level = var_int_of(&config()->compression_level);
		source_set_compression_level(config, level);
	}

	// [(options)]
	parse_storage_options(self, config, "CREATE STORAGE");

	// validate encryption settings
	int id = encryption_mgr_of(&config->encryption);
	if (id != ENCRYPTION_NONE)
	{
		// generate key
		if (str_empty(&config->encryption_key))
		{
			uint8_t data[32];
			random_generate_alnum(global()->random, data, sizeof(data));
			str_strndup(&config->encryption_key, data, sizeof(data));
		}
	}

	unguard();
	return &cmd->cmd;
}

Cmd*
parse_storage_drop(Lex* self)
{
	// DROP STORAGE [IF EXISTS] name
	auto cmd = cmd_storage_drop_allocate();
	guard(cmd_free, &cmd->cmd);

	// if exists
	cmd->if_exists = parse_if_exists(self);

	// name
	if (! lex_if(self, KNAME, &cmd->name))
		error("DROP STORAGE 'name' expected");

	unguard();
	return &cmd->cmd;
}

static void
parse_storage_alter_rename(Lex* self, Cmd* arg)
{
	// ALTER STORAGE [IF EXISTS] name RENAME TO name
	auto cmd = cmd_storage_alter_of(arg);

	// [TO]
	lex_if(self, KTO, NULL);

	// name
	if (! lex_if(self, KNAME, &cmd->name_new))
		error("ALTER STORAGE RENAME 'name' expected");
}

static void
parse_storage_alter_set(Lex* self, Cmd* arg)
{
	// ALTER STORAGE [IF EXISTS] name SET (options)
	auto cmd = cmd_storage_alter_of(arg);

	// create storage config
	cmd->config = source_allocate();
	source_set_name(cmd->config, &cmd->name.string);

	// (options)
	cmd->config_mask =
		parse_storage_options(self, cmd->config, "ALTER STORAGE");

	if (cmd->config_mask & SOURCE_UUID)
		error("storage uuid cannot be changed this way");
}

Cmd*
parse_storage_alter(Lex* self)
{
	// ALTER STORAGE [IF EXISTS] name RENAME TO name
	// ALTER STORAGE [IF EXISTS] name SET (options)
	auto cmd = cmd_storage_alter_allocate();
	guard(cmd_free, &cmd->cmd);

	// if exists
	cmd->if_exists = parse_if_exists(self);

	// name
	if (! lex_if(self, KNAME, &cmd->name))
		error("ALTER STORAGE 'name' expected");

	// RENAME | SET
	if (lex_if(self, KRENAME, NULL))
		parse_storage_alter_rename(self, &cmd->cmd);
	else
	if (lex_if(self, KSET, NULL))
		parse_storage_alter_set(self, &cmd->cmd);
	else
		error("ALTER STORAGE name 'RENAME or SET' expected");

	unguard();
	return &cmd->cmd;
}
