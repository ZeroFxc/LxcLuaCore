#include "ljit_ir.h"
#include "../../../core/lopcodes.h"
#include <stdlib.h>
#include <string.h>

ljit_bb_t *ljit_ir_bb_build(Proto *proto) {
    if (!proto || proto->sizecode == 0) return NULL;

    // Allocate array to mark basic block leaders
    char *is_leader = (char *)calloc(proto->sizecode, sizeof(char));
    if (!is_leader) return NULL;

    // The first instruction is always a leader
    is_leader[0] = 1;

    // Find leaders
    for (int pc = 0; pc < proto->sizecode; pc++) {
        Instruction i = proto->code[pc];
        OpCode op = GET_OPCODE(i);

        switch (op) {
            case OP_JMP: {
                int dest = pc + 1 + GETARG_sJ(i);
                if (dest >= 0 && dest < proto->sizecode) {
                    is_leader[dest] = 1;
                }
                if (pc + 1 < proto->sizecode) {
                    is_leader[pc + 1] = 1;
                }
                break;
            }
            case OP_EQ:
            case OP_LT:
            case OP_LE:
            case OP_EQK:
            case OP_EQI:
            case OP_LTI:
            case OP_LEI:
            case OP_GTI:
            case OP_GEI:
            case OP_TEST:
            case OP_TESTSET:
            case OP_IS:
            case OP_TESTNIL:
            case OP_INSTANCEOF: {
                // Conditional jumps skip the next instruction (which is usually a JMP)
                // So target of conditional jump is pc+2 or wherever the next jump points
                if (pc + 1 < proto->sizecode) {
                    is_leader[pc + 1] = 1; // The instruction after conditional jump
                }
                // Check if next instruction is JMP to mark its target
                if (pc + 1 < proto->sizecode && GET_OPCODE(proto->code[pc + 1]) == OP_JMP) {
                    int dest = pc + 1 + 1 + GETARG_sJ(proto->code[pc + 1]);
                    if (dest >= 0 && dest < proto->sizecode) {
                        is_leader[dest] = 1;
                    }
                }
                break;
            }
            case OP_FORPREP:
            case OP_TFORPREP: {
                int dest = pc + 1 + GETARG_Bx(i);
                if (dest >= 0 && dest < proto->sizecode) {
                    is_leader[dest] = 1;
                }
                if (pc + 1 < proto->sizecode) {
                    is_leader[pc + 1] = 1;
                }
                break;
            }
            case OP_FORLOOP:
            case OP_TFORLOOP: {
                int dest = pc + 1 - GETARG_Bx(i);
                if (dest >= 0 && dest < proto->sizecode) {
                    is_leader[dest] = 1;
                }
                if (pc + 1 < proto->sizecode) {
                    is_leader[pc + 1] = 1;
                }
                break;
            }
            case OP_TFORCALL: {
                if (pc + 1 < proto->sizecode) {
                    is_leader[pc + 1] = 1;
                }
                break;
            }
            case OP_RETURN:
            case OP_RETURN0:
            case OP_RETURN1:
            case OP_TAILCALL: {
                if (pc + 1 < proto->sizecode) {
                    is_leader[pc + 1] = 1;
                }
                break;
            }
            default:
                break;
        }
    }

    // Construct basic blocks list based on leaders
    ljit_bb_t *head = NULL;
    ljit_bb_t *tail = NULL;
    int current_start = 0;

    for (int pc = 1; pc <= proto->sizecode; pc++) {
        if (pc == proto->sizecode || is_leader[pc]) {
            ljit_bb_t *bb = (ljit_bb_t *)malloc(sizeof(ljit_bb_t));
            if (!bb) {
                // Handle allocation failure: free list and return NULL
                ljit_bb_t *curr = head;
                while (curr) {
                    ljit_bb_t *next = curr->next;
                    free(curr);
                    curr = next;
                }
                free(is_leader);
                return NULL;
            }
            bb->start_pc = current_start;
            bb->end_pc = pc - 1;
            bb->next = NULL;

            if (!head) {
                head = bb;
                tail = bb;
            } else {
                tail->next = bb;
                tail = bb;
            }
            current_start = pc;
        }
    }

    free(is_leader);
    return head;
}
