#include <stdio.h>

#include "gx5125/matcher.h"

int main(void)
{
    if (gx5125_matcher_selftest() != 0) {
        puts("GOODIX_BETA_MATCHER_SELFTEST=FAIL");
        return 1;
    }
    puts("GOODIX_BETA_MATCHER_SELFTEST=PASS pure_linux:1 "
         "alignment:rotation-translation descriptor_bits:160 descriptor_gate:56 top3_consensus:1 "
         "threshold_calibrated:0 biometric_input_used:0 "
         "biometric_output_saved:0");
    return 0;
}
