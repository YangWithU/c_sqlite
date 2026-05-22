#include "src/catalog/id_generator.h"

void id_generator_init(id_generator_t* gen) {
    /* System tables use IDs 0, 1, 2 (__tables, __columns, __indexes).
     * User tables start at 3 to avoid collisions. */
    gen->next_table_id  = 3;
    gen->next_column_id = 1;
    gen->next_index_id  = 1;
}

table_id_t id_generator_next_table(id_generator_t* gen) {
    return gen->next_table_id++;
}

column_id_t id_generator_next_column(id_generator_t* gen) {
    return gen->next_column_id++;
}

index_id_t id_generator_next_index(id_generator_t* gen) {
    return gen->next_index_id++;
}
