#pragma once

#include "src/common/types.h"

typedef struct {
    table_id_t  next_table_id;
    column_id_t next_column_id;
    index_id_t  next_index_id;
} id_generator_t;

void        id_generator_init(id_generator_t* gen);
table_id_t  id_generator_next_table(id_generator_t* gen);
column_id_t id_generator_next_column(id_generator_t* gen);
index_id_t  id_generator_next_index(id_generator_t* gen);
