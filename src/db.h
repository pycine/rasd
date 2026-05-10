#ifndef RASD_DB_H
#define RASD_DB_H

#include <stdint.h>
#include <sqlite3.h>

#define RASD_DIR        ".rasd"
#define RASD_DB_FILE    ".rasd/db.sqlite"

typedef struct {
    sqlite3    *handle;
    char        path[512];
} RasdDB;

/* lifecycle */
int  db_open(RasdDB *db);
void db_close(RasdDB *db);

/* write */
int  db_insert(RasdDB *db, int64_t timestamp, int64_t bytes_sent, int64_t bytes_recv);
int  db_commit(RasdDB *db);

/* read */
typedef struct {
    int64_t timestamp;
    int64_t bytes_sent;
    int64_t bytes_recv;
} RasdRow;

/* caller must free() the returned array */
RasdRow *db_query_range(RasdDB *db, int64_t t_start, int64_t t_end, int *out_count);

/* stats */
typedef struct {
    int64_t record_count;
    double  size_kb;
    int64_t oldest_ts;
    int64_t newest_ts;
    int64_t most_traffic_ts;
    int64_t least_traffic_ts;
} RasdDBStats;

int  db_stats(RasdDB *db, RasdDBStats *out);
int  db_prune(RasdDB *db, int days);

#endif /* RASD_DB_H */
