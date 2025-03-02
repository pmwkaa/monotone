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

typedef struct Test       Test;
typedef struct TestCursor TestCursor;
typedef struct TestGroup  TestGroup;
typedef struct TestSuite  TestSuite;

struct Test
{
	char*      name;
	char*      description;
	TestGroup* group;
	List       link_group;
	List       link;
};

struct TestGroup
{
	char* name;
	char* directory;
	bool  optional;
	List  list_test;
	List  link;
};

struct TestCursor
{
	char*              name;
	monotone_cursor_t* cursor;
	List               link;
};

struct TestSuite
{
	void*       dlhandle;
	char*       current_test_options;
	char*       current_test;
	FILE*       current_test_result;
	int         current_test_ok_exists;
	char*       current_test_ok_file;
	char*       current_test_result_file;
	int         current_test_started;
	int         current_line;
	int         current_plan_line;
	TestGroup*  current_group;
	Test*       current;
	monotone_t* env;
	int         list_cursor_count;
	List        list_cursor;
	char*       option_test_dir;
	char*       option_result_dir;
	char*       option_plan_file;
	char*       option_test;
	char*       option_group;
	int         option_fix;
	List        list_test;
	List        list_group;
};

void test_suite_init(TestSuite*);
void test_suite_free(TestSuite*);
void test_suite_cleanup(TestSuite*);
int  test_suite_run(TestSuite*);
