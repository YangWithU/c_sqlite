#pragma once

typedef struct {
    int echo;
    int stop_on_error;
    int error_count;
} sql_runner_t;

void sql_runner_init(sql_runner_t* runner);
void sql_runner_destroy(sql_runner_t* runner);
int  sql_runner_execute_file(sql_runner_t* runner, const char* path);
int  sql_runner_execute_string(sql_runner_t* runner, const char* sql);
