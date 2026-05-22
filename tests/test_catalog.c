#include "xtest.h"
#include "src/common/config.h"
#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/mem.h"
#include "src/storage/schema.h"
#include "src/catalog/id_generator.h"
#include "src/catalog/catalog.h"

#include <string.h>
#include <stdlib.h>

/* =====================================================================
 * TEST: id_generator, sequential
 * ===================================================================== */

TEST(id_generator, sequential) {
    id_generator_t gen;
    id_generator_init(&gen);

    /* Generate 10 IDs of each type and verify they are sequential */
    table_id_t table_ids[10];
    for (int i = 0; i < 10; i++)
        table_ids[i] = id_generator_next_table(&gen);

    for (int i = 0; i < 10; i++)
        EXPECT_EQ(table_ids[i], (table_id_t)(i + 3));  /* starts at 3 (system tables 0,1,2) */

    column_id_t col_ids[10];
    for (int i = 0; i < 10; i++)
        col_ids[i] = id_generator_next_column(&gen);

    for (int i = 0; i < 10; i++)
        EXPECT_EQ(col_ids[i], (column_id_t)(i + 1));

    index_id_t idx_ids[10];
    for (int i = 0; i < 10; i++)
        idx_ids[i] = id_generator_next_index(&gen);

    for (int i = 0; i < 10; i++)
        EXPECT_EQ(idx_ids[i], (index_id_t)(i + 1));
}

/* =====================================================================
 * TEST: catalog, create_get_table
 * ===================================================================== */

