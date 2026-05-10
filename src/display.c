#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* ------------------------------------------------------------------ ANSI */

#define BOLD   "\033[1m"
#define BLUE   "\033[94m"
#define GREEN  "\033[32m"
#define RESET  "\033[0m"

/* ------------------------------------------------------------------ sparkline */

static const char *SPARKS[] = {
    "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"
};
#define N_SPARKS 8
#define STRETCH  4

static void build_spark(const double *vals, int n, char *out, size_t out_sz)
{
    if (n == 0) { out[0] = '\0'; return; }

    double lo =  DBL_MAX;
    double hi = -DBL_MAX;
    for (int i = 0; i < n; i++) {
        if (vals[i] < lo) lo = vals[i];
        if (vals[i] > hi) hi = vals[i];
    }
    double span = hi - lo;
    if (span == 0.0) span = 1.0;

    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        int idx = (int)((vals[i] - lo) / span * (N_SPARKS - 1));
        if (idx < 0) idx = 0;
        if (idx >= N_SPARKS) idx = N_SPARKS - 1;
        for (int s = 0; s < STRETCH; s++)
            strncat(out, SPARKS[idx], out_sz - strlen(out) - 1);
    }
}

/* ------------------------------------------------------------------ bar */

static void draw_bar(const Bucket *b, int n, const Total *total,
                     const char *title, const char *unit)
{
    double max_val = 0.0;
    for (int i = 0; i < n; i++) {
        if (b[i].up   > max_val) max_val = b[i].up;
        if (b[i].down > max_val) max_val = b[i].down;
    }

    int   width = 32;
    double scale = (max_val > width) ? max_val / width : 1.0;

    printf("\n  %s\n", title);
    printf("  ");
    for (int i = 0; i < (int)strlen(title) + 2; i++) putchar('-');
    printf("\n");
    printf("  " GREEN "█ Upload" RESET "  " BLUE "█ Download" RESET "\n\n");

    for (int i = 0; i < n; i++) {
        int up_len   = (int)(b[i].up   / scale);
        int down_len = (int)(b[i].down / scale);

        printf("%5s ", b[i].label);
        printf(GREEN);
        for (int j = 0; j < up_len;   j++) putchar('\xe2'), putchar('\x96'), putchar('\x88');
        printf(RESET BLUE);
        for (int j = 0; j < down_len; j++) putchar('\xe2'), putchar('\x96'), putchar('\x88');
        printf(RESET);
        printf(" %6.2f\xe2\x86\x91 %6.2f\xe2\x86\x93 %s\n", b[i].up, b[i].down, unit);
    }

    printf("\nTotal: " GREEN "%.2f\xe2\x86\x91" RESET
           "  " BLUE "%.2f\xe2\x86\x93" RESET " %s\n",
           total->up, total->down, unit);
}

/* ------------------------------------------------------------------ compact */

static void draw_compact(const Bucket *b, int n, const Total *total,
                         const char *title, const char *unit)
{
    printf("\n  %s\n", title);
    printf("  ");
    for (int i = 0; i < (int)strlen(title) + 2; i++) putchar('\xe2'), putchar('\x94'), putchar('\x80');
    printf("\n");

    for (int i = 0; i < n; i++) {
        printf("  %5s   " GREEN "\xe2\x86\x91" RESET " %7.2f   "
               BLUE "\xe2\x86\x93" RESET " %7.2f %s\n",
               b[i].label, b[i].up, b[i].down, unit);
    }

    printf("\n  Total: " GREEN "%.2f\xe2\x86\x91" RESET
           "  " BLUE "%.2f\xe2\x86\x93" RESET " %s\n",
           total->up, total->down, unit);
}

/* ------------------------------------------------------------------ spark */

static void draw_spark(const Bucket *b, int n, const Total *total,
                       const char *title, const char *unit)
{
    double *ups   = malloc(n * sizeof(double));
    double *downs = malloc(n * sizeof(double));
    if (!ups || !downs) { free(ups); free(downs); return; }

    int up_peak_i   = 0;
    int down_peak_i = 0;

    for (int i = 0; i < n; i++) {
        ups[i]   = b[i].up;
        downs[i] = b[i].down;
        if (b[i].up   > ups[up_peak_i])     up_peak_i   = i;
        if (b[i].down > downs[down_peak_i]) down_peak_i = i;
    }

    /* 4 bytes per spark char (UTF-8) * STRETCH * n + null */
    size_t spark_buf_sz = (size_t)(n * STRETCH * 4 + 16);
    char *up_spark   = calloc(1, spark_buf_sz);
    char *down_spark = calloc(1, spark_buf_sz);
    if (!up_spark || !down_spark) goto cleanup;

    build_spark(ups,   n, up_spark,   spark_buf_sz);
    build_spark(downs, n, down_spark, spark_buf_sz);

    /* tick row: show every ~6th label, strip ":00" suffix for hour labels */
    int tick_row_len = n * STRETCH + 8;
    char *tick_row = calloc(1, tick_row_len);
    if (!tick_row) goto cleanup;
    memset(tick_row, ' ', tick_row_len - 1);

    int step = n / 6;
    if (step < 1) step = 1;
    for (int i = 0; i < n; i += step) {
        char short_label[16];
        strncpy(short_label, b[i].label, sizeof(short_label) - 1);
        short_label[sizeof(short_label) - 1] = '\0';
        /* strip ":00" for hour labels */
        char *colon = strstr(short_label, ":00");
        if (colon) *colon = '\0';

        int pos = i * STRETCH;
        for (int j = 0; short_label[j] && pos + j < tick_row_len - 1; j++)
            tick_row[pos + j] = short_label[j];
    }
    /* rtrim */
    int tlen = strlen(tick_row);
    while (tlen > 0 && tick_row[tlen - 1] == ' ') tick_row[--tlen] = '\0';

    printf("\n  %s\n", title);
    printf("  ");
    for (int i = 0; i < (int)strlen(title) + 2; i++) putchar('\xe2'), putchar('\x94'), putchar('\x80');
    printf("\n");

    printf("  " GREEN "\xe2\x86\x91" RESET " %s  peak %s %.2f %s\n",
           up_spark,   b[up_peak_i].label,   ups[up_peak_i],   unit);
    printf("  " BLUE  "\xe2\x86\x93" RESET " %s  peak %s %.2f %s\n",
           down_spark, b[down_peak_i].label, downs[down_peak_i], unit);
    printf("    %s\n", tick_row);
    printf("\n  Total: " GREEN "%.2f\xe2\x86\x91" RESET
           "  " BLUE "%.2f\xe2\x86\x93" RESET " %s\n",
           total->up, total->down, unit);

    free(tick_row);
cleanup:
    free(up_spark);
    free(down_spark);
    free(ups);
    free(downs);
}

/* ------------------------------------------------------------------ public */

void display_render(const Bucket *buckets, int n, const Total *total,
                    const char *title, DisplayStyle style, int gb_show)
{
    const char *unit = gb_show ? "GB" : "MB";

    switch (style) {
        case STYLE_COMPACT: draw_compact(buckets, n, total, title, unit); break;
        case STYLE_SPARK:   draw_spark  (buckets, n, total, title, unit); break;
        case STYLE_BAR:
        default:            draw_bar    (buckets, n, total, title, unit); break;
    }
}
