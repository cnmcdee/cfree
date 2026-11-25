// FDesigner code-window test target: known locals in two scopes.
// Compile with: gcc -g -O0 -o prog prog.c   (from the work dir, so the
// DWARF paths match the FDESIGNER_DEBUG_BREAK locations used by
// tests/code_window_tests.sh). Line numbers are load-bearing:
//   main:   a=42 at line 14, msg="hello" at line 15
//   helper: x=42, y=3.5, z=44 at line 11 (return)
#include <stdio.h>
static int helper(int x) {
    double y = 3.5;
    int z = x + 2;
    return z;
}
int main(void) {
    int a = 42;
    const char *msg = "hello";
    int r = helper(a);
    printf("%d %s\n", r, msg);
    return 0;
}
