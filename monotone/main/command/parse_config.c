
//
// monotone
//
// time-series storage
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

Cmd*
parse_show(Lex* self)
{
	// SHOW op [, ...]
	auto cmd = cmd_show_allocate();
	guard(guard, cmd_free, &cmd->cmd);

	for (;;)
	{
		// SHOW name
		int type;
		Token tk;
		lex_next(self, &tk);
		switch (tk.id) {
		case KMEMORY:
			type = SHOW_MEMORY;
			break;
		case KWAL:
			type = SHOW_WAL;
			break;
		case KCLOUD:
			// show cloud storage
			if (! lex_if(self, KNAME, &tk))
				error("SHOW CLOUD <name> expected");
			type = SHOW_CLOUDS;
			break;
		case KCLOUDS:
			type = SHOW_CLOUDS;
			break;
		case KSTORAGE:
			// show storage storage
			if (! lex_if(self, KNAME, &tk))
				error("SHOW STORAGE <name> expected");
			type = SHOW_STORAGES;
			break;
		case KSTORAGES:
			type = SHOW_STORAGES;
			break;
		case KPARTITIONS:
			// show partitions [storage]
			lex_if(self, KNAME, &tk);
			type = SHOW_PARTITIONS;
			break;
		case KPIPELINE:
			type = SHOW_PIPELINE;
			break;
		case KCONFIG:
		case KALL:
			type = SHOW_CONFIG;
			break;
		case KNAME:
			type = SHOW_NAME;
			break;
		default:
			error("SHOW <MEMORY | WAL | CLOUDS | STORAGES | PARTITIONS | PIPELINE | CONFIG | NAME>");
		}

		// [debug]
		bool debug = lex_if(self, KDEBUG, NULL);

		auto op = cmd_show_allocate_op(type, &tk, debug);
		list_append(&cmd->list, &op->link);
		cmd->list_count++;

		// ,
		if (lex_if(self, ',', NULL))
			continue;

		break;
	}

	unguard(&guard);
	return &cmd->cmd;
}

Cmd*
parse_set(Lex* self)
{
	// SET name TO INT|STRING|BOOL

	// name
	Token name;
	if (! lex_if(self, KNAME, &name))
		error("SET <name> expected");

	// to
	if (! lex_if(self, KTO, NULL))
		error("SET name <TO> expected");

	// value
	Token value;
	lex_next(self, &value);

	return &cmd_set_allocate(&name, &value)->cmd;
}
