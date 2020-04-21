/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019-2020 The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef FLB_FILTER_MATH_H
#define FLB_FILTER_MATH_H

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_filter.h>

#define FOREACH_OPERATION(OPERATION) \
        OPERATION(SUM)   \
        OPERATION(SUB)   \
        OPERATION(MUL)   \
        OPERATION(DIV)   \

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

enum FILTER_MATH_OPERATION {
    FOREACH_OPERATION(GENERATE_ENUM)
};
// update with last enum (if that changes)
const int NUMBER_OF_OPERATORS = DIV + 1;

static const char *OPERATION_STRING[] = {
    FOREACH_OPERATION(GENERATE_STRING)
};

struct filter_math_ctx
{
    enum FILTER_MATH_OPERATION operation;
    char *output_field;
    int output_field_len;
    struct mk_list operands;
    int operands_cnt;
    struct flb_filter_instance *ins;
};

struct filter_math_operand
{
    char *field;
    int field_len;
    int constant;
    struct mk_list _head;
};

#endif
