#include "xtest.h"
#include "src/common/config.h"
#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/mem.h"
#include "src/storage/disk_manager.h"
#include "src/storage/page.h"
#include "src/storage/slotted_page.h"
#include "src/storage/value.h"
#include "src/storage/schema.h"
#include "src/storage/tuple.h"
#include "src/storage/heap_file.h"
#include "src/storage/free_space_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Helper: create a temporary db file path, cleaned up via atexit */
static char temp_db_path[256];

static void cleanup_temp_files(void) {
    if (temp_db_path[0])
        unlink(temp_db_path);
}

static const char* make_temp_db_path(void) {
    static int initialized = 0;
    if (!initialized) {
        atexit(cleanup_temp_files);
        initialized = 1;
    }
    snprintf(temp_db_path, sizeof(temp_db_path), "/tmp/test_minisqlite_%d.db", (int)getpid());
    return temp_db_path;
}

/* =====================================================================
 * TEST: disk_manager, allocateReadWrite
 * ===================================================================== */

TEST(disk_manager, allocate_read_write) {
    const char* path = make_temp_db_path();
    unlink(path);

    disk_manager_t dm;
    int rc = disk_manager_open(&dm, path);
    EXPECT_EQ(rc, DB_OK);

    /* Allocate 10 pages */
    page_id_t pages[10];
    for (int i = 0; i < 10; i++) {
        int pid = disk_manager_allocate_page(&dm);
        EXPECT_TRUE(pid > 0);
        pages[i] = (page_id_t)pid;
    }

    /* Write different content to each page */
    for (int i = 0; i < 10; i++) {
        char write_buf[PAGE_SIZE];
        memset(write_buf, 0, PAGE_SIZE);
        /* Fill page with a pattern based on its index */
        int pattern = 0xA0 + i;
        memset(write_buf + PAGE_HEADER_SIZE, pattern, PAGE_SIZE - PAGE_HEADER_SIZE);
        /* Also set the page_id in the header for verification */
        page_header_set_page_id(write_buf, pages[i]);

        rc = disk_manager_write_page(&dm, pages[i], write_buf);
        EXPECT_EQ(rc, DB_OK);
    }

    /* Read back and verify */
    for (int i = 0; i < 10; i++) {
        char read_buf[PAGE_SIZE];
        rc = disk_manager_read_page(&dm, pages[i], read_buf);
        EXPECT_EQ(rc, DB_OK);

        /* Verify page_id in header */
        int32_t pid = page_header_get_page_id(read_buf);
        EXPECT_EQ(pid, pages[i]);

        /* Verify the fill pattern */
        int pattern = 0xA0 + i;
        EXPECT_EQ((unsigned char)read_buf[PAGE_HEADER_SIZE], (unsigned char)pattern);
    }

    disk_manager_close(&dm);
    unlink(path);
}

/* =====================================================================
 * TEST: disk_manager, dealloc_reuse
 * ===================================================================== */

TEST(disk_manager, dealloc_reuse) {
    const char* path = make_temp_db_path();
    unlink(path);

    disk_manager_t dm;
    int rc = disk_manager_open(&dm, path);
    EXPECT_EQ(rc, DB_OK);

    /* Allocate 5 pages */
    page_id_t pages[5];
    for (int i = 0; i < 5; i++) {
        pages[i] = disk_manager_allocate_page(&dm);
        EXPECT_TRUE(pages[i] > 0);
    }

    page_id_t p1 = pages[1], p2 = pages[2];

    /* Deallocate the middle page (pages[2]) */
    rc = disk_manager_deallocate_page(&dm, p1);
    EXPECT_EQ(rc, DB_OK);

    /* Allocate a new page — should reuse the deallocated one */
    page_id_t reused = disk_manager_allocate_page(&dm);
    EXPECT_EQ(reused, p1);  /* should get the same page back */

    /* Deallocate another page and verify reuse */
    rc = disk_manager_deallocate_page(&dm, p2);
    EXPECT_EQ(rc, DB_OK);

    page_id_t reused2 = disk_manager_allocate_page(&dm);
    EXPECT_EQ(reused2, p2);

    disk_manager_close(&dm);
    unlink(path);
}

