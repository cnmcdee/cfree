// Debugger test target: console C++ program.
// Markers (// MARK:xxx) are found by the harness to get line numbers, so
// edits here do not break the tests. Compile with: g++ -g -O0 -o console console.cpp
#include <cstdio>
#include <vector>

int helper(int v) {
    int r = v * 2 + 1;  // MARK:HELPER
    return r;
}

int compute(int a, int b) {
    int s = a + b;       // MARK:COMPUTE
    int d = helper(s);   // MARK:COMPUTE_CALL
    return d;
}

int main(int argc, char** argv) {
    if (argc > 1) std::printf("ARG=%s\n", argv[1]);  // MARK:ARG
    int sum = 0;
    std::vector<int> vals = {1, 2, 3};
    for (int i = 0; i < 3; i++) {
        int x = helper(i);  // MARK:LOOP
        sum += x;           // MARK:LOOP_NEXT
        std::printf("i=%d x=%d sum=%d\n", i, x, sum);
        std::fflush(stdout);  // line-buffered for the debugger's live view
    }
    int total = compute(10, 20);  // MARK:CALL
    std::printf("TOTAL total=%d sum=%d\n", total, sum);  // MARK:AFTER_CALL
    std::printf("DONE sum=%d\n", sum);
    return 0;
}
