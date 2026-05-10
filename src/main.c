#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#include "db.h"
#include "record.h"
#include "fetch.h"
#include "display.h"

#define VERSION "1.0.0"
#define BOLD    "\033[1m"
#define RESET   "\033[0m"

/* ------------------------------------------------------------------ usage */

static void usage_main(void)
{
    printf(BOLD "rasd" RESET " v" VERSION " — network usage monitor\n\n");
    printf("Usage:\n");
    printf("  rasd record [options]       Start recording bandwidth\n");
    printf("  rasd db [options]           Database operations\n");
    printf("  rasd [query options]        Show usage (default: today)\n\n");
    printf("Query options:\n");
    printf("  --today                     Today's usage (default)\n");
    printf("  --yesterday                 Yesterday's usage\n");
    printf("  --thisweek                  This week's usage\n");
    printf("  --month [N]                 Month 1-12 (default: current)\n");
    printf("  --date YYYY-MM-DD           Specific date\n");
    printf("  --since YYYY-MM-DD          From date until now\n");
    printf("  -g, --gigabyte              Show in GB\n");
    printf("  --style bar|compact|spark   Graph style (default: bar)\n");
    printf("  --json                      JSON output\n");
    printf("  -V, --version               Print version\n\n");
    printf("Examples:\n");
    printf("  rasd record -w 5 -v\n");
    printf("  rasd --today --style spark\n");
    printf("  rasd --since 2026-01-01 -g\n");
    printf("  rasd db --stats\n");
}

static void usage_record(void)
{
    printf("Usage: rasd record [options]\n\n");
    printf("  -w, --wait N     Sampling interval in seconds (default: 3)\n");
    printf("  -d, --dry-run    Don't write to database\n");
    printf("  -v, --verbose    Print live stats\n");
    printf("      --json       Output as JSON\n");
}

static void usage_db(void)
{
    printf("Usage: rasd db [options]\n\n");
    printf("  --stats          Show database info\n");
    printf("  --prune N        Delete records older than N days\n");
    printf("  --json           JSON output\n");
}

/* ------------------------------------------------------------------ db stats display */

static void fmt_ts(char *buf, size_t n, int64_t ts)
{
    time_t t = (time_t)ts;
    struct tm *tm = localtime(&t);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", tm);
}

static void print_db_stats(const RasdDBStats *s, const char *db_path, int json)
{
    char oldest[32], newest[32], most[32], least[32];
    fmt_ts(oldest, sizeof(oldest), s->oldest_ts);
    fmt_ts(newest, sizeof(newest), s->newest_ts);
    fmt_ts(most,   sizeof(most),   s->most_traffic_ts);
    fmt_ts(least,  sizeof(least),  s->least_traffic_ts);

    /* date-only for most/least */
    most[10]  = '\0';
    least[10] = '\0';

    if (json) {
        printf("{\n");
        printf("   \"records\": %lld,\n",       (long long)s->record_count);
        printf("   \"size_kb\": %.1f,\n",        s->size_kb);
        printf("   \"oldest_record\": \"%s\",\n", oldest);
        printf("   \"newest_record\": \"%s\",\n", newest);
        printf("   \"most_traffic\": \"%s\",\n",  most);
        printf("   \"least_traffic\": \"%s\"\n",  least);
        printf("}\n");
    } else {
        printf("\n  " BOLD "Database info" RESET "\n\n");
        printf("  %-18s %lld\n",  "Records",       (long long)s->record_count);
        printf("  %-18s %.1f KB  (%s)\n", "Size",  s->size_kb, db_path);
        printf("  %-18s %s\n",   "Oldest record",  oldest);
        printf("  %-18s %s\n",   "Newest record",  newest);
        printf("  %-18s %s\n",   "Most Traffic",   most);
        printf("  %-18s %s\n",   "Least Traffic",  least);
        printf("\n");
    }
}

/* ------------------------------------------------------------------ confirm prompt */

static int confirm_prune(int days)
{
    time_t cutoff = time(NULL) - (int64_t)days * 86400;
    char buf[32];
    struct tm *tm = localtime(&cutoff);
    strftime(buf, sizeof(buf), "%Y %d %b", tm);
    printf("Records before %s will be pruned. Continue? [y/N] ", buf);
    fflush(stdout);
    char ans[8] = {0};
    if (!fgets(ans, sizeof(ans), stdin)) return 0;
    return (ans[0] == 'y' || ans[0] == 'Y');
}

/* ------------------------------------------------------------------ subcommand: record */

static int cmd_record(int argc, char **argv)
{
    RecordOpts opts = { .wait = 3.0 };

    static struct option lo[] = {
        { "wait",    required_argument, NULL, 'w' },
        { "dry-run", no_argument,       NULL, 'd' },
        { "verbose", no_argument,       NULL, 'v' },
        { "json",    no_argument,       NULL, 'j' },
        { "help",    no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "w:dvjh", lo, NULL)) != -1) {
        switch (c) {
            case 'w': opts.wait    = atof(optarg); break;
            case 'd': opts.dry_run = 1;            break;
            case 'v': opts.verbose = 1;            break;
            case 'j': opts.json    = 1;            break;
            case 'h': usage_record(); return 0;
            default:  usage_record(); return 1;
        }
    }

    RasdDB db;
    if (db_open(&db) != 0) return 1;

    record_loop(&db, &opts);

    db_close(&db);
    return 0;
}

