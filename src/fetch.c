#include "fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ helpers */

static double to_mb(int64_t bytes) { return (double)bytes / (1024.0 * 1024.0); }
static double to_gb(int64_t bytes) { return (double)bytes / (1024.0 * 1024.0 * 1024.0); }

static const char *WEEKDAYS[] = { "Mon","Tue","Wed","Thu","Fri","Sat","Sun" };

static int parse_date(const char *s, struct tm *out)
{
    memset(out, 0, sizeof(*out));
    if (sscanf(s, "%d-%d-%d", &out->tm_year, &out->tm_mon, &out->tm_mday) != 3)
        return -1;
    out->tm_year -= 1900;
    out->tm_mon  -= 1;
    out->tm_isdst = -1;
    return 0;
}

/* ------------------------------------------------------------------ bucket map (simple sorted array) */

#define MAX_BUCKETS 400

typedef struct {
    char   key[16];
    double up;
    double down;
} BucketEntry;

static BucketEntry g_bkt[MAX_BUCKETS];
static int         g_nbkt = 0;

static BucketEntry *bucket_find_or_create(const char *key)
{
    for (int i = 0; i < g_nbkt; i++)
        if (strcmp(g_bkt[i].key, key) == 0)
            return &g_bkt[i];

    if (g_nbkt >= MAX_BUCKETS) return NULL;
    BucketEntry *e = &g_bkt[g_nbkt++];
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->key[sizeof(e->key) - 1] = '\0';
    e->up    = 0.0;
    e->down  = 0.0;
    return e;
}

static int cmp_by_key(const void *a, const void *b)
{
    return strcmp(((const BucketEntry *)a)->key, ((const BucketEntry *)b)->key);
}

static void sort_weekday(void)
{
    /* reorder by Mon..Sun */
    BucketEntry sorted[7];
    int n = 0;
    for (int d = 0; d < 7; d++) {
        for (int i = 0; i < g_nbkt; i++) {
            if (strcmp(g_bkt[i].key, WEEKDAYS[d]) == 0) {
                sorted[n++] = g_bkt[i];
                break;
            }
        }
    }
    memcpy(g_bkt, sorted, n * sizeof(BucketEntry));
    g_nbkt = n;
}

static void sort_numeric_key(void)
{
    /* sort by integer value of key (day-of-month) */
    for (int i = 0; i < g_nbkt - 1; i++) {
        for (int j = i + 1; j < g_nbkt; j++) {
            if (atoi(g_bkt[i].key) > atoi(g_bkt[j].key)) {
                BucketEntry tmp = g_bkt[i];
                g_bkt[i] = g_bkt[j];
                g_bkt[j] = tmp;
            }
        }
    }
}

/* ------------------------------------------------------------------ JSON output */

static void print_json(const char *title)
{
    printf("{\n");
    for (int i = 0; i < g_nbkt; i++) {
        printf("   \"%s\": { \"up\": %.4f, \"down\": %.4f }",
               g_bkt[i].key, g_bkt[i].up, g_bkt[i].down);
        printf(",\n");
    }
    /* compute total */
    double tu = 0, td = 0;
    for (int i = 0; i < g_nbkt; i++) { tu += g_bkt[i].up; td += g_bkt[i].down; }
    printf("   \"total\": { \"up\": %.4f, \"down\": %.4f }\n", tu, td);
    printf("}\n");
    (void)title;
}

/* ------------------------------------------------------------------ time range builders */

static void time_range_today(time_t *t_start, time_t *t_end)
{
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    *t_start = mktime(&tm);
    *t_end   = now + 1;
}

static void time_range_yesterday(time_t *t_start, time_t *t_end)
{
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    tm.tm_mday--;
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    *t_start = mktime(&tm);
    *t_end   = *t_start + 86400;
}

static void time_range_thisweek(time_t *t_start, time_t *t_end)
{
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    /* Monday = 0 in our scheme; tm_wday: Sun=0 Mon=1 ... */
    int wday = (tm.tm_wday + 6) % 7; /* 0=Mon */
    tm.tm_mday -= wday;
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    *t_start = mktime(&tm);
    *t_end   = *t_start + 7 * 86400;
}

static void time_range_month(int month, time_t *t_start, time_t *t_end)
{
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    int year = tm.tm_year;
    if (month > tm.tm_mon + 1) year--; /* previous year */

    tm.tm_year = year;
    tm.tm_mon  = month - 1;
    tm.tm_mday = 1;
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    *t_start = mktime(&tm);

    tm.tm_mon++;
    if (tm.tm_mon > 11) { tm.tm_mon = 0; tm.tm_year++; }
    *t_end = mktime(&tm);
}

static int time_range_since(const char *since_str, time_t *t_start, time_t *t_end)
{
    struct tm tm;
    if (parse_date(since_str, &tm) != 0) {
        fprintf(stderr, "rasd: invalid date format, use YYYY-MM-DD\n");
        return -1;
    }
    *t_start = mktime(&tm);
    *t_end   = time(NULL) + 1;
    return 0;
}

