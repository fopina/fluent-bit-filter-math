/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_filter.h>
#include <fluent-bit/flb_filter_plugin.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_kv.h>
#include <fluent-bit/flb_str.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_pack.h>
#include <msgpack.h>

#include "math.h"

#include <stdio.h>
#include <sys/types.h>


static void teardown(struct filter_math_ctx *ctx)
{
    struct mk_list *tmp;
    struct mk_list *head;

    struct filter_math_operand *operand;

    flb_free(ctx->output_field);

    mk_list_foreach_safe(head, tmp, &ctx->operands) {
        operand = mk_list_entry(head, struct filter_math_operand, _head);
        flb_free(operand->field);
        mk_list_del(&operand->_head);
        flb_free(operand);
    }

}

static int configure(struct filter_math_ctx *ctx,
                     struct flb_filter_instance *f_ins,
                     struct flb_config *config)
{
    struct mk_list *head;
    struct flb_kv *kv;
    struct filter_math_operand *operand;
    char *p;

    ctx->output_field = NULL;
    ctx->output_field_len = 0;
    ctx->operation = -1;

    mk_list_foreach(head, &f_ins->properties) {        
        kv = mk_list_entry(head, struct flb_kv, _head);

        if (strcasecmp(kv->key, "operation") == 0) {
            for (int i=0; i<NUMBER_OF_OPERATORS; i++) {
                if (strncasecmp(kv->val, OPERATION_STRING[i], 3) == 0) {
                    ctx->operation = i;
                    break;
                }
            }
            if (ctx->operation == -1) {
                flb_plg_error(ctx->ins, "Key \"operation\" has invalid value "
                              "'%s'. Expected 'sum', 'sub', 'mul' or 'div'\n",
                              kv->val);
                return -1;
            }
        }
        else if (strcasecmp(kv->key, "output_field") == 0) {
            ctx->output_field = flb_strndup(kv->val, flb_sds_len(kv->val));
            ctx->output_field_len = flb_sds_len(kv->val);
        }
        else if (strcasecmp(kv->key, "field") == 0) {
            operand = flb_malloc(sizeof(struct filter_math_operand));
            if (!operand) {
                flb_plg_error(ctx->ins, "Unable to allocate memory for "
                              "operand");
                flb_free(operand);
                return -1;
            }

            operand->field = flb_strndup(kv->val, flb_sds_len(kv->val));
            operand->field_len = flb_sds_len(kv->val);

            mk_list_add(&operand->_head, &ctx->operands);
            ctx->operands_cnt++;
        }
        else if (strcasecmp(kv->key, "constant") == 0) {
            operand = flb_malloc(sizeof(struct filter_math_operand));
            if (!operand) {
                flb_plg_error(ctx->ins, "Unable to allocate memory for "
                              "operand");
                flb_free(operand);
                return -1;
            }

            operand->constant = strtod(kv->val, &p);
            if (operand->constant == 0) {
                flb_plg_error(ctx->ins, "Constant should be an integer value (different than 0)");
                flb_free(operand);
                return -1;
            }
            mk_list_add(&operand->_head, &ctx->operands);
            ctx->operands_cnt++;
        }
        else {
            flb_plg_error(ctx->ins, "Invalid configuration key '%s'", kv->key);
            return -1;
        }
    }

    /* Sanity checks */
    if (!ctx->output_field) {
        flb_plg_error(ctx->ins, "Output_field is required or the operation is pointless");
        return -1;
    }

    if ((ctx->operation < 0) ||
            (ctx->operation >= NUMBER_OF_OPERATORS)) {
        flb_plg_error(ctx->ins, "Operation can only be: sum, sub, mul or div");
        return -1;
    }

    if (ctx->operands_cnt < 2) {
        flb_plg_error(ctx->ins, "Any operation requires at least 2 operands ('field' or 'constant')");
        return -1;
    }

    return 0;
}

static void helper_pack_string(msgpack_packer * packer, const char *str,
                               int len)
{
    if (str == NULL) {
        msgpack_pack_nil(packer);
    }
    else {
        msgpack_pack_str(packer, len);
        msgpack_pack_str_body(packer, str, len);
    }
}

static inline double find_operand_val(struct filter_math_operand *operand, msgpack_object * map,
                               struct filter_math_ctx *ctx)
{
    for (int i = 0; i < map->via.map.size; i++) {
        if ((map->via.map.ptr[i].key.type == MSGPACK_OBJECT_STR) && (operand->field_len == map->via.map.ptr[i].key.via.str.size) && (strncasecmp(operand->field, map->via.map.ptr[i].key.via.str.ptr, operand->field_len) == 0)) {
            switch(map->via.map.ptr[i].val.type) {
                case MSGPACK_OBJECT_POSITIVE_INTEGER:
                    return (double) map->via.map.ptr[i].val.via.u64;
                case MSGPACK_OBJECT_NEGATIVE_INTEGER:
                    return (double) map->via.map.ptr[i].val.via.i64;
                case MSGPACK_OBJECT_FLOAT32:
                case MSGPACK_OBJECT_FLOAT64:
                    return map->via.map.ptr[i].val.via.f64;

            }
            flb_plg_debug(ctx->ins, "invalid field type %s: %d", map->via.map.ptr[i].key.via.str.ptr, map->via.map.ptr[i].val.type);
        }
    }
    return 0;
}

