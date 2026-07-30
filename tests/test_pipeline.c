#include <stdio.h>

#include "gx5125/pipeline.h"

int main(void)
{
    if (gx5125_pipeline_selftest() != 0) {
        fputs("GOODIX_BETA_PIPELINE_SELFTEST=FAIL\n", stderr);
        return 1;
    }
    puts("GOODIX_BETA_PIPELINE_SELFTEST=PASS pure_linux:1 usb_accessed:0 "
         "preprocessor:1 extractor:1 deterministic_exact:1 finger_presence:1 "
         "synthetic_records:90 biometric_input_used:0 biometric_output_saved:0");
    return 0;
}
