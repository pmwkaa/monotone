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

typedef struct Error Error;

struct Error
{
	char        text[256];
	int         text_len;
	const char* file;
	const char* function;
	int         line;
	LogFunction log;
	void*       log_arg;
};

static inline void
error_init(Error* self, LogFunction log, void* log_arg)
{
	self->text_len = 0;
	self->text[0]  = 0;
	self->file     = NULL;
	self->function = NULL;
	self->line     = 0;
	self->log      = log;
	self->log_arg  = log_arg;
}

static inline void
error_reset(Error* self)
{
	self->text_len = 0;
	self->text[0]  = 0;
	self->file     = NULL;
	self->function = NULL;
	self->line     = 0;
}

static inline void no_return
error_throw_as(Error*        self,
               ExceptionMgr* mgr,
               const char*   file,
               const char*   function, int line,
               const char*   fmt,
              ...)
{
	va_list args;
	if (self->log)
	{
		va_start(args, fmt);
		if (self->log)
			self->log(self->log_arg, file, function, line, "error: ", fmt, args);
		va_end(args);
	}
	va_start(args, fmt);
	self->file     = file;
	self->function = function;
	self->line     = line;
	self->text_len = vsnprintf(self->text, sizeof(self->text), fmt, args);
	va_end(args);
	exception_mgr_throw(mgr);
}