/* =====================================================================
 * TEST: slotted_page, insert_get_delete
 * ===================================================================== */

TEST(slotted_page, insert_get_delete) {
    char page[PAGE_SIZE];
    slotted_page_init(page, 1);

    /* Insert 5 tuples */
    const char* data[5] = { "tuple0", "tuple1", "tuple2", "tuple3", "tuple4" };
    uint16_t sizes[5];
    slot_id_t slots[5];

    for (int i = 0; i < 5; i++) {
        sizes[i] = (uint16_t)(strlen(data[i]) + 1);  /* include null terminator */
        int sid = slotted_page_insert(page, data[i], sizes[i]);
        EXPECT_TRUE(sid >= 0);
        slots[i] = (slot_id_t)sid;
    }

    /* Read back and verify */
    for (int i = 0; i < 5; i++) {
        const char* tuple_data = NULL;
        uint16_t tuple_size = 0;
        int rc = slotted_page_get(page, slots[i], &tuple_data, &tuple_size);
        EXPECT_EQ(rc, DB_OK);
        EXPECT_TRUE(tuple_size > 0);
        EXPECT_STR_EQ(tuple_data, data[i]);
    }

    /* Delete the middle tuple (slot 2) */
    int rc = slotted_page_delete(page, slots[2]);
    EXPECT_EQ(rc, DB_OK);

    /* Verify the deleted slot returns error */
    const char* deleted_data = NULL;
    uint16_t deleted_size = 0;
    rc = slotted_page_get(page, slots[2], &deleted_data, &deleted_size);
    EXPECT_NE(rc, DB_OK);

    /* Verify other tuples are still accessible */
    for (int i = 0; i < 5; i++) {
        if (i == 2) continue;
        const char* tuple_data = NULL;
        uint16_t tuple_size = 0;
        rc = slotted_page_get(page, slots[i], &tuple_data, &tuple_size);
        EXPECT_EQ(rc, DB_OK);
        EXPECT_STR_EQ(tuple_data, data[i]);
    }

    /* Verify tuple count */
    uint32_t num_tuples = slotted_page_num_tuples(page);
    EXPECT_EQ(num_tuples, 4u);
}

/* =====================================================================
 * TEST: value, make_compare_arithmetic
 * ===================================================================== */

