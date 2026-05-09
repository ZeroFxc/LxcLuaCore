#include <stdio.h>
#include <stdlib.h>
int main() {
    system("gdb -batch -ex run -ex \"info locals\" -ex \"print node->op\" ./lxclua test_jit_closure.lua > info.txt 2>&1");
    return 0;
}
