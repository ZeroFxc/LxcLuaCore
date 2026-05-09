#include <stdio.h>
#include <stdlib.h>
int main() {
    system("gdb -batch -ex run -ex bt ./lxclua test_jit_closure.lua > bt.txt 2>&1");
    return 0;
}