static int time_range_date(const char *date_str, time_t *t_start, time_t *t_end)
{
    struct tm tm;
    if (parse_date(date_str, &tm) != 0) {
        fprintf(stderr, "rasd: invalid date format, use YYYY-MM-DD\n");
        return -1;
    }
    *t_start = mktime(&tm);
    *t_end   = *t_start + 86400;
    return 0;
}

/* ------------------------------------------------------------------ main fetch */

void fetch_usage(RasdDB *db, const FetchOpts *opts)
{
    time_t t_start = 0, t_end = 0;
    char   title[64] = "";

    switch (opts->period) {
        case PERIOD_TODAY:
            time_range_today(&t_start, &t_end);
            snprintf(title, sizeof(title), "Today Usage");
            break;
        case PERIOD_YESTERDAY:
            time_range_yesterday(&t_start, &t_end);
            snprintf(title, sizeof(title), "Yesterday");
            break;
        case PERIOD_THISWEEK:
            time_range_thisweek(&t_start, &t_end);
            snprintf(title, sizeof(title), "This Week Usage");
            break;
        case PERIOD_MONTH:
            time_range_month(opts->month, &t_start, &t_end);
            snprintf(title, sizeof(title), "Month %d Usage", opts->month);
            break;
        case PERIOD_SINCE:
            if (time_range_since(opts->since, &t_start, &t_end) != 0) return;
            snprintf(title, sizeof(title), "Since %s", opts->since);
            break;
        case PERIOD_DATE:
            if (time_range_date(opts->date, &t_start, &t_end) != 0) return;
            snprintf(title, sizeof(title), "%s", opts->date);
            break;
    }

    int      nrows = 0;
    RasdRow *rows  = db_query_range(db, (int64_t)t_start, (int64_t)t_end, &nrows);

    if (!rows || nrows < 2) {
        printf("Not enough data.\n");
        free(rows);
        return;
    }

    /* reset bucket map */
    memset(g_bkt, 0, sizeof(g_bkt));
    g_nbkt = 0;

    double total_up   = 0.0;
    double total_down = 0.0;

    double (*convert)(int64_t) = opts->gb_show ? to_gb : to_mb;

    for (int i = 1; i < nrows; i++) {
        int64_t up   = rows[i].bytes_sent - rows[i-1].bytes_sent;
        int64_t down = rows[i].bytes_recv - rows[i-1].bytes_recv;
        if (up   < 0) up   = 0;
        if (down < 0) down = 0;

        struct tm tm = *localtime((time_t *)&rows[i].timestamp);
        char key[16];

        switch (opts->period) {
            case PERIOD_TODAY:
            case PERIOD_YESTERDAY:
            case PERIOD_DATE:
                snprintf(key, sizeof(key), "%02d:00", tm.tm_hour);
                break;
            case PERIOD_THISWEEK:
                snprintf(key, sizeof(key), "%s", WEEKDAYS[(tm.tm_wday + 6) % 7]);
                break;
            case PERIOD_MONTH:
                snprintf(key, sizeof(key), "%d", tm.tm_mday);
                break;
            case PERIOD_SINCE:
                strftime(key, sizeof(key), "%Y-%m-%d", &tm);
                break;
        }

        BucketEntry *b = bucket_find_or_create(key);
        if (b) {
            b->up   += convert(up);
            b->down += convert(down);
        }

        total_up   += convert(up);
        total_down += convert(down);
    }

    free(rows);

    if (g_nbkt == 0) {
        printf("No data for this period.\n");
        return;
    }

    /* sort buckets */
    switch (opts->period) {
        case PERIOD_TODAY:
        case PERIOD_YESTERDAY:
        case PERIOD_DATE:
        case PERIOD_SINCE:
            qsort(g_bkt, g_nbkt, sizeof(BucketEntry), cmp_by_key);
            break;
        case PERIOD_THISWEEK:
            sort_weekday();
            break;
        case PERIOD_MONTH:
            sort_numeric_key();
            break;
    }

    if (opts->json_output) {
        print_json(title);
        return;
    }

    /* build Bucket array for display */
    Bucket *display_bkts = malloc(g_nbkt * sizeof(Bucket));
    if (!display_bkts) return;

    for (int i = 0; i < g_nbkt; i++) {
        strncpy(display_bkts[i].label, g_bkt[i].key, sizeof(display_bkts[i].label) - 1);
        display_bkts[i].label[sizeof(display_bkts[i].label) - 1] = '\0';
        display_bkts[i].up   = g_bkt[i].up;
        display_bkts[i].down = g_bkt[i].down;
    }

    Total total = { .up = total_up, .down = total_down };
    display_render(display_bkts, g_nbkt, &total, title, opts->style, opts->gb_show);

    free(display_bkts);
}
