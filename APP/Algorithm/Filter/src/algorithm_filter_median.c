#include "algorithm_filter_median.h"

#include <math.h>
#include <stddef.h>

bool Algorithm_Filter_Median3(
    float a, float b, float c, float *output)
{
    float median;

    if ((output == NULL) || !isfinite(a) || !isfinite(b) || !isfinite(c)) {
        return false;
    }
    if (a > b) {
        median = a;
        a = b;
        b = median;
    }
    if (b > c) {
        median = b;
        b = c;
        c = median;
    }
    if (a > b) {
        b = a;
    }
    *output = b;
    return true;
}
