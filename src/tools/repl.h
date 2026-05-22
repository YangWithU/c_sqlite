#pragma once

#include "src/executor/execution_engine.h"

/* Interactive REPL for MiniSQLite */
typedef struct {
    execution_engine_t* engine;
    const char*         db_path;
    int                 running;
} repl_t;

/* Initialize the REPL */
void repl_init(repl_t* repl, execution_engine_t* engine, const char* db_path);

/* Run the interactive loop */
void repl_run(repl_t* repl);

/* Stop the REPL */
void repl_stop(repl_t* repl);
