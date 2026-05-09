#include <stdlib.h>
#include <stdio.h>
int main() {
    system("ulimit -c unlimited; ./lxclua test_jit_closure.lua");
    return 0;
}
