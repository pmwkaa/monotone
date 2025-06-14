#ifndef MONOTONE_H
#define MONOTONE_H

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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#define MONOTONE_API __attribute__((visibility("default")))

typedef struct monotone        monotone_t;
typedef struct monotone_event  monotone_event_t;
typedef struct monotone_cursor monotone_cursor_t;

enum
{
	MONOTONE_DELETE = 1
};

struct monotone_event
{
	int      flags;
	uint64_t id;
	void*    key;
	size_t   key_size;
	void*    value;
	size_t   value_size;
};

// environment
MONOTONE_API monotone_t*
monotone_init(void);

MONOTONE_API void
monotone_free(void*);

MONOTONE_API const char*
monotone_error(monotone_t*);

MONOTONE_API int
monotone_open(monotone_t*, const char* path);

MONOTONE_API int
monotone_execute(monotone_t*, const char* command, char** result);

// batch write
MONOTONE_API int
monotone_write(monotone_t*, monotone_event_t*, int count);

// cursor
MONOTONE_API monotone_cursor_t*
monotone_cursor(monotone_t*, const char* options, monotone_event_t*);

MONOTONE_API int
monotone_read(monotone_cursor_t*, monotone_event_t*);

MONOTONE_API int
monotone_next(monotone_cursor_t*);

#ifdef __cplusplus
}
#endif

#endif // MONOTONE_H
