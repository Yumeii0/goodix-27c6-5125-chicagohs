#include <stdio.h>
#include "gx5125/extractor.h"
int main(void) {
    int rc = gx5125_extractor_selftest();
    if (rc != 0) {
        fputs("GOODIX_BETA_EXTRACTOR_SELFTEST=FAIL\n", stderr);
        return 1;
    }
    puts("GOODIX_BETA_EXTRACTOR_SELFTEST=PASS pure_linux:1 wine:0 dll:0");
    return 0;
}
