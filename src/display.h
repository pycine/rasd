#ifndef RASD_DISPLAY_H
#define RASD_DISPLAY_H

#include <stddef.h>

/* one bucket: a label + up/down values in MB or GB */
typedef struct {
    char   label[16];
    double up;
    double down;
} Bucket;

typedef struct {
    double up;
    double down;
} Total;

typedef enum {
    STYLE_BAR     = 0,
    STYLE_COMPACT = 1,
    STYLE_SPARK   = 2,
} DisplayStyle;

void display_render(const Bucket *buckets, int n, const Total *total,
                    const char *title, DisplayStyle style, int gb_show);

#endif /* RASD_DISPLAY_H */