TEST(catalog, create_get_table) {
    catalog_t cat;
    catalog_init(&cat, NULL);

    /* Create 3 tables with simple schemas */
    column_def_t cols1[] = {
        { .name = "id",   .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
    };
    schema_t s1;
    schema_create(&s1, cols1, 1);

    column_def_t cols2[] = {
        { .name = "id",   .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
        { .name = "name", .type = TYPE_VARCHAR, .nullable = true,  .max_length = 32 },
    };
    schema_t s2;
    schema_create(&s2, cols2, 2);

    column_def_t cols3[] = {
        { .name = "id",    .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
        { .name = "email", .type = TYPE_VARCHAR, .nullable = false, .max_length = 128 },
        { .name = "age",   .type = TYPE_INTEGER, .nullable = true,  .max_length = 0 },
    };
    schema_t s3;
    schema_create(&s3, cols3, 3);

    table_id_t t1 = catalog_create_table(&cat, "users", &s1);
    table_id_t t2 = catalog_create_table(&cat, "products", &s2);
    table_id_t t3 = catalog_create_table(&cat, "orders", &s3);

    EXPECT_TRUE(t1 != INVALID_TABLE_ID);
    EXPECT_TRUE(t2 != INVALID_TABLE_ID);
    EXPECT_TRUE(t3 != INVALID_TABLE_ID);

    /* Verify metadata via get_table */
    table_meta_t meta;

    EXPECT_EQ(catalog_get_table(&cat, "users", &meta), DB_OK);
    EXPECT_EQ(meta.table_id, t1);
    EXPECT_STR_EQ(meta.table_name, "users");
    EXPECT_STR_EQ(meta.table_type, "user");

    EXPECT_EQ(catalog_get_table(&cat, "products", &meta), DB_OK);
    EXPECT_EQ(meta.table_id, t2);
    EXPECT_STR_EQ(meta.table_name, "products");

    EXPECT_EQ(catalog_get_table(&cat, "orders", &meta), DB_OK);
    EXPECT_EQ(meta.table_id, t3);
    EXPECT_STR_EQ(meta.table_name, "orders");

    /* Nonexistent table should fail */
    EXPECT_EQ(catalog_get_table(&cat, "nonexistent", &meta), DB_UNKNOWN_TABLE);

    schema_destroy(&s1);
    schema_destroy(&s2);
    schema_destroy(&s3);
    catalog_destroy(&cat);
}

/* =====================================================================
 * TEST: catalog, create_schema
 * ===================================================================== */

TEST(catalog, create_schema) {
    catalog_t cat;
    catalog_init(&cat, NULL);

    /* Create a table with 5 columns */
    column_def_t cols[] = {
        { .name = "id",        .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
        { .name = "name",      .type = TYPE_VARCHAR, .nullable = false, .max_length = 64 },
        { .name = "email",     .type = TYPE_VARCHAR, .nullable = true,  .max_length = 128 },
        { .name = "age",       .type = TYPE_INTEGER, .nullable = true,  .max_length = 0 },
        { .name = "is_active", .type = TYPE_BOOLEAN,  .nullable = false, .max_length = 0 },
    };
    schema_t schema;
    schema_create(&schema, cols, 5);

    table_id_t tid = catalog_create_table(&cat, "employees", &schema);
    EXPECT_TRUE(tid != INVALID_TABLE_ID);

    /* Retrieve the schema */
    schema_t retrieved;
    int rc = catalog_get_schema(&cat, tid, &retrieved);
    EXPECT_EQ(rc, DB_OK);

    /* Verify column info */
    EXPECT_EQ(retrieved.num_columns, 5);
    EXPECT_STR_EQ(retrieved.columns[0].name, "id");
    EXPECT_EQ(retrieved.columns[0].type, TYPE_INTEGER);
    EXPECT_STR_EQ(retrieved.columns[1].name, "name");
    EXPECT_EQ(retrieved.columns[1].type, TYPE_VARCHAR);
    EXPECT_EQ(retrieved.columns[1].max_length, 64u);
    EXPECT_STR_EQ(retrieved.columns[2].name, "email");
    EXPECT_EQ(retrieved.columns[2].type, TYPE_VARCHAR);
    EXPECT_EQ(retrieved.columns[2].max_length, 128u);
    EXPECT_TRUE(retrieved.columns[2].nullable);
    EXPECT_STR_EQ(retrieved.columns[3].name, "age");
    EXPECT_EQ(retrieved.columns[3].type, TYPE_INTEGER);
    EXPECT_TRUE(retrieved.columns[3].nullable);
    EXPECT_STR_EQ(retrieved.columns[4].name, "is_active");
    EXPECT_EQ(retrieved.columns[4].type, TYPE_BOOLEAN);

    /* Verify computed schema properties */
    EXPECT_TRUE(retrieved.fixed_size > 0);
    EXPECT_EQ(retrieved.var_cols, 2u); /* name and email are VARCHAR */

    /* Nonexistent table schema should fail */
    schema_t bad_schema;
    EXPECT_EQ(catalog_get_schema(&cat, 9999, &bad_schema), DB_UNKNOWN_TABLE);

    schema_destroy(&schema);
    schema_destroy(&retrieved);
    catalog_destroy(&cat);
}

/* =====================================================================
 * TEST: catalog, drop_table
 * ===================================================================== */

TEST(catalog, drop_table) {
    catalog_t cat;
    catalog_init(&cat, NULL);

    column_def_t cols[] = {
        { .name = "id", .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
    };
    schema_t schema;
    schema_create(&schema, cols, 1);

    table_id_t tid = catalog_create_table(&cat, "temp_table", &schema);
    EXPECT_TRUE(tid != INVALID_TABLE_ID);

    /* Verify it exists */
    table_meta_t meta;
    EXPECT_EQ(catalog_get_table(&cat, "temp_table", &meta), DB_OK);

    /* Drop it */
    int rc = catalog_drop_table(&cat, "temp_table");
    EXPECT_EQ(rc, DB_OK);

    /* Verify get fails now */
    EXPECT_EQ(catalog_get_table(&cat, "temp_table", &meta), DB_UNKNOWN_TABLE);

    /* Schema should also be gone */
    schema_t retrieved;
    EXPECT_EQ(catalog_get_schema(&cat, tid, &retrieved), DB_UNKNOWN_TABLE);

    /* Dropping again should fail */
    EXPECT_EQ(catalog_drop_table(&cat, "temp_table"), DB_UNKNOWN_TABLE);

    schema_destroy(&schema);
    catalog_destroy(&cat);
}

/* =====================================================================
 * TEST: catalog, create_get_index
 * ===================================================================== */

TEST(catalog, create_get_index) {
    catalog_t cat;
    catalog_init(&cat, NULL);

    column_def_t cols[] = {
        { .name = "id",    .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
        { .name = "name",  .type = TYPE_VARCHAR, .nullable = true,  .max_length = 32 },
    };
    schema_t schema;
    schema_create(&schema, cols, 2);

    table_id_t tid = catalog_create_table(&cat, "users", &schema);
    EXPECT_TRUE(tid != INVALID_TABLE_ID);

    /* Create an index */
    index_id_t iid = catalog_create_index(&cat, "idx_users_name", tid,
                                           /*col_id=*/1, /*root_page_id=*/10, /*unique=*/1);
    EXPECT_TRUE(iid != INVALID_INDEX_ID);

    /* Get index by name */
    index_meta_t imeta;
    int rc = catalog_get_index(&cat, "idx_users_name", &imeta);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(imeta.index_id, iid);
    EXPECT_STR_EQ(imeta.index_name, "idx_users_name");
    EXPECT_EQ(imeta.table_id, tid);
    EXPECT_EQ(imeta.column_id, 1);
    EXPECT_EQ(imeta.root_page_id, 10);
    EXPECT_EQ(imeta.is_unique, 1);

    /* Nonexistent index should fail */
    EXPECT_EQ(catalog_get_index(&cat, "nonexistent", &imeta), DB_UNKNOWN_TABLE);

    /* Duplicate name should fail */
    index_id_t dup = catalog_create_index(&cat, "idx_users_name", tid, 1, 20, 0);
    EXPECT_EQ(dup, INVALID_INDEX_ID);

    schema_destroy(&schema);
    catalog_destroy(&cat);
}

/* =====================================================================
 * TEST: catalog, table_indexes
 * ===================================================================== */

TEST(catalog, table_indexes) {
    catalog_t cat;
    catalog_init(&cat, NULL);

    column_def_t cols[] = {
        { .name = "id",    .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
        { .name = "name",  .type = TYPE_VARCHAR, .nullable = true,  .max_length = 32 },
        { .name = "email", .type = TYPE_VARCHAR, .nullable = true,  .max_length = 64 },
    };
    schema_t schema;
    schema_create(&schema, cols, 3);

    table_id_t tid = catalog_create_table(&cat, "accounts", &schema);
    EXPECT_TRUE(tid != INVALID_TABLE_ID);

    /* Create 2 indexes on the same table */
    index_id_t iid1 = catalog_create_index(&cat, "idx_accounts_name", tid,
                                            /*col_id=*/1, /*root_page_id=*/20, /*unique=*/0);
    index_id_t iid2 = catalog_create_index(&cat, "idx_accounts_email", tid,
                                            /*col_id=*/2, /*root_page_id=*/30, /*unique=*/1);
    EXPECT_TRUE(iid1 != INVALID_INDEX_ID);
    EXPECT_TRUE(iid2 != INVALID_INDEX_ID);

    /* Get all indexes for the table */
    vector_t indexes;
    catalog_get_table_indexes(&cat, tid, &indexes);
    EXPECT_EQ(vector_size(&indexes), 2u);

    /* Verify the indexes belong to the right table and have expected columns */
    int found_name = 0, found_email = 0;
    for (size_t i = 0; i < vector_size(&indexes); i++) {
        index_meta_t* imeta = (index_meta_t*)vector_at(&indexes, i);
        EXPECT_EQ(imeta->table_id, tid);
        if (strcmp(imeta->index_name, "idx_accounts_name") == 0) {
            found_name = 1;
            EXPECT_EQ(imeta->column_id, 1);
            EXPECT_EQ(imeta->root_page_id, 20);
            EXPECT_EQ(imeta->is_unique, 0);
        }
        if (strcmp(imeta->index_name, "idx_accounts_email") == 0) {
            found_email = 1;
            EXPECT_EQ(imeta->column_id, 2);
            EXPECT_EQ(imeta->root_page_id, 30);
            EXPECT_EQ(imeta->is_unique, 1);
        }
    }
    EXPECT_TRUE(found_name);
    EXPECT_TRUE(found_email);

    /* Dropping the table should remove the indexes too */
    catalog_drop_table(&cat, "accounts");
    vector_t after_indexes;
    catalog_get_table_indexes(&cat, tid, &after_indexes);
    EXPECT_EQ(vector_size(&after_indexes), 0u);

    vector_destroy(&indexes);
    vector_destroy(&after_indexes);
    schema_destroy(&schema);
    catalog_destroy(&cat);
}

/* =====================================================================
 * TEST: catalog, list_tables
 * ===================================================================== */

TEST(catalog, list_tables) {
    catalog_t cat;
    catalog_init(&cat, NULL);

    column_def_t cols[] = {
        { .name = "id", .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
    };
    schema_t schema;
    schema_create(&schema, cols, 1);

    /* Create 3 user tables */
    catalog_create_table(&cat, "alpha", &schema);
    catalog_create_table(&cat, "beta", &schema);
    catalog_create_table(&cat, "gamma", &schema);

    /* List tables — should only return user tables (not system tables) */
    vector_t names;
    int rc = catalog_list_tables(&cat, &names);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(vector_size(&names), 3u);

    /* Verify all three names are present */
    int found_alpha = 0, found_beta = 0, found_gamma = 0;
    for (size_t i = 0; i < vector_size(&names); i++) {
        char* name = *(char**)vector_at(&names, i);
        if (strcmp(name, "alpha") == 0)   found_alpha = 1;
        if (strcmp(name, "beta") == 0)    found_beta = 1;
        if (strcmp(name, "gamma") == 0)   found_gamma = 1;
    }
    EXPECT_TRUE(found_alpha);
    EXPECT_TRUE(found_beta);
    EXPECT_TRUE(found_gamma);

    /* Clean up the name strings */
    for (size_t i = 0; i < vector_size(&names); i++) {
        char* name = *(char**)vector_at(&names, i);
        free(name);
    }
    vector_destroy(&names);

    schema_destroy(&schema);
    catalog_destroy(&cat);
}
