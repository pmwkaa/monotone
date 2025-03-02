
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

enum
{
	MONOTONE_OBJ        = 0x3fb15941,
	MONOTONE_OBJ_CURSOR = 0x143BAF02,
	MONOTONE_OBJ_FREED  = 0x0000dead
};

struct monotone
{
	int  type;
	Main main;
};

struct monotone_cursor
{
	int              type;
	EngineCursor     cursor;
	struct monotone* env;
};

static inline void
monotone_enter(monotone_t* self)
{
	runtime_init(&self->main.context);
}

MONOTONE_API monotone_t*
monotone_init(void)
{
	monotone_t* self = malloc(sizeof(monotone_t));
	if (unlikely(self == NULL))
		return NULL;
	self->type = MONOTONE_OBJ;
	main_init(&self->main);

	monotone_enter(self);
	Exception e;
	if (enter(&e)) {
		main_prepare(&self->main);
	}
	if (leave(&e)) {
		main_free(&self->main);
		free(self);
		self = NULL;
	}
	return self;
}

MONOTONE_API void
monotone_free(void* ptr)
{
	switch (*(int*)ptr) {
	case MONOTONE_OBJ:
	{
		monotone_t* self = ptr;
		monotone_enter(self);
		Exception e;
		if (enter(&e))
		{
			main_stop(&self->main);
			main_free(&self->main);
		}
		if (leave(&e))
		{ }
		break;
	}
	case MONOTONE_OBJ_CURSOR:
	{
		monotone_cursor_t* self = ptr;
		monotone_t* env = self->env;
		monotone_enter(env);
		Exception e;
		if (enter(&e)) {
			engine_cursor_close(&self->cursor);
		}
		if (leave(&e))
		{ }
		break;
	}
	case MONOTONE_OBJ_FREED:
		fprintf(stderr, "\n%s(%p): attempt to use freed object\n",
		        source_function, ptr);
		fflush(stderr);
		abort();
		break;
	default:
		fprintf(stderr, "\n%s(%p): unexpected object type\n",
		        source_function, ptr);
		fflush(stderr);
		abort();
		return;
	}

	*(int*)ptr = MONOTONE_OBJ_FREED;
	free(ptr);
}

MONOTONE_API const char*
monotone_error(monotone_t* self)
{
	unused(self);
	return mn_runtime.error.text;
}

MONOTONE_API int
monotone_open(monotone_t* self, const char* directory)
{
	int rc = 0;
	monotone_enter(self);
	Exception e;
	if (enter(&e)) {
		main_start(&self->main, directory);
	}
	if (leave(&e)) {
		rc = -1;
	}
	return rc;
}

MONOTONE_API int
monotone_execute(monotone_t* self, const char* command, char** result)
{
	int rc = 0;
	monotone_enter(self);
	Exception e;
	if (enter(&e)) {
		main_execute(&self->main, command, result);
	}
	if (leave(&e)) {
		rc = -1;
	}
	return rc;
}

MONOTONE_API int
monotone_write(monotone_t* self, monotone_event_t* events, int count)
{
	monotone_enter(self);
	int rc = 0;
	Exception e;
	if (enter(&e)) {
		engine_write(&self->main.engine, (EventArg*)events, count);
	}
	if (leave(&e)) {
		rc = -1;
	}
	return rc;
}

hot MONOTONE_API monotone_cursor_t*
monotone_cursor(monotone_t* self, const char* options, monotone_event_t* key)
{
	unused(options);
	monotone_enter(self);

	monotone_cursor_t* cursor;
	cursor = malloc(sizeof(monotone_cursor_t));
	if (unlikely(cursor == NULL))
		return NULL;
	cursor->type = MONOTONE_OBJ_CURSOR;
	cursor->env  = self;
	engine_cursor_init(&cursor->cursor);

	Exception e;
	if (enter(&e))
	{
		if (key == NULL) {
			engine_cursor_open(&cursor->cursor, &self->main.engine, NULL);
		} else
		{
			auto copy = event_malloc((EventArg*)key);
			guard(event_free, copy);
			engine_cursor_open(&cursor->cursor, &self->main.engine, copy);
		}
		engine_cursor_skip_deletes(&cursor->cursor);
	}

	if (leave(&e))
	{ }

	if (unlikely(e.triggered))
	{
		if (enter(&e)) {
			engine_cursor_close(&cursor->cursor);
			mn_free(cursor);
			cursor = NULL;
		}
		if (leave(&e))
		{ }
	}

	return cursor;
}

hot MONOTONE_API int
monotone_read(monotone_cursor_t* self, monotone_event_t* event)
{
	monotone_enter(self->env);
	int rc;
	Exception e;
	if (enter(&e))
	{
		auto at = engine_cursor_at(&self->cursor);
		if (likely(at))
		{
			event->flags      = at->flags;
			event->id         = at->id;
			event->key_size   = at->key_size;
			event->key        = event_key(at);
			event->value_size = at->data_size - at->key_size;
			event->value      = event_value(at);
			rc = 1;

			// this situation is possible on concurrent delete
			// from the same thread
			if (unlikely(event_is_delete(at)))
				rc = 0;

		} else {
			rc = 0;
		}
	}
	if (leave(&e)) {
		rc = -1;
	}
	return rc;
}

hot MONOTONE_API int
monotone_next(monotone_cursor_t* self)
{
	monotone_enter(self->env);
	int rc;
	Exception e;
	if (enter(&e)) {
		engine_cursor_next(&self->cursor);
		engine_cursor_skip_deletes(&self->cursor);
		rc = engine_cursor_has(&self->cursor);
	}
	if (leave(&e)) {
		rc = -1;
	}
	return rc;
}
