// Deliberately writes one element past a heap allocation.
// Built only in the AddressSanitizer preset; the test expects the sanitizer report.
#include <cstdio>

int main() {
    volatile int index = 8;
    int* values = new int[8];
    values[index] = 1;
    std::printf("no sanitizer report, wrote %d\n", values[index]);
    delete[] values;
    return 0;
}
