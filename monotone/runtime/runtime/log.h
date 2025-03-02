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

typedef void (*LogFunction)(void* arg,
                            const char* file,
                            const char* function, int line,
                            const char* prefix,
                            const char* fmt,
                            va_list args);

static inline void
log_at(LogFunction log, void* log_arg,
       const char* file,
       const char* function, int line,
       const char* fmt,
       ...)
{
	if (log == NULL)
		return;
	va_list args;
	va_start(args, fmt);
	log(log_arg, file, function, line, "", fmt, args);
	va_end(args);
}