static inline double map_operate_fn(msgpack_object * map,
                               struct filter_math_ctx *ctx,
                               void(*f) (double *result, double *val))
{
    int i;
    struct mk_list *tmp;
    struct mk_list *head;
    struct filter_math_operand *operand;
    bool first_elem = true;
    double result = 0;
    double val;

    mk_list_foreach_safe(head, tmp, &ctx->operands) {
        operand = mk_list_entry(head, struct filter_math_operand, _head);
        if (!operand->field) {
            val = (double)operand->constant;
        } else {
            val = find_operand_val(operand, map, ctx);
        }
        if (first_elem) {
            first_elem = false;
            result = val;
        } else {
            (*f)(&result, &val);
        }
    }

    return result;
}

static inline void apply_sum(double *result, double *val)
{
    *result += *val;
}

static inline void apply_sub(double *result, double *val)
{
    *result -= *val;
}

static inline void apply_mul(double *result, double *val)
{
    *result *= *val;
}

static inline void apply_div(double *result, double *val)
{
    *result /= *val;
}

static inline int apply_operation(msgpack_packer *packer,
                                      msgpack_object *root,
                                      struct filter_math_ctx *ctx,
                                      void(*f) (double *result, double *val))
{
    int i;
    msgpack_object ts = root->via.array.ptr[0];
    msgpack_object map = root->via.array.ptr[1];
    msgpack_object *key;

    if (map.type != MSGPACK_OBJECT_MAP) return 0;

    double output = map_operate_fn(&map, ctx, f);

    flb_plg_debug(ctx->ins, "Nest : Outer map size is %d, will be %d, nested "
                  "map size will be %d",
                  map.via.map.size, 1, output);

    /* Record array init(2) */
    msgpack_pack_array(packer, 2);

    /* Record array item 1/2 */
    msgpack_pack_object(packer, ts);

    /*
     * Record array item 2/2
     * Create a new map with toplevel items +1 for nested map
     */
    msgpack_pack_map(packer, map.via.map.size + 1);
    

    for (i = 0; i < map.via.map.size; i++) {
        key = &map.via.map.ptr[i].key;
        msgpack_pack_object(packer, *key);
        msgpack_pack_object(packer, map.via.map.ptr[i].val);
    }
    
    /* Pack the output key */
    helper_pack_string(packer, ctx->output_field, ctx->output_field_len);

    /* Pack the output value */
    msgpack_pack_double(packer, output);

    return 1;
}

static int cb_math_init(struct flb_filter_instance *f_ins,
                        struct flb_config *config, void *data)
{
    struct filter_math_ctx *ctx;

    ctx = flb_malloc(sizeof(struct filter_math_ctx));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    mk_list_init(&ctx->operands);
    ctx->ins = f_ins;
    ctx->operands_cnt = 0;

    if (configure(ctx, f_ins, config) < 0) {
        flb_free(ctx);
        return -1;
    }

    flb_filter_set_context(f_ins, ctx);
    return 0;
}

static int cb_math_filter(const void *data, size_t bytes,
                          const char *tag, int tag_len,
                          void **out_buf, size_t * out_size,
                          struct flb_filter_instance *f_ins,
                          void *context, struct flb_config *config)
{
    msgpack_unpacked result;
    size_t off = 0;
    (void) f_ins;
    (void) config;

    struct filter_math_ctx *ctx = context;
    int modified_records = 0;
    int total_modified_records = 0;

    msgpack_sbuffer buffer;
    msgpack_sbuffer_init(&buffer);

    msgpack_packer packer;
    msgpack_packer_init(&packer, &buffer, msgpack_sbuffer_write);

    /*
     * Records come in the format,
     *
     * [ TIMESTAMP, { K1=>V1, K2=>V2, ...} ],
     * [ TIMESTAMP, { K1=>V1, K2=>V2, ...} ]
     *
     * Example record,
     * [1123123, {"Mem.total"=>4050908, "Mem.used"=>476, "Mem.free"=>3574332 }]
     */

    msgpack_unpacked_init(&result);
    while (msgpack_unpack_next(&result, data, bytes, &off) == MSGPACK_UNPACK_SUCCESS) {
        modified_records = 0;
        if (result.data.type == MSGPACK_OBJECT_ARRAY) {
            if (ctx->operation == SUM) {
                modified_records =
                    apply_operation(&packer, &result.data, ctx, &apply_sum);
            }
            else if (ctx->operation == SUB) {
                modified_records =
                    apply_operation(&packer, &result.data, ctx, &apply_sub);
            }
            else if (ctx->operation == MUL) {
                modified_records =
                    apply_operation(&packer, &result.data, ctx, &apply_mul);
            }
            else {
                modified_records =
                    apply_operation(&packer, &result.data, ctx, &apply_div);
            }


            if (modified_records == 0) {
                // not matched, so copy original event.
                msgpack_pack_object(&packer, result.data);
            }
            total_modified_records += modified_records;
        }
        else {
            flb_plg_debug(ctx->ins, "Record is NOT an array, skipping");
            msgpack_pack_object(&packer, result.data);
        }
    }
    msgpack_unpacked_destroy(&result);

    *out_buf = buffer.data;
    *out_size = buffer.size;

    if (total_modified_records == 0) {
        msgpack_sbuffer_destroy(&buffer);
        return FLB_FILTER_NOTOUCH;
    }
    else {
        return FLB_FILTER_MODIFIED;
    }
}

static int cb_math_exit(void *data, struct flb_config *config)
{
    struct filter_math_ctx *ctx = data;

    teardown(ctx);
    flb_free(ctx);
    return 0;
}

struct flb_filter_plugin filter_math_plugin = {
    .name = "math",
    .description = "apply math operations on fields",
    .cb_init = cb_math_init,
    .cb_filter = cb_math_filter,
    .cb_exit = cb_math_exit,
    .flags = 0
};
