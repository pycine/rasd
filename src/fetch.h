#ifndef RASD_FETCH_H
#define RASD_FETCH_H

#include "db.h"
#include "display.h"

typedef enum {
    PERIOD_TODAY     = 0,
    PERIOD_YESTERDAY = 1,
    PERIOD_THISWEEK  = 2,
    PERIOD_MONTH     = 3,
    PERIOD_SINCE     = 4,
    PERIOD_DATE      = 5,
} Period;

typedef struct {
    Period      period;
    int         month;          /* 1-12, for PERIOD_MONTH */
    char        since[32];      /* "YYYY-MM-DD", for PERIOD_SINCE */
    char        date[32];       /* "YYYY-MM-DD", for PERIOD_DATE */
    int         gb_show;
    int         json_output;
    DisplayStyle style;
} FetchOpts;

void fetch_usage(RasdDB *db, const FetchOpts *opts);

#endif /* RASD_FETCH_H */
