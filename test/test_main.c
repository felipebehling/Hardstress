#include <stdio.h>
#include <assert.h>
#include "hardstress.h"
#include "metrics.h"
#include "utils.h"

#include "metrics.c"

// Forward declaration of test functions
void test_detect_cpu_count();
void test_get_total_system_memory();
void test_now_sec();
void test_splitmix64();
void test_shuffle32();
void test_csv_logging_bug();

int main() {
    printf("Running tests...\n");

    test_detect_cpu_count();
    test_get_total_system_memory();
    test_now_sec();
    test_splitmix64();
    test_shuffle32();
    test_csv_logging_bug();

    printf("\nAll tests passed!\n");
    return 0;
}

void test_detect_cpu_count() {
    printf("\n- Running test_detect_cpu_count...\n");
    int cpu_count = detect_cpu_count();
    printf("  - Detected %d CPU(s)\n", cpu_count);
    assert(cpu_count > 0);
    printf("  - PASSED: cpu_count is greater than 0.\n");
}

void test_get_total_system_memory() {
    printf("\n- Running test_get_total_system_memory...\n");
    unsigned long long total_mem = get_total_system_memory();
    printf("  - Detected %llu MB of total system memory\n", total_mem / (1024 * 1024));
    assert(total_mem > 0);
    printf("  - PASSED: total_mem is greater than 0.\n");
}

void test_now_sec() {
    printf("\n- Running test_now_sec...\n");
    double time1 = now_sec();
    printf("  - Initial time: %f\n", time1);

    // Sleep for a short duration
#ifdef _WIN32
    Sleep(10); // 10 milliseconds
#else
    usleep(10000); // 10000 microseconds = 10 milliseconds
#endif

    double time2 = now_sec();
    printf("  - Time after delay: %f\n", time2);

    assert(time2 > time1);
    printf("  - PASSED: now_sec is monotonic and increasing.\n");
}

void test_splitmix64() {
    printf("\n- Running test_splitmix64...\n");
    uint64_t seed = 12345;
    uint64_t val1 = splitmix64(&seed);
    uint64_t val2 = splitmix64(&seed);
    printf("  - Generated values: %llu, %llu\n", (unsigned long long)val1, (unsigned long long)val2);
    assert(val1 != val2);
    printf("  - PASSED: Subsequent values are different.\n");

    seed = 12345; // Reset seed
    uint64_t val3 = splitmix64(&seed);
    assert(val1 == val3);
    printf("  - PASSED: Same seed produces the same value.\n");
}

void test_shuffle32() {
    printf("\n- Running test_shuffle32...\n");
    uint32_t arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint32_t arr_copy[10];
    memcpy(arr_copy, arr, sizeof(arr));

    uint64_t seed = 67890;
    shuffle32(arr, 10, &seed);

    int different = 0;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != arr_copy[i]) {
            different = 1;
            break;
        }
    }
    assert(different == 1);
    printf("  - PASSED: Array is shuffled.\n");

    // Check if the shuffled array contains all original elements
    int found_count = 0;
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            if (arr_copy[i] == arr[j]) {
                found_count++;
                break;
            }
        }
    }
    assert(found_count == 10);
    printf("  - PASSED: Shuffled array contains all original elements.\n");
}

void test_csv_logging_bug() {
    printf("\n- Running test_csv_logging_bug...\n");

    AppContext app;
    memset(&app, 0, sizeof(AppContext));

    app.threads = 2;
    app.cpu_count = 2;
    app.history_len = 10;
    app.history_pos = 1; // Sampler has just advanced to this position

    // Allocate and initialize thread history
    app.thread_history = calloc(app.threads, sizeof(unsigned*));
    for (int i = 0; i < app.threads; i++) {
        app.thread_history[i] = calloc(app.history_len, sizeof(unsigned));
    }

    // Previous position (0) should have some data
    app.thread_history[0][0] = 100;
    app.thread_history[1][0] = 200;

    // Current position (1) is zeroed out by the sampler
    app.thread_history[0][1] = 0;
    app.thread_history[1][1] = 0;

    // Dummy CPU usage data
    app.cpu_usage = calloc(app.cpu_count, sizeof(double));
    app.cpu_usage[0] = 0.5;
    app.cpu_usage[1] = 0.6;
    app.temp_celsius = 50.0;

    // Open a temporary file to capture the log output
    char tmp_filename[] = "temp_test_log.csv";
    app.csv_log_file = fopen(tmp_filename, "w");
    assert(app.csv_log_file != NULL);

    // Call the function to be tested
    log_csv_sample(&app);

    fclose(app.csv_log_file);

    // Read the file and check the content
    FILE *f = fopen(tmp_filename, "r");
    assert(f != NULL);

    char line[512];
    assert(fgets(line, sizeof(line), f) != NULL);

    fclose(f);
    remove(tmp_filename);

    // With the fix, the implementation now logs the value at the previous position.
    // We expect to see ",100,200," for the thread iterations.
    printf("  - Logged line: %s\n", line);
    assert(strstr(line, ",100,200,") != NULL);

    printf("  - PASSED: Fix confirmed, logged iteration counts are correct.\n");

    // Free allocated memory
    for (int i = 0; i < app.threads; i++) {
        free(app.thread_history[i]);
    }
    free(app.thread_history);
    free(app.cpu_usage);
}
