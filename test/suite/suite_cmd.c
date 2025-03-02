
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

#include <monotone_private.h>
#include <monotone.h>
#include <monotone_test.h>
#include <dlfcn.h>

static TestCursor*
test_cursor_new(TestSuite* self, const char* name)
{
	TestCursor* cursor = malloc(sizeof(*cursor));
	if (cursor == NULL)
		return NULL;
	cursor->name = strdup(name);
	if (cursor->name == NULL) {
		free(cursor);
		return NULL;
	}
	cursor->cursor = NULL;
	list_init(&cursor->link);
	list_append(&self->list_cursor, &cursor->link);
	self->list_cursor_count++;
	return cursor;
}

static TestCursor*
test_cursor_find(TestSuite* self, const char* name)
{
	list_foreach(&self->list_cursor)
	{
		auto cursor = list_at(TestCursor, link);
		if (strcasecmp(name, cursor->name) == 0)
			return cursor;
	}
	return NULL;
}

static void
test_cursor_free(TestSuite* self, TestCursor* cursor)
{
	list_unlink(&cursor->link);
	self->list_cursor_count--;
	if (cursor->cursor)
		monotone_free(cursor->cursor);
	free(cursor->name);
	free(cursor);
}

static int
test_suite_cmd_trap(TestSuite* self, char* arg)
{
	unused(self);
	unused(arg);

	kill(getpid(), SIGTRAP);
	return 0;
}

static int
test_suite_cmd_files(TestSuite* self, char* arg)
{
	char* arg_dir = test_arg(&arg);

	char path[PATH_MAX];
	if (arg_dir)
		snprintf(path, sizeof(path), "%s/%s", self->option_result_dir, arg_dir);
	else
		snprintf(path, sizeof(path), "%s", self->option_result_dir);

	DIR* dir = opendir(path);
	if (unlikely(dir == NULL))
	{
		test_error(self, "failed to open directory: %s", path);
		return -1;
	}

	for (;;)
	{
		auto entry = readdir(dir);
		if (entry == NULL)
			break;
		if (entry->d_name[0] == '.')
			continue;
		test_log(self, "%s\n", entry->d_name);
	}

	closedir(dir);
	return 0;
}

static int
test_suite_cmd_unit(TestSuite* self, char* arg)
{
	char* name = test_arg(&arg);
	if (name == NULL)
	{
		test_error(self, "line %d: unit <name> expected",
		           self->current_line);
		return -1;
	}

	void* ptr = dlsym(self->dlhandle, name);
	if (ptr == NULL)
	{
		test_error(self, "unit: test '%s' function not found\n", name);
		return -1;
	}
	void (*test_function)(void) = ptr;
	test_function();
	return 0;
}

static int
test_suite_cmd_init(TestSuite* self, char* arg)
{
	unused(arg);
	if (self->env)
	{
		test_error(self, "line %d: init: env already created",
		           self->current_line);
		return -1;
	}

	auto env = monotone_init();
	if (env == NULL) {
		test_error(self, "line %d: init: monotone_init() failed",
		           self->current_line);
		return -1;
	}

	self->env = env;
	return 0;
}

static int
test_suite_cmd_open(TestSuite* self, char* arg)
{
	unused(arg);
	if (! self->env)
	{
		test_error(self, "line %d: open: env is not created",
		           self->current_line);
		return -1;
	}

	int rc;
	rc = monotone_open(self->env, self->option_result_dir);
	if (rc == -1)
	{
		test_log(self, "error: %s\n", monotone_error(self->env));
		return 0;
	}
	return 0;
}

static int
test_suite_cmd_close(TestSuite* self, char* arg)
{
	unused(arg);
	if (! self->env)
	{
		test_log(self, "close: env is not created\n");
		return 0;
	}
	if (self->list_cursor_count > 0) {
		test_error(self, "line %d: close: env has openned cursors left",
		           self->current_line);
		return -1;
	}
	monotone_free(self->env);
	self->env = NULL;
	return 0;
}

static int
test_suite_cmd_error(TestSuite* self, char* arg)
{
	unused(arg);
	test_log_error(self);
	return 0;
}

