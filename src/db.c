#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

/* ------------------------------------------------------------------ helpers */

static int _mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int _exec(sqlite3 *h, const char *sql)
{
    char *err = NULL;
    int rc = sqlite3_exec(h, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "rasd: SQL error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ lifecycle */

int db_open(RasdDB *db)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    snprintf(db->path, sizeof(db->path), "%s/%s", home, RASD_DB_FILE);

    /* ensure directory exists */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s", home, RASD_DIR);
    if (_mkdir_p(dir) != 0) {
        fprintf(stderr, "rasd: cannot create directory %s: %s\n", dir, strerror(errno));
        return -1;
    }

    if (sqlite3_open(db->path, &db->handle) != SQLITE_OK) {
        fprintf(stderr, "rasd: cannot open database %s: %s\n",
                db->path, sqlite3_errmsg(db->handle));
        return -1;
    }

    /* performance pragmas */
    _exec(db->handle, "PRAGMA journal_mode=WAL;");
    _exec(db->handle, "PRAGMA synchronous=NORMAL;");

    /* schema */
    const char *schema =
        "CREATE TABLE IF NOT EXISTS data_usage ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp  INTEGER NOT NULL,"
        "  bytes_sent INTEGER NOT NULL,"
        "  bytes_recv INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_timestamp ON data_usage(timestamp);";

    if (_exec(db->handle, schema) != 0) return -1;

    return 0;
}

void db_close(RasdDB *db)
{
    if (db->handle) {
        sqlite3_exec(db->handle, "PRAGMA wal_checkpoint(TRUNCATE);", NULL, NULL, NULL);
        sqlite3_close(db->handle);
        db->handle = NULL;
    }
}

/* ------------------------------------------------------------------ write */

int db_insert(RasdDB *db, int64_t timestamp, int64_t bytes_sent, int64_t bytes_recv)
{
    const char *sql =
        "INSERT INTO data_usage (timestamp, bytes_sent, bytes_recv) VALUES (?, ?, ?);";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "rasd: prepare failed: %s\n", sqlite3_errmsg(db->handle));
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, timestamp);
    sqlite3_bind_int64(stmt, 2, bytes_sent);
    sqlite3_bind_int64(stmt, 3, bytes_recv);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_commit(RasdDB *db)
{
    /* WAL mode auto-commits, but we batch with explicit transactions */
    _exec(db->handle, "BEGIN;");
    /* caller already inserted rows; just commit */
    return _exec(db->handle, "COMMIT;");
}

/* ------------------------------------------------------------------ read */

RasdRow *db_query_range(RasdDB *db, int64_t t_start, int64_t t_end, int *out_count)
{
    const char *sql =
        "SELECT timestamp, bytes_sent, bytes_recv "
        "FROM data_usage "
        "WHERE timestamp BETWEEN ? AND ? "
        "ORDER BY timestamp ASC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "rasd: prepare failed: %s\n", sqlite3_errmsg(db->handle));
        *out_count = 0;
        return NULL;
    }

    sqlite3_bind_int64(stmt, 1, t_start);
    sqlite3_bind_int64(stmt, 2, t_end);

    /* dynamic array */
    int capacity = 1024;
    int count    = 0;
    RasdRow *rows = malloc(capacity * sizeof(RasdRow));
    if (!rows) { sqlite3_finalize(stmt); *out_count = 0; return NULL; }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            RasdRow *tmp = realloc(rows, capacity * sizeof(RasdRow));
            if (!tmp) { free(rows); sqlite3_finalize(stmt); *out_count = 0; return NULL; }
            rows = tmp;
        }
        rows[count].timestamp  = sqlite3_column_int64(stmt, 0);
        rows[count].bytes_sent = sqlite3_column_int64(stmt, 1);
        rows[count].bytes_recv = sqlite3_column_int64(stmt, 2);
        count++;
    }

    sqlite3_finalize(stmt);
    *out_count = count;
    return rows;
}

/* ------------------------------------------------------------------ stats */

int db_stats(RasdDB *db, RasdDBStats *out)
{
    memset(out, 0, sizeof(*out));

    sqlite3_stmt *stmt;

    /* record count */
    if (sqlite3_prepare_v2(db->handle,
            "SELECT COUNT(*) FROM data_usage;", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            out->record_count = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }

    /* oldest / newest */
    if (sqlite3_prepare_v2(db->handle,
            "SELECT MIN(timestamp), MAX(timestamp) FROM data_usage;",
            -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out->oldest_ts = sqlite3_column_int64(stmt, 0);
            out->newest_ts = sqlite3_column_int64(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    /* most traffic timestamp */
    if (sqlite3_prepare_v2(db->handle,
            "SELECT timestamp FROM data_usage "
            "GROUP BY timestamp "
            "ORDER BY SUM(bytes_sent + bytes_recv) DESC LIMIT 1;",
            -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            out->most_traffic_ts = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }

    /* least traffic timestamp */
    if (sqlite3_prepare_v2(db->handle,
            "SELECT timestamp FROM data_usage "
            "GROUP BY timestamp "
            "ORDER BY SUM(bytes_sent + bytes_recv) ASC LIMIT 1;",
            -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            out->least_traffic_ts = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }

    /* file size */
    struct stat st;
    if (stat(db->path, &st) == 0)
        out->size_kb = (double)st.st_size / 1024.0;

    return 0;
}

int db_prune(RasdDB *db, int days)
{
    time_t cutoff = time(NULL) - (int64_t)days * 86400;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->handle,
            "DELETE FROM data_usage WHERE timestamp < ?;",
            -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "rasd: prepare failed: %s\n", sqlite3_errmsg(db->handle));
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, (int64_t)cutoff);
    sqlite3_step(stmt);
    int deleted = sqlite3_changes(db->handle);
    sqlite3_finalize(stmt);

    _exec(db->handle, "VACUUM;");

    printf("Pruned %d records older than %d days.\n", deleted, days);
    return 0;
}
