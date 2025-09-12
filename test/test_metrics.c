#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>

#include "hardstress.h"
#include "metrics.h"

// Forward declaration
void test_proc_stat_parsing();

// We can't have two main functions, so I will rename this to run_metrics_tests
// and call it from the original main in test_main.c
void run_metrics_tests() {
    printf("\nRunning metrics tests...\n");
    test_proc_stat_parsing();
}

void test_proc_stat_parsing() {
    printf("\n- Running test_proc_stat_parsing...\n");

    // Create a mock /proc/stat file
    char mock_proc_stat_path[] = "/tmp/mock_proc_stat_XXXXXX";
    int fd = mkstemp(mock_proc_stat_path);
    assert(fd != -1);

    const char *mock_data = "cpu 123 456 789 101112 0 0 0 0 0 0\n"
                            "cpu0 1000 10 200 8000 50 10 10 0 20 10\n";
    write(fd, mock_data, strlen(mock_data));
    close(fd);

    // --- Test read_proc_stat ---
    cpu_sample_t sample_a[1] = {0};
    int cpus_read = read_proc_stat(sample_a, 1, mock_proc_stat_path);
    assert(cpus_read == 1);
    printf("  - PASSED: read_proc_stat read 1 CPU.\n");

    assert(sample_a[0].user == 1000);
    assert(sample_a[0].nice == 10);
    assert(sample_a[0].system == 200);
    assert(sample_a[0].idle == 8000);
    assert(sample_a[0].iowait == 50);
    assert(sample_a[0].irq == 10);
    assert(sample_a[0].softirq == 10);
    assert(sample_a[0].steal == 0);
    assert(sample_a[0].guest == 20);
    assert(sample_a[0].guest_nice == 10);
    printf("  - PASSED: read_proc_stat correctly parsed all fields.\n");


    // --- Test compute_usage ---
    cpu_sample_t sample_b[1] = {0};
    sample_b[0].user = 1100;
    sample_b[0].nice = 10;
    sample_b[0].system = 250;
    sample_b[0].idle = 8100;
    sample_b[0].iowait = 50;
    sample_b[0].irq = 10;
    sample_b[0].softirq = 10;
    sample_b[0].steal = 0;
    sample_b[0].guest = 70;
    sample_b[0].guest_nice = 20;

    double usage = compute_usage(&sample_a[0], &sample_b[0]);
    double expected_usage = (double)(210.0 / 310.0);

    printf("  - Calculated usage: %f\n", usage);
    printf("  - Expected usage:   %f\n", expected_usage);

    // Compare with a small tolerance
    assert(fabs(usage - expected_usage) < 1e-9);
    printf("  - PASSED: compute_usage calculated correct percentage.\n");


    // Clean up the mock file
    unlink(mock_proc_stat_path);
}