static int
test_suite_cmd_insert(TestSuite* self, char* arg)
{
	char* arg_id    = test_arg(&arg);
	char* arg_value = test_arg(&arg);

	if (arg_id == NULL)
	{
		test_error(self, "line %d: insert <id> [value] expected",
		           self->current_line);
		return -1;
	}

	if (! self->env)
	{
		test_log(self, "error: env is not created\n");
		return 0;
	}

	monotone_event_t event =
	{
		.flags      = 0,
		.id         = strtoull(arg_id, NULL, 10),
		.key        = NULL,
		.key_size   = 0,
		.value      = arg_value,
		.value_size = arg_value ? strlen(arg_value) : 0,
	};
	int rc = monotone_write(self->env, &event, 1);
	if (rc == -1)
		test_log_error(self);

	return 0;
}

static int
test_suite_cmd_delete(TestSuite* self, char* arg)
{
	char* arg_id    = test_arg(&arg);
	char* arg_value = test_arg(&arg);

	if (arg_id == NULL)
	{
		test_error(self, "line %d: insert <id> [value] expected",
		           self->current_line);
		return -1;
	}

	if (! self->env)
	{
		test_log(self, "error: env is not openned\n");
		return 0;
	}

	monotone_event_t event =
	{
		.id         = strtoull(arg_id, NULL, 10),
		.key        = NULL,
		.key_size   = 0,
		.value      = arg_value,
		.value_size = arg_value ? strlen(arg_value) : 0,
		.flags      = MONOTONE_DELETE
	};
	int rc = monotone_write(self->env, &event, 1);
	if (rc == -1)
		test_log_error(self);

	return 0;
}

static int
test_suite_cmd_cursor(TestSuite* self, char* arg)
{
	char* arg_name = test_arg(&arg);
	char* arg_id   = test_arg(&arg);
	char* arg_key  = test_arg(&arg);

	if (arg_name == NULL)
	{
		test_error(self, "line %d: cursor <name> <id> [value] expected",
		           self->current_line);
		return -1;
	}

	if (! self->env)
	{
		test_log(self, "error: env is not openned\n");
		return 0;
	}

	auto cursor = test_cursor_find(self, arg_name);
	if (cursor)
	{
		test_error(self, "line %d: cursor redefined",
		           self->current_plan_line);
		return -1;
	}

	cursor = test_cursor_new(self, arg_name);

	monotone_event_t* key = NULL;
	monotone_event_t  event;
	if (arg_id)
	{
		event.flags      = 0;
		event.id         = strtoull(arg_id, NULL, 10);
		event.key        = arg_key;
		event.key_size   = arg_key ? strlen(arg_key) : 0;
		event.value      = NULL;
		event.value_size = 0;
		key = &event;
	}

	cursor->cursor = monotone_cursor(self->env, NULL, key);
	if (cursor->cursor == NULL)
		test_log_error(self);

	return 0;
}

static int
test_suite_cmd_cursor_close(TestSuite* self, char* arg)
{
	char* arg_name = test_arg(&arg);
	if (arg_name == NULL)
	{
		test_error(self, "line %d: cursor_close <name> expected",
		           self->current_line);
		return -1;
	}

	if (! self->env)
	{
		test_log(self, "error: env is not openned\n");
		return 0;
	}

	auto cursor = test_cursor_find(self, arg_name);
	if (! cursor)
	{
		test_error(self, "line %d: cursor not found",
		           self->current_plan_line);
		return -1;
	}

	test_cursor_free(self, cursor);
	return 0;
}

static inline void
test_log_event(TestSuite* self, monotone_event_t* event)
{
	if (event->key_size > 0 && event->value_size > 0)
	{
		test_log(self, "[%" PRIu64 ", %.*s, %.*s]\n", event->id, event->key_size, event->key,
		         event->value_size, event->value);
	} else
	if (event->key_size > 0)
	{
		test_log(self, "[%" PRIu64 ", %.*s]\n", event->id, event->key_size, event->key);
	} else
	if (event->value_size > 0)
	{
		test_log(self, "[%" PRIu64 ", %.*s]\n", event->id, event->value_size, event->value);
	} else
	{
		test_log(self, "[%" PRIu64 "]\n", event->id);
	}
}

