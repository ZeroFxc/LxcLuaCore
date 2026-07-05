#include "ljit_ir.h"
#include "../../../core/lopcodes.h"
#include "../core/ljit_debug.h"
#include <stdlib.h>
#include <string.h>

/* 根据 PC 查找所属的 BB */
static ljit_bb_t *find_bb_by_pc(ljit_bb_t *head, int pc) {
    ljit_bb_t *bb = head;
    while (bb) {
        if (pc >= bb->start_pc && pc <= bb->end_pc) return bb;
        bb = bb->next;
    }
    return NULL;
}

/* 向 BB 添加后继边，自动去重 + 动态扩容（初始 4，翻倍） */
static void bb_add_succ(ljit_bb_t *bb, ljit_bb_t *succ) {
    if (!bb || !succ) return;
    /* 去重 */
    for (int i = 0; i < bb->succ_count; i++) {
        if (bb->succs[i] == succ) return;
    }
    /* 扩容 */
    if (bb->succ_count >= bb->succ_cap) {
        bb->succ_cap = bb->succ_cap ? bb->succ_cap * 2 : 4;
        bb->succs = (ljit_bb_t **)realloc(bb->succs, bb->succ_cap * sizeof(ljit_bb_t *));
    }
    bb->succs[bb->succ_count++] = succ;
}

/* 向 BB 添加前驱边，自动去重 + 动态扩容（初始 4，翻倍） */
static void bb_add_pred(ljit_bb_t *bb, ljit_bb_t *pred) {
    if (!bb || !pred) return;
    /* 去重 */
    for (int i = 0; i < bb->pred_count; i++) {
        if (bb->preds[i] == pred) return;
    }
    /* 扩容 */
    if (bb->pred_count >= bb->pred_cap) {
        bb->pred_cap = bb->pred_cap ? bb->pred_cap * 2 : 4;
        bb->preds = (ljit_bb_t **)realloc(bb->preds, bb->pred_cap * sizeof(ljit_bb_t *));
    }
    bb->preds[bb->pred_count++] = pred;
}

/* 判断指定 opcode 是否为条件跳转（跳过下一条指令） */
static int is_cond_jump(OpCode op) {
    switch (op) {
        case OP_EQ: case OP_LT: case OP_LE:
        case OP_EQK: case OP_EQI:
        case OP_LTI: case OP_LEI: case OP_GTI: case OP_GEI:
        case OP_TEST: case OP_TESTSET:
        case OP_IS: case OP_TESTNIL:
        case OP_INSTANCEOF:
            return 1;
        default:
            return 0;
    }
}

/* 判断指定 opcode 是否为无条件跳转 */
static int is_uncond_jump(OpCode op) {
    return op == OP_JMP;
}

/* 判断指定 opcode 是否为循环跳转（FORPREP/FORLOOP/TFORPREP/TFORLOOP） */
static int is_loop_branch(OpCode op) {
    switch (op) {
        case OP_FORPREP: case OP_FORLOOP:
        case OP_TFORPREP: case OP_TFORLOOP:
            return 1;
        default:
            return 0;
    }
}

/* 判断指定 opcode 是否为终止指令（返回/尾调用） */
static int is_terminal(OpCode op) {
    switch (op) {
        case OP_RETURN: case OP_RETURN0: case OP_RETURN1:
        case OP_TAILCALL:
            return 1;
        default:
            return 0;
    }
}

