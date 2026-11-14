#ifndef GATEWAY_HISTORY_H
#define GATEWAY_HISTORY_H
/* Internal SQLite API: called exclusively by the storage thread. */
#include "storage.h"
#include <sqlite3.h>
#include "query.h"
typedef struct {
 sqlite3 *db;
 sqlite3_stmt *frame, *point, *event, *diagnostic;
 char id[33];
} History;
typedef struct { uint64_t frames, points, events, runs, diagnostics; } Pruned;
int history_open(History *h, const char *path, const char *utc, double start,
                 const char *mode, const char *dir);
int history_batch(History *h, const StoredRecord *items, size_t count,
                  const StoredRecord *diagnostics, size_t diagnostic_count,
                  const StorageStats *after);
int history_prune(History *h, const char *cutoff, Pruned *pruned);
int history_finish(History *h, int code, int sig, int complete, const StorageStats *stats);
int history_close(History *h);
void history_query(History *h, const Query *q, QueryResult *result);
#ifdef STORAGE_TESTING
/* Linked only into gateway-fault/test builds, never the production gateway. */
void storage_test_vfs_init(void);
int storage_test_before_batch(sqlite3 *db);
double storage_test_retention_interval(void);
#endif
#endif
