#include "record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

/* ------------------------------------------------------------------ signal */

static volatile sig_atomic_t g_running = 1;

static void _sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ------------------------------------------------------------------ /proc/net/dev */

int net_read_counters(NetCounters *out)
{
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) {
        perror("rasd: cannot open /proc/net/dev");
        return -1;
    }

    out->bytes_sent = 0;
    out->bytes_recv = 0;

    char line[512];
    int  lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        if (lineno <= 2) continue; /* skip header lines */

        /* format: iface: recv_bytes ... send_bytes ...
         * field positions (0-indexed after the colon):
         *   0  = recv bytes
         *   8  = sent bytes
         */
        char *colon = strchr(line, ':');
        if (!colon) continue;

        char   iface[64];
        int64_t rb = 0, rp, re, rf, rr, rm, rro, rx;
        int64_t tb = 0, tp, te, tf, tc, tm, tca, tx;

        /* parse interface name */
        sscanf(line, " %63[^:]:", iface);

        /* skip loopback */
        if (strcmp(iface, "lo") == 0) continue;

        sscanf(colon + 1,
            "%lld %lld %lld %lld %lld %lld %lld %lld"
            "%lld %lld %lld %lld %lld %lld %lld %lld",
            &rb, &rp, &re, &rf, &rr, &rm, &rro, &rx,
            &tb, &tp, &te, &tf, &tc, &tm, &tca, &tx);

        out->bytes_recv += rb;
        out->bytes_sent += tb;
    }

    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ helpers */

static void fmt_timestamp(char *buf, size_t n)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", tm);
}

static void print_verbose(const NetCounters *cur, double dl_bytes, double ul_bytes, double wait)
{
    char ts[32];
    fmt_timestamp(ts, sizeof(ts));

    printf("-------- %s --------\n", ts);
    printf("Total Megabytes Received : %.0f MB\n", (double)cur->bytes_recv / (1024.0 * 1024.0));
    printf("Total Megabytes Sent     : %.0f MB\n", (double)cur->bytes_sent / (1024.0 * 1024.0));
    printf("Download                 : %.2f KB/s\n", (dl_bytes / wait) / 1024.0);
    printf("Upload                   : %.2f KB/s\n", (ul_bytes / wait) / 1024.0);
    printf("-------------------------------------\n");
}

static void print_json(const NetCounters *cur, double dl_bytes, double ul_bytes, double wait)
{
    char ts[32];
    fmt_timestamp(ts, sizeof(ts));

    printf("{\n");
    printf("   \"timestamp\": \"%s\",\n", ts);
    printf("   \"total_bytes_sent\": %lld,\n", (long long)cur->bytes_sent);
    printf("   \"total_bytes_sent_mb\": %lld,\n", (long long)(cur->bytes_sent / (1024*1024)));
    printf("   \"total_bytes_recv\": %lld,\n", (long long)cur->bytes_recv);
    printf("   \"total_bytes_recv_mb\": %lld,\n", (long long)(cur->bytes_recv / (1024*1024)));
    printf("   \"download_kbps\": %.2f,\n", (dl_bytes / wait) / 1024.0);
    printf("   \"upload_kbps\": %.2f\n", (ul_bytes / wait) / 1024.0);
    printf("}\n");
}

/* ------------------------------------------------------------------ loop */

void record_loop(RasdDB *db, const RecordOpts *opts)
{
    /* register signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = _sig_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    double wait = opts->wait > 0.0 ? opts->wait : 3.0;

    NetCounters old, cur;
    if (net_read_counters(&old) != 0) return;

    int   batch_count  = 0;
    const int BATCH_SZ = 15;

    /* begin a transaction for batched inserts */
    if (!opts->dry_run)
        sqlite3_exec(db->handle, "BEGIN;", NULL, NULL, NULL);

    while (g_running) {
        /* sleep in 100ms increments so we can catch signals promptly */
        long total_ms = (long)(wait * 1000);
        long slept    = 0;
        while (g_running && slept < total_ms) {
            struct timespec ts = { 0, 100 * 1000 * 1000 }; /* 100ms */
            nanosleep(&ts, NULL);
            slept += 100;
        }
        if (!g_running) break;

        if (net_read_counters(&cur) != 0) continue;

        /* detect counter reset (reboot / interface change) */
        if (cur.bytes_recv < old.bytes_recv || cur.bytes_sent < old.bytes_sent) {
            old = cur;
            continue;
        }

        double dl = (double)(cur.bytes_recv - old.bytes_recv);
        double ul = (double)(cur.bytes_sent - old.bytes_sent);

        if (opts->json)
            print_json(&cur, dl, ul, wait);
        else if (opts->verbose)
            print_verbose(&cur, dl, ul, wait);

        if (!opts->dry_run) {
            db_insert(db, (int64_t)time(NULL), cur.bytes_sent, cur.bytes_recv);
            batch_count++;

            if (batch_count >= BATCH_SZ) {
                sqlite3_exec(db->handle, "COMMIT;", NULL, NULL, NULL);
                sqlite3_exec(db->handle, "BEGIN;",  NULL, NULL, NULL);
                batch_count = 0;
            }
        }

        old = cur;
    }

    /* flush remaining */
    if (!opts->dry_run)
        sqlite3_exec(db->handle, "COMMIT;", NULL, NULL, NULL);
}
