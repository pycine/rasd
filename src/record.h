#ifndef RASD_RECORD_H
#define RASD_RECORD_H

#include "db.h"
#include <stdint.h>

typedef struct {
    int64_t bytes_sent;
    int64_t bytes_recv;
} NetCounters;

typedef struct {
    double  wait;       /* sampling interval in seconds */
    int     dry_run;    /* don't write to DB */
    int     verbose;    /* print live stats */
    int     json;       /* output as JSON */
} RecordOpts;

/* read total bytes from /proc/net/dev (all interfaces summed) */
int  net_read_counters(NetCounters *out);

/* main recording loop — blocks until SIGTERM/SIGINT */
void record_loop(RasdDB *db, const RecordOpts *opts);

#endif /* RASD_RECORD_H */