ljit_bb_t *ljit_ir_bb_build(Proto *proto) {
    if (!proto || proto->sizecode == 0) return NULL;

    JIT_DBG(MOD_IR_BB, "build basic blocks: sizecode=%d", proto->sizecode);

    /* 分配 leader 标记数组 */
    char *is_leader = (char *)calloc(proto->sizecode, sizeof(char));
    if (!is_leader) return NULL;

    /* 第一条指令始终是 leader */
    is_leader[0] = 1;

    /* 扫描所有指令，标记 leader */
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
                /* 条件跳转：跳过下一条指令（通常是 JMP） */
                if (pc + 1 < proto->sizecode) {
                    is_leader[pc + 1] = 1; /* JMP 指令 */
                }
                /* 检查下一条是否为 JMP，标记其目标 */
                if (pc + 1 < proto->sizecode && GET_OPCODE(proto->code[pc + 1]) == OP_JMP) {
                    int dest = pc + 1 + 1 + GETARG_sJ(proto->code[pc + 1]);
                    if (dest >= 0 && dest < proto->sizecode) {
                        is_leader[dest] = 1;
                    }
                    /* 标记 PC+2 为 leader（条件为真时的 true 分支） */
                    if (pc + 2 < proto->sizecode) {
                        is_leader[pc + 2] = 1;
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

    /* 根据 leader 标记构建 BB 链表 */
    ljit_bb_t *head = NULL;
    ljit_bb_t *tail = NULL;
    int current_start = 0;

    for (int pc = 1; pc <= proto->sizecode; pc++) {
        if (pc == proto->sizecode || is_leader[pc]) {
            ljit_bb_t *bb = (ljit_bb_t *)calloc(1, sizeof(ljit_bb_t));
            if (!bb) {
                /* 分配失败，释放已分配的链表 */
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
            bb->bb_id = 0;
            bb->preds = NULL;
            bb->pred_count = 0;
            bb->pred_cap = 0;
            bb->succs = NULL;
            bb->succ_count = 0;
            bb->succ_cap = 0;

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

    /* 统计 BB 数量并分配 bb_id */
    int bb_count = 0;
    ljit_bb_t *tmp = head;
    while (tmp) {
        tmp->bb_id = bb_count++;
        tmp = tmp->next;
    }
    JIT_DBG(MOD_IR_BB, "built %d basic blocks, head=%p", bb_count, head);

    /* ========== CFG 构建 ========== */
    JIT_DBG(MOD_IR_BB, "building CFG edges...");

    for (ljit_bb_t *bb = head; bb; bb = bb->next) {
        int last_pc = bb->end_pc;
        Instruction last_i = proto->code[last_pc];
        OpCode last_op = GET_OPCODE(last_i);

        if (is_uncond_jump(last_op)) {
            /* 无条件跳转：后继是跳转目标 BB */
            int dest = last_pc + 1 + GETARG_sJ(last_i);
            ljit_bb_t *target = find_bb_by_pc(head, dest);
            if (target) {
                bb_add_succ(bb, target);
                bb_add_pred(target, bb);
            }
        }
        else if (is_cond_jump(last_op)) {
            /* 条件跳转：后继是跳转目标 BB（true 分支）和 fall-through BB（false 分支） */
            /* false 分支：下一个 BB（包含 JMP 指令） */
            ljit_bb_t *next_bb = bb->next;
            if (next_bb) {
                bb_add_succ(bb, next_bb);
                bb_add_pred(next_bb, bb);
            }

            /* true 分支：条件为真时跳过 JMP，进入 PC+2 */
            if (last_pc + 1 < proto->sizecode &&
                GET_OPCODE(proto->code[last_pc + 1]) == OP_JMP) {
                /* PC+2 是 true 分支入口 */
                if (last_pc + 2 < proto->sizecode) {
                    ljit_bb_t *true_bb = find_bb_by_pc(head, last_pc + 2);
                    if (true_bb && true_bb != next_bb) {
                        bb_add_succ(bb, true_bb);
                        bb_add_pred(true_bb, bb);
                    }
                }
                /* JMP 目标也是后继（false 分支最终到达汇合点） */
                int jmp_dest = last_pc + 1 + 1 + GETARG_sJ(proto->code[last_pc + 1]);
                ljit_bb_t *jmp_target = find_bb_by_pc(head, jmp_dest);
                if (jmp_target && jmp_target != next_bb) {
                    bb_add_succ(bb, jmp_target);
                    bb_add_pred(jmp_target, bb);
                }
            }
            else if (next_bb) {
                /* 条件跳转后没有 JMP（如 for 循环的条件判断），仅 fall-through */
                bb_add_succ(bb, next_bb);
                bb_add_pred(next_bb, bb);
            }
        }
        else if (is_loop_branch(last_op)) {
            /* 循环跳转（FORPREP/FORLOOP/TFORPREP/TFORLOOP）：目标和 fall-through 都是后继 */
            ljit_bb_t *next_bb = bb->next;
            int dest;
            if (last_op == OP_FORPREP || last_op == OP_TFORPREP) {
                dest = last_pc + 1 + GETARG_Bx(last_i);
            } else {
                /* FORLOOP / TFORLOOP */
                dest = last_pc + 1 - GETARG_Bx(last_i);
            }
            ljit_bb_t *target = find_bb_by_pc(head, dest);
            if (target) {
                bb_add_succ(bb, target);
                bb_add_pred(target, bb);
            }
            if (next_bb && next_bb != target) {
                bb_add_succ(bb, next_bb);
                bb_add_pred(next_bb, bb);
            }
        }
        else if (is_terminal(last_op)) {
            /* 返回/尾调用：无后继 */
        }
        else {
            /* 其他指令：后继是下一个 BB */
            ljit_bb_t *next_bb = bb->next;
            if (next_bb) {
                bb_add_succ(bb, next_bb);
                bb_add_pred(next_bb, bb);
            }
        }
    }

    /* 打印 CFG 调试信息 */
    JIT_DBG(MOD_IR_BB, "CFG dump:");
    for (ljit_bb_t *bb = head; bb; bb = bb->next) {
        char pred_buf[256] = {0};
        char succ_buf[256] = {0};
        int pred_off = 0, succ_off = 0;

        for (int i = 0; i < bb->pred_count; i++) {
            pred_off += snprintf(pred_buf + pred_off, sizeof(pred_buf) - pred_off,
                "%sBB%d", (i > 0 ? "," : ""), bb->preds[i]->bb_id);
        }
        if (bb->pred_count == 0) {
            snprintf(pred_buf, sizeof(pred_buf), "(none)");
        }

        for (int i = 0; i < bb->succ_count; i++) {
            succ_off += snprintf(succ_buf + succ_off, sizeof(succ_buf) - succ_off,
                "%sBB%d", (i > 0 ? "," : ""), bb->succs[i]->bb_id);
        }
        if (bb->succ_count == 0) {
            snprintf(succ_buf, sizeof(succ_buf), "(none)");
        }

        JIT_DBG(MOD_IR_BB, "  BB%d: [%d,%d] preds=[%s] succs=[%s]",
            bb->bb_id, bb->start_pc, bb->end_pc, pred_buf, succ_buf);
    }

    return head;
}