/* ------------------------------------------------------------------ subcommand: db */

static int cmd_db(int argc, char **argv)
{
    int do_stats = 0, do_prune = 0, prune_days = 0, json = 0;

    static struct option lo[] = {
        { "stats", no_argument,       NULL, 's' },
        { "prune", required_argument, NULL, 'p' },
        { "json",  no_argument,       NULL, 'j' },
        { "help",  no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "sp:jh", lo, NULL)) != -1) {
        switch (c) {
            case 's': do_stats  = 1;           break;
            case 'p': do_prune  = 1; prune_days = atoi(optarg); break;
            case 'j': json      = 1;           break;
            case 'h': usage_db(); return 0;
            default:  usage_db(); return 1;
        }
    }

    RasdDB db;
    if (db_open(&db) != 0) return 1;

    if (do_prune) {
        if (prune_days <= 0) {
            fprintf(stderr, "rasd: --prune requires a positive number of days\n");
            db_close(&db);
            return 1;
        }
        if (confirm_prune(prune_days))
            db_prune(&db, prune_days);
        else
            printf("Aborted.\n");
    } else {
        RasdDBStats stats;
        if (db_stats(&db, &stats) == 0)
            print_db_stats(&stats, db.path, json);
    }

    db_close(&db);
    return 0;
}

/* ------------------------------------------------------------------ subcommand: query (default) */

static DisplayStyle parse_style(const char *s)
{
    if (strcmp(s, "compact") == 0) return STYLE_COMPACT;
    if (strcmp(s, "spark")   == 0) return STYLE_SPARK;
    return STYLE_BAR;
}

static int cmd_query(int argc, char **argv)
{
    FetchOpts opts = {
        .period     = PERIOD_TODAY,
        .style      = STYLE_BAR,
        .gb_show    = 0,
        .json_output = 0,
    };

    int period_set = 0;

    static struct option lo[] = {
        { "today",     no_argument,       NULL,  1  },
        { "yesterday", no_argument,       NULL,  2  },
        { "thisweek",  no_argument,       NULL,  3  },
        { "month",     optional_argument, NULL,  4  },
        { "date",      required_argument, NULL,  5  },
        { "since",     required_argument, NULL,  6  },
        { "gigabyte",  no_argument,       NULL, 'g' },
        { "style",     required_argument, NULL,  7  },
        { "json",      no_argument,       NULL, 'j' },
        { "version",   no_argument,       NULL, 'V' },
        { "help",      no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "gjVh", lo, NULL)) != -1) {
        switch (c) {
            case 1: opts.period = PERIOD_TODAY;     period_set = 1; break;
            case 2: opts.period = PERIOD_YESTERDAY; period_set = 1; break;
            case 3: opts.period = PERIOD_THISWEEK;  period_set = 1; break;
            case 4:
                opts.period = PERIOD_MONTH;
                period_set  = 1;
                if (optarg) {
                    opts.month = atoi(optarg);
                } else {
                    /* default: current month */
                    time_t now = time(NULL);
                    opts.month = localtime(&now)->tm_mon + 1;
                }
                break;
            case 5:
                opts.period = PERIOD_DATE;
                period_set  = 1;
                strncpy(opts.date, optarg, sizeof(opts.date) - 1);
                break;
            case 6:
                opts.period = PERIOD_SINCE;
                period_set  = 1;
                strncpy(opts.since, optarg, sizeof(opts.since) - 1);
                break;
            case 'g': opts.gb_show     = 1;               break;
            case  7:  opts.style       = parse_style(optarg); break;
            case 'j': opts.json_output = 1;               break;
            case 'V':
                printf("rasd v" VERSION "\n");
                return 0;
            case 'h': usage_main(); return 0;
            default:  usage_main(); return 1;
        }
    }

    (void)period_set; /* default is already PERIOD_TODAY */

    RasdDB db;
    if (db_open(&db) != 0) return 1;

    fetch_usage(&db, &opts);

    db_close(&db);
    printf("\n");
    return 0;
}

/* ------------------------------------------------------------------ entry point */

int main(int argc, char **argv)
{
    if (argc < 2) {
        /* default: show today */
        FetchOpts opts = {
            .period      = PERIOD_TODAY,
            .style       = STYLE_BAR,
            .gb_show     = 0,
            .json_output = 0,
        };
        RasdDB db;
        if (db_open(&db) != 0) return 1;
        fetch_usage(&db, &opts);
        db_close(&db);
        printf("\n");
        return 0;
    }

    if (strcmp(argv[1], "record") == 0) {
        return cmd_record(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "db") == 0) {
        return cmd_db(argc - 1, argv + 1);
    }

    /* query mode — pass full argv so getopt sees the flags */
    return cmd_query(argc, argv);
}