TEST(value, make_compare_arithmetic) {
    /* Create values of each type */
    value_t vi = value_make_integer(42);
    value_t vf = value_make_float(3.14);
    value_t vs = value_make_varchar("hello");
    value_t vb = value_make_boolean(true);
    value_t vn = value_make_null();

    /* Verify type tags */
    EXPECT_EQ(vi.type, TYPE_INTEGER);
    EXPECT_EQ(vf.type, TYPE_FLOAT);
    EXPECT_EQ(vs.type, TYPE_VARCHAR);
    EXPECT_EQ(vb.type, TYPE_BOOLEAN);
    EXPECT_EQ(vn.type, TYPE_NULL);

    /* Verify stored values */
    EXPECT_EQ(vi.val.int_val, 42);
    EXPECT_DOUBLE_EQ(vf.val.float_val, 3.14);
    EXPECT_STR_EQ(vs.val.varchar, "hello");
    EXPECT_TRUE(vb.val.bool_val);

    /* Comparison: same types */
    value_t vi2 = value_make_integer(100);
    EXPECT_EQ(value_compare(&vi, &vi2), -1);   /* 42 < 100 */
    EXPECT_EQ(value_compare(&vi2, &vi), 1);    /* 100 > 42 */
    EXPECT_EQ(value_compare(&vi, &vi), 0);     /* 42 == 42 */

    /* Comparison: NULL is always least */
    EXPECT_EQ(value_compare(&vn, &vi), -1);
    EXPECT_EQ(value_compare(&vi, &vn), 1);
    EXPECT_EQ(value_compare(&vn, &vn), 0);

    /* Comparison: cross-type INTEGER/FLOAT */
    value_t vf42 = value_make_float(42.0);
    EXPECT_EQ(value_compare(&vi, &vf42), 0);   /* 42 == 42.0 */

    /* Comparison: VARCHAR (strcmp may return any negative value, not just -1) */
    value_t vs2 = value_make_varchar("world");
    EXPECT_TRUE(value_compare(&vs, &vs2) < 0);    /* "hello" < "world" */

    /* Arithmetic: add */
    value_t result;
    int rc = value_add(&vi, &vi2, &result);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(result.type, TYPE_INTEGER);
    EXPECT_EQ(result.val.int_val, 142);

    /* Arithmetic: add int + float => float */
    rc = value_add(&vi, &vf, &result);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(result.type, TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(result.val.float_val, 45.14);

    /* Arithmetic: subtract */
    rc = value_sub(&vi2, &vi, &result);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(result.val.int_val, 58);

    /* Arithmetic: multiply */
    rc = value_mul(&vi, &vi2, &result);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(result.val.int_val, 4200);

    /* Arithmetic: divide */
    rc = value_div(&vi2, &vi, &result);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(result.val.int_val, 2);  /* 100/42 = 2 (integer division) */

    /* Division by zero */
    value_t vzero = value_make_integer(0);
    rc = value_div(&vi, &vzero, &result);
    EXPECT_NE(rc, DB_OK);

    /* Type mismatch on arithmetic with VARCHAR */
    rc = value_add(&vs, &vi, &result);
    EXPECT_NE(rc, DB_OK);

    /* Cast */
    value_t cast_result;
    rc = value_cast_to(&vi, TYPE_FLOAT, &cast_result);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(cast_result.type, TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(cast_result.val.float_val, 42.0);
    value_destroy(&cast_result);

    rc = value_cast_to(&vf, TYPE_INTEGER, &cast_result);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(cast_result.type, TYPE_INTEGER);
    EXPECT_EQ(cast_result.val.int_val, 3);
    value_destroy(&cast_result);

    rc = value_cast_to(&vi, TYPE_VARCHAR, &cast_result);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(cast_result.type, TYPE_VARCHAR);
    EXPECT_STR_EQ(cast_result.val.varchar, "42");
    value_destroy(&cast_result);

    /* Clean up VARCHAR values */
    value_destroy(&vs);
    value_destroy(&vs2);
    value_destroy(&vf42);
    value_destroy(&vzero);
    /* Non-VARCHAR values don't need destroy, but it's safe to call */
}

/* =====================================================================
 * TEST: schema, fixed_variable_size
 * ===================================================================== */

TEST(schema, fixed_variable_size) {
    /* Define a schema: id INTEGER, name VARCHAR(50), score FLOAT, active BOOLEAN */
    column_def_t cols[] = {
        { .name = "id",     .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
        { .name = "name",   .type = TYPE_VARCHAR, .nullable = true,  .max_length = 50 },
        { .name = "score",  .type = TYPE_FLOAT,   .nullable = true,  .max_length = 0 },
        { .name = "active", .type = TYPE_BOOLEAN,  .nullable = false, .max_length = 0 },
    };

    schema_t schema;
    int rc = schema_create(&schema, cols, 4);
    EXPECT_EQ(rc, DB_OK);

    /* Verify column fixed sizes */
    EXPECT_EQ(schema_column_fixed_size(TYPE_INTEGER), 8u);  /* int64_t = 8 */
    EXPECT_EQ(schema_column_fixed_size(TYPE_FLOAT), 8u);    /* double = 8 */
    EXPECT_EQ(schema_column_fixed_size(TYPE_BOOLEAN), 1u);  /* bool = 1 */
    EXPECT_EQ(schema_column_fixed_size(TYPE_VARCHAR), 0u);  /* variable */

    /* Verify schema's computed fixed_size = 8 (INTEGER) + 8 (FLOAT) + 1 (BOOLEAN) = 17 */
    EXPECT_EQ(schema.fixed_size, 17u);

    /* Verify variable columns count */
    EXPECT_EQ(schema.var_cols, 1u);

    /* Verify null bitmap size = ceil(4/8) = 1 */
    EXPECT_EQ(schema.null_bitmap_size, 1u);

    /* Verify fixed_offset for each column */
    EXPECT_EQ(schema.columns[0].fixed_offset, 0u);   /* id: first fixed */
    EXPECT_EQ(schema.columns[0].var_index, -1);
    EXPECT_EQ(schema.columns[1].var_index, 0);        /* name: first var */
    EXPECT_EQ(schema.columns[2].fixed_offset, 8u);    /* score: after id */
    EXPECT_EQ(schema.columns[2].var_index, -1);
    EXPECT_EQ(schema.columns[3].fixed_offset, 16u);   /* active: after score */
    EXPECT_EQ(schema.columns[3].var_index, -1);

    /* Verify max serialized tuple size */
    uint32_t max_ts = schema_max_tuple_size(&schema);
    /* null_bitmap(1) + fixed(17) + varchar(50 + 4) = 72 */
    EXPECT_EQ(max_ts, 72u);

    /* Find column by name */
    EXPECT_EQ(schema_find_column(&schema, "id"), 0);
    EXPECT_EQ(schema_find_column(&schema, "name"), 1);
    EXPECT_EQ(schema_find_column(&schema, "score"), 2);
    EXPECT_EQ(schema_find_column(&schema, "active"), 3);
    EXPECT_EQ(schema_find_column(&schema, "nonexistent"), -1);

    schema_destroy(&schema);
}

/* =====================================================================
 * TEST: tuple, serialize_deserialize
 * ===================================================================== */

TEST(tuple, serialize_deserialize) {
    /* Build a schema: id INTEGER, name VARCHAR(50), score FLOAT, active BOOLEAN */
    column_def_t cols[] = {
        { .name = "id",     .type = TYPE_INTEGER, .nullable = false, .max_length = 0 },
        { .name = "name",   .type = TYPE_VARCHAR, .nullable = true,  .max_length = 50 },
        { .name = "score",  .type = TYPE_FLOAT,   .nullable = true,  .max_length = 0 },
        { .name = "active", .type = TYPE_BOOLEAN,  .nullable = false, .max_length = 0 },
    };

    schema_t schema;
    int rc = schema_create(&schema, cols, 4);
    EXPECT_EQ(rc, DB_OK);

    /* Create a tuple and set values */
    tuple_t tuple;
    rc = tuple_create(&tuple, 4);
    EXPECT_EQ(rc, DB_OK);

    value_t v_id = value_make_integer(1001);
    value_t v_name = value_make_varchar("Alice");
    value_t v_score = value_make_float(95.5);
    value_t v_active = value_make_boolean(true);

    tuple_set_value(&tuple, 0, &v_id);
    tuple_set_value(&tuple, 1, &v_name);
    tuple_set_value(&tuple, 2, &v_score);
    tuple_set_value(&tuple, 3, &v_active);

    /* Serialize */
    uint32_t max_size = schema_max_tuple_size(&schema);
    char* buf = (char*)malloc(max_size);
    EXPECT_NOT_NULL(buf);

    uint32_t actual_size = 0;
    rc = tuple_serialize(&tuple, &schema, buf, &actual_size);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_TRUE(actual_size > 0);

    /* Deserialize into a new tuple */
    tuple_t tuple2;
    rc = tuple_deserialize(&tuple2, &schema, buf, actual_size);
    EXPECT_EQ(rc, DB_OK);

    /* Verify all values */
    const value_t* v;
    v = tuple_get_value(&tuple2, 0);
    EXPECT_NOT_NULL(v);
    EXPECT_EQ(v->type, TYPE_INTEGER);
    EXPECT_EQ(v->val.int_val, 1001);

    v = tuple_get_value(&tuple2, 1);
    EXPECT_NOT_NULL(v);
    EXPECT_EQ(v->type, TYPE_VARCHAR);
    EXPECT_STR_EQ(v->val.varchar, "Alice");

    v = tuple_get_value(&tuple2, 2);
    EXPECT_NOT_NULL(v);
    EXPECT_EQ(v->type, TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(v->val.float_val, 95.5);

    v = tuple_get_value(&tuple2, 3);
    EXPECT_NOT_NULL(v);
    EXPECT_EQ(v->type, TYPE_BOOLEAN);
    EXPECT_TRUE(v->val.bool_val);

    /* Test with a NULL value: set score to NULL */
    value_t v_null = value_make_null();
    tuple_set_value(&tuple, 2, &v_null);

    uint32_t actual_size2 = 0;
    rc = tuple_serialize(&tuple, &schema, buf, &actual_size2);
    EXPECT_EQ(rc, DB_OK);

    tuple_t tuple3;
    rc = tuple_deserialize(&tuple3, &schema, buf, actual_size2);
    EXPECT_EQ(rc, DB_OK);

    v = tuple_get_value(&tuple3, 0);
    EXPECT_EQ(v->type, TYPE_INTEGER);
    EXPECT_EQ(v->val.int_val, 1001);

    v = tuple_get_value(&tuple3, 2);
    EXPECT_EQ(v->type, TYPE_NULL);

    v = tuple_get_value(&tuple3, 3);
    EXPECT_EQ(v->type, TYPE_BOOLEAN);
    EXPECT_TRUE(v->val.bool_val);

    /* Clean up */
    tuple_destroy(&tuple);
    tuple_destroy(&tuple2);
    tuple_destroy(&tuple3);
    value_destroy(&v_name);
    free(buf);
    schema_destroy(&schema);
}

/* =====================================================================
 * TEST: free_space_manager, track_reuse
 * ===================================================================== */

TEST(free_space_manager, track_reuse) {
    free_space_manager_t fsm;
    int rc = fsm_init(&fsm);
    EXPECT_EQ(rc, DB_OK);

    /* Track free space for several pages */
    rc = fsm_update(&fsm, 1, 1000);
    EXPECT_EQ(rc, DB_OK);
    rc = fsm_update(&fsm, 2, 500);
    EXPECT_EQ(rc, DB_OK);
    rc = fsm_update(&fsm, 3, 200);
    EXPECT_EQ(rc, DB_OK);
    rc = fsm_update(&fsm, 4, 3000);
    EXPECT_EQ(rc, DB_OK);

    EXPECT_EQ(fsm_count(&fsm), 4u);

    /* Find page with at least 150 bytes — should be page 3 (best-fit: 200) */
    page_id_t pid = fsm_find_page(&fsm, 150);
    EXPECT_EQ(pid, 3);

    /* Find page with at least 600 bytes — should be page 1 (best-fit: 1000) */
    pid = fsm_find_page(&fsm, 600);
    EXPECT_EQ(pid, 1);

    /* Find page with at least 2500 bytes — should be page 4 (best-fit: 3000) */
    pid = fsm_find_page(&fsm, 2500);
    EXPECT_EQ(pid, 4);

    /* Find page with more space than any page has — should return INVALID_PAGE_ID */
    pid = fsm_find_page(&fsm, 5000);
    EXPECT_EQ(pid, INVALID_PAGE_ID);

    /* Update a page's free space */
    rc = fsm_update(&fsm, 3, 1500);
    EXPECT_EQ(rc, DB_OK);

    /* Now page 3 has 1500, but page 1 (1000) is still the best-fit for 600
     * because 1000 < 1500 (least free space that meets the requirement) */
    pid = fsm_find_page(&fsm, 600);
    EXPECT_EQ(pid, 1);

    /* Remove a page */
    rc = fsm_remove(&fsm, 2);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(fsm_count(&fsm), 3u);

    /* Verify page 2 is no longer tracked */
    pid = fsm_find_page(&fsm, 400);
    /* page 2 (500) was removed. Remaining: page 1 (1000), page 3 (1500), page 4 (3000).
     * Best-fit for 400 = page 1 (1000) since 1000 is least sufficient */
    EXPECT_EQ(pid, 1);

    /* Remove nonexistent page */
    rc = fsm_remove(&fsm, 99);
    EXPECT_NE(rc, DB_OK);

    fsm_destroy(&fsm);
}