static int
test_suite_cmd_read(TestSuite* self, char* arg)
{
	char* arg_name = test_arg(&arg);
	if (arg_name == NULL)
	{
		test_error(self, "line %d: read <cursor> expected",
		           self->current_line);
		return -1;
	}

	if (! self->env)
	{
		test_log(self, "error: env is not openned\n");
		return 0;
	}

	auto cursor = test_cursor_find(self, arg_name);
	if (! cursor)
	{
		test_error(self, "line %d: cursor not found",
		           self->current_plan_line);
		return -1;
	}
	if (cursor->cursor == NULL)
		return -1;

	monotone_event_t event;
	int rc = monotone_read(cursor->cursor, &event);
	if (rc == 0)
		test_log(self, "(eof)\n");
	else
		test_log_event(self, &event);
	return 0;
}

static int
test_suite_cmd_next(TestSuite* self, char* arg)
{
	char* arg_name = test_arg(&arg);

	if (arg_name == NULL)
	{
		test_error(self, "line %d: next <cursor> expected",
		           self->current_line);
		return -1;
	}

	if (! self->env)
	{
		test_log(self, "error: env is not openned\n");
		return 0;
	}

	auto cursor = test_cursor_find(self, arg_name);
	if (! cursor)
	{
		test_error(self, "line %d: cursor not found",
		           self->current_plan_line);
		return -1;
	}

	if (cursor->cursor == NULL)
		return -1;

	int rc = monotone_next(cursor->cursor);
	if (rc == -1)
		test_log_error(self);
	else
	if (rc == 0)
		test_log(self, "(eof)\n");

	return 0;
}

static int
test_suite_cmd_select(TestSuite* self, char* arg)
{
	char* arg_id    = test_arg(&arg);
	char* arg_value = test_arg(&arg);

	if (! self->env)
	{
		test_log(self, "error: env is not openned\n");
		return 0;
	}

	monotone_event_t* key = NULL;
	monotone_event_t  event;
	if (arg_id)
	{
		event.flags      = 0;
		event.id         = strtoull(arg_id, NULL, 10);
		event.key        = arg_value;
		event.key_size   = arg_value ? strlen(arg_value) : 0;
		event.value      = NULL;
		event.value_size = 0;
		key = &event;
	}

	auto cursor = monotone_cursor(self->env, NULL, key);
	if (cursor == NULL)
	{
		test_log_error(self);
		return 0;
	}

	while (monotone_read(cursor, &event))
	{
		test_log_event(self, &event);
		int rc = monotone_next(cursor);
		if (rc == -1)
		{
			test_log_error(self);
			break;
		}
	}

	test_log(self, "(eof)\n");

	monotone_free(cursor);
	return 0;
}

static struct
{
	const char* name;
	int         name_size;
	int        (*function)(TestSuite*, char*);
} cmds[] =
{
	// test suite specific
	{ "trap",         4,  test_suite_cmd_trap         },
	{ "files",        5,  test_suite_cmd_files        },
	{ "unit",         4,  test_suite_cmd_unit         },

	// api
	{ "init",         4,  test_suite_cmd_init         },
	{ "open",         4,  test_suite_cmd_open         },
	{ "close",        5,  test_suite_cmd_close        },
	{ "error",        5,  test_suite_cmd_error        },
	{ "insert",       6,  test_suite_cmd_insert       },
	{ "delete",       6,  test_suite_cmd_delete       },
	{ "cursor_close", 12, test_suite_cmd_cursor_close },
	{ "cursor",       6,  test_suite_cmd_cursor       },
	{ "read",         4,  test_suite_cmd_read         },
	{ "next",         4,  test_suite_cmd_next         },
	{ "select",       6,  test_suite_cmd_select       },
	{  NULL,          0,  NULL                        }
};

int
test_suite_cmd(TestSuite* self, char* query)
{
	for (int i = 0; cmds[i].name; i++)
		if (! strncmp(query, cmds[i].name, cmds[i].name_size))
			return cmds[i].function(self, query + cmds[i].name_size);

	// execute command
	if (! self->env)
	{
		test_error(self, "line %d: open: env is not created",
		           self->current_line);
		return -1;
	}

	char* result = NULL;
	int rc = monotone_execute(self->env, query, &result);
	if (rc == -1)
	{
		test_log_error(self);
	} else
	{
		if (result)
		{
			test_log(self, "%s\n", result);
			free(result);
		}
	}

	return 0;
}
