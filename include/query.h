#ifndef GATEWAY_QUERY_H
#define GATEWAY_QUERY_H
#include "storage.h"
#define QUERY_CAPACITY 8
#define QUERY_RESULT_MAX (1600 * 1024)
typedef struct {
 uint64_t id;
 double deadline;
 char from[21], until[21], after_utc[21], after_run[33];
 int64_t after_rx;
 unsigned limit;
} Query;
typedef struct { uint64_t id; char *json; size_t size; int code; } QueryResult;
int query_utc_valid(const char *s);
int query_parse(const char *line, Query *q);
int diagnostic_record_json(FILE *f, const StoredRecord *r, const char *run);
int record_json(FILE *f, const StoredRecord *r, const char *run);
int storage_query(Storage *s, const Query *q);
int storage_query_fd(Storage *s);
int storage_query_result(Storage *s, QueryResult *r);
#endif
