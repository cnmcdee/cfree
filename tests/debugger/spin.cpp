// Debugger test target: never exits on its own (kill/restart tests).
// Compile with: g++ -g -O0 -o spin spin.cpp
#include <cstdio>
int main() {
    volatile long n = 0;
    std::printf("SPINNING\n");
    std::fflush(stdout);
    while (1) {
        n++;
    }
    return 0;
}
