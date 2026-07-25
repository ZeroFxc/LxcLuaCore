#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"
#include "../core/ljit_debug.h"
#include "../frontend/ljit_analyze.h"
#include "../../../core/lobject.h"
#include <math.h>

/* ========================================================================
 * 性能计数器: 统计各 codegen 路径命中次数
 * ======================================================================== */
int ljit_stat_int_fastpath = 0;       /* INT_FASTPATH: 编译时已知整数 */
int ljit_stat_guarded_fastpath = 0;   /* GUARDED_INT/NUM_FASTPATH: 运行时守卫 */
int ljit_stat_num_fastpath = 0;       /* NUM_FASTPATH: 编译时已知浮点 */
int ljit_stat_generic = 0;            /* GENERIC: C回退 */
int ljit_stat_cmp_inline = 0;        /* CMP_INLINE: 整数常量比较内联 */

/* ========================================================================
 * C 辅助函数 (从 JIT 代码调用，处理无法内联的通用情况)
 * ======================================================================== */

/**
 * @brief 二元算术运算通用 C 回退: ADD/SUB/MUL/DIV/IDIV/MOD
 * @details 调用 luaO_arith 完成运算，自动处理类型转换和元方法
 * @param L Lua 状态
 * @param ra 目标 TValue 指针
 * @param rb 左操作数 TValue 指针
 * @param rc 右操作数 TValue 指针
 */
void SLJIT_FUNC ljit_icall_arith_add(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPADD, rb, rc, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_arith_sub(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPSUB, rb, rc, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_arith_mul(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPMUL, rb, rc, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_arith_div(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPDIV, rb, rc, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_arith_idiv(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPIDIV, rb, rc, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_arith_mod(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPMOD, rb, rc, cast(StkId, ra));
}

/**
 * @brief 位运算通用 C 回退: BAND/BOR/BXOR/SHL/SHR
 * @details 调用 luaO_arith 完成位运算，自动处理类型转换和元方法
 */
void SLJIT_FUNC ljit_icall_arith_band(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPBAND, rb, rc, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_arith_bor(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPBOR, rb, rc, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_arith_bxor(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPBXOR, rb, rc, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_arith_shl(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPSHL, rb, rc, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_arith_shr(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    luaO_arith(L, LUA_OPSHR, rb, rc, cast(StkId, ra));
}

/**
 * @brief 一元运算通用 C 回退: UNM/BNOT
 * @details 调用 luaO_arith 完成一元运算（将源操作数同时作为 p1/p2 传入）
 */
void SLJIT_FUNC ljit_icall_unm(lua_State *L, TValue *ra, TValue *rb) {
    luaO_arith(L, LUA_OPUNM, rb, rb, cast(StkId, ra));
}
void SLJIT_FUNC ljit_icall_bnot(lua_State *L, TValue *ra, TValue *rb) {
    luaO_arith(L, LUA_OPBNOT, rb, rb, cast(StkId, ra));
}

/**
 * @brief 布尔取反通用 C 回退: NOT
 * @details 实现 Lua not 语义: nil/false -> true, 其他 -> false
 */
void SLJIT_FUNC ljit_icall_not(lua_State *L, TValue *ra, TValue *rb) {
    (void)L;
    if (l_isfalse(rb))
        setbtvalue(ra);
    else
        setbfvalue(ra);
}

/* ========================================================================
 * 代码生成辅助函数
 * ======================================================================== */

/**
 * @brief 获取 IR 值的推断类型
 * @param ctx JIT 编译上下文
 * @param val IR 值指针
 * @return 推断的类型 ljit_type_t
 */
static ljit_type_t ljit_cg_get_val_type(ljit_ctx_t *ctx, ljit_ir_val_t *val) {
    ljit_analyze_info_t *ainfo = (ljit_analyze_info_t *)ctx->analyze_info;
    if (val->type == IR_VAL_INT)
        return JIT_TYPE_INT;
    if (val->type == IR_VAL_NUM)
        return JIT_TYPE_NUM;
    if (val->type == IR_VAL_REG) {
        if (ainfo && val->v.reg >= 0 && val->v.reg < ainfo->max_regs)
            return ainfo->reg_types[val->v.reg];
        return JIT_TYPE_ANY;
    }
    /* IR_VAL_CONST, IR_VAL_UPVAL 等保守返回 ANY */
    return JIT_TYPE_ANY;
}

/**
 * @brief 更新虚拟寄存器的推断类型（在生成代码时产生更精确类型后调用）
 * @param ctx JIT 编译上下文
 * @param reg 虚拟寄存器编号
 * @param type 新的类型
 */
static void ljit_cg_update_reg_type(ljit_ctx_t *ctx, int reg, ljit_type_t type) {
    ljit_analyze_info_t *ainfo = (ljit_analyze_info_t *)ctx->analyze_info;
    if (ainfo && reg >= 0 && reg < ainfo->max_regs) {
        ainfo->reg_types[reg] = type;
    }
}

/**
 * @brief 在 tmp_reg 中生成 TValue 地址
 * @param compiler SLJIT 编译器
 * @param ctx JIT 编译上下文
 * @param tmp_reg 用于存放结果地址的临时寄存器
 * @param val IR 值 (必须是 IR_VAL_REG 或 IR_VAL_CONST)
 */
static void ljit_cg_emit_tvalue_addr(struct sljit_compiler *compiler, ljit_ctx_t *ctx,
                                    int tmp_reg, ljit_ir_val_t *val) {
    int tvalue_size = sizeof(TValue);
    if (val->type == IR_VAL_CONST) {
        sljit_emit_op2(compiler, SLJIT_ADD, tmp_reg, 0,
                       SLJIT_IMM, (sljit_sw)ctx->proto->k,
                       SLJIT_IMM, (sljit_sw)(val->v.k * tvalue_size));
    } else {
        sljit_emit_op2(compiler, SLJIT_ADD, tmp_reg, 0,
                       SLJIT_S0, 0,
                       SLJIT_IMM, (sljit_sw)(val->v.reg * tvalue_size));
    }
}

/**
 * @brief 将立即数 (IR_VAL_INT/IR_VAL_NUM) 写入目标寄存器的栈 TValue，用于 GENERIC 路径传参
 * @param compiler SLJIT 编译器
 * @param val IR 立即数值
 * @param dest_ofs 目标 TValue 在栈上的字节偏移 (SLJIT_S0 + dest_ofs)
 */
static void ljit_cg_emit_imm_to_stack(struct sljit_compiler *compiler, ljit_ir_val_t *val, sljit_sw dest_ofs) {
    int value_size = sizeof(Value);
    if (val->type == IR_VAL_INT) {
        /* 写入 value_.i */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), dest_ofs,
                       SLJIT_IMM, (sljit_sw)val->v.i);
        /* 写入 tt_ = LUA_VNUMINT */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                       SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        sljit_emit_op1(compiler, SLJIT_MOV32,
                       SLJIT_MEM1(SLJIT_S0), dest_ofs + value_size,
                       SLJIT_R2, 0);
    } else if (val->type == IR_VAL_NUM) {
        union { lua_Number n; sljit_sw i; } u;
        u.n = val->v.n;
        /* 写入 value_.n (8字节浮点值) */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), dest_ofs,
                       SLJIT_IMM, u.i);
        /* 写入 tt_ = LUA_VNUMFLT */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                       SLJIT_IMM, (sljit_sw)LUA_VNUMFLT);
        sljit_emit_op1(compiler, SLJIT_MOV32,
                       SLJIT_MEM1(SLJIT_S0), dest_ofs + value_size,
                       SLJIT_R2, 0);
    }
}

void ljit_cg_emit_load_operand(struct ljit_ctx *ctx, int target_reg, void *val_ptr) {
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    ljit_ir_val_t *val = (ljit_ir_val_t *)val_ptr;
    if (val->type == IR_VAL_REG) {
        if (val->is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, SLJIT_MEM1(SLJIT_S0), val->stack_ofs);
        } else {
            if (target_reg != val->phys_reg) {
                sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, val->phys_reg, 0);
            }
        }
    } else if (val->type == IR_VAL_INT) {
        sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, SLJIT_IMM, val->v.i);
    } else if (val->type == IR_VAL_NUM) {
        /* IR_VAL_NUM 立即数按位模式加载到整数寄存器 (用于浮点fast path的fcopy) */
        union { lua_Number n; sljit_sw i; } u;
        u.n = val->v.n;
        sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, SLJIT_IMM, u.i);
    } else if (val->type == IR_VAL_CONST) {
        sljit_sw k_ptr = (sljit_sw)&ctx->proto->k[val->v.k];
        /* Use SLJIT_R2 as temporary pointer holding register */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, k_ptr);
        /* Load value_ (offset 0) into target_reg */
        sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, SLJIT_MEM1(SLJIT_R2), 0);
    }
}

/**
 * @brief 内联存储整数值到 Lua 栈 TValue (不再通过 C 函数调用 ljit_icall_set_integer)
 * @details TValue 布局: value_ (sizeof(Value)字节, 偏移0) + tt_ (偏移 sizeof(Value))
 *          直接写入 value_.i = val, tt_ = LUA_VNUMINT, 省去 C 函数调用开销
 * @param ctx JIT 编译上下文
 * @param val_ptr 目标 IR 值指针 (IR_VAL_REG 类型)
 * @param src_reg SLJIT 寄存器编号 (持有要写入的整数值)
 */
void ljit_cg_emit_store_operand(struct ljit_ctx *ctx, void *val_ptr, int src_reg) {
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    ljit_ir_val_t *val = (ljit_ir_val_t *)val_ptr;
    if (val->type == IR_VAL_REG) {
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);

        /* 直接将整数值写入 Lua 栈 TValue 的 value_.i 字段 (偏移0) */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0),
                       (sljit_sw)(val->v.reg * tvalue_size), src_reg, 0);

        /* 写入类型标记 LUA_VNUMINT 到 TValue 的 tt_ 字段 (偏移 sizeof(Value)) */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                       SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        sljit_emit_op1(compiler, SLJIT_MOV32,
                       SLJIT_MEM1(SLJIT_S0),
                       (sljit_sw)(val->v.reg * tvalue_size + value_size),
                       SLJIT_R2, 0);

        /* 非 spilled: 将栈上的 TValue 值_ 字段重新加载到分配好的物理寄存器 */
        if (!val->is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, val->phys_reg, 0,
                           SLJIT_MEM1(SLJIT_S0),
                           (sljit_sw)(val->v.reg * tvalue_size));
        }
    }
}

/**
 * @brief 加载浮点操作数到浮点寄存器
 * @details 根据操作数类型选择正确的加载方式:
 *          - IR_VAL_INT/INT类型寄存器: 整数→浮点数值转换 (CONV_F64_FROM_SW)
 *          - IR_VAL_NUM/NUM类型寄存器: double位模式直接拷贝 (COPY_TO_F64/MOV_F64)
 *          - 未知类型保守按浮点处理（位拷贝）
 * @param ctx JIT 编译上下文
 * @param freg 目标浮点寄存器 (SLJIT_FR0 等)
 * @param val_ptr IR 值指针
 */
static void ljit_cg_emit_load_float_operand(struct ljit_ctx *ctx, int freg, void *val_ptr) {
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    ljit_ir_val_t *val = (ljit_ir_val_t *)val_ptr;
    ljit_analyze_info_t *ainfo = (ljit_analyze_info_t *)ctx->analyze_info;

    if (val->type == IR_VAL_REG) {
        int reg = val->v.reg;
        ljit_type_t t = JIT_TYPE_ANY;
        if (ainfo && reg >= 0 && reg < ainfo->max_regs)
            t = ainfo->reg_types[reg];

        if (val->is_spilled) {
            sljit_sw val_ofs = val->stack_ofs;
            if (t == JIT_TYPE_INT) {
                /* spilled INT: 从栈TValue加载ivalue到R0，再转换为double */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0,
                               SLJIT_MEM1(SLJIT_S0), val_ofs);
                sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, freg, 0, SLJIT_R0, 0);
            } else {
                /* spilled NUM/ANY: 从栈直接加载double位模式 */
                sljit_emit_fop1(compiler, SLJIT_MOV_F64, freg, 0,
                                SLJIT_MEM1(SLJIT_S0), val_ofs);
            }
        } else {
            int phys = val->phys_reg;
            if (t == JIT_TYPE_INT) {
                /* 非spilled INT: phys_reg持整数值，需要数值转换 */
                sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, freg, 0, phys, 0);
            } else {
                /* 非spilled NUM/ANY: phys_reg持double位模式，直接位拷贝 */
                sljit_emit_fcopy(compiler, SLJIT_COPY_TO_F64, freg, phys);
            }
        }
    } else if (val->type == IR_VAL_NUM) {
        /* 浮点立即数 */
        sljit_emit_fset64(compiler, freg, val->v.n);
    } else if (val->type == IR_VAL_INT) {
        /* 整数立即数: 先加载到整数寄存器，再转换为浮点 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, val->v.i);
        sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, freg, 0, SLJIT_R0, 0);
    }
}

/**
 * @brief 将浮点寄存器的值存储到 Lua 栈 TValue
 * @details 写入 value_.n = double, tt_ = LUA_VNUMFLT
 * @param ctx JIT 编译上下文
 * @param val_ptr 目标 IR 值指针 (IR_VAL_REG 类型)
 * @param freg 源浮点寄存器 (持有 double 结果)
 */
static void ljit_cg_emit_store_float_operand(struct ljit_ctx *ctx, void *val_ptr, int freg) {
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    ljit_ir_val_t *val = (ljit_ir_val_t *)val_ptr;
    if (val->type == IR_VAL_REG) {
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);
        sljit_sw dest_ofs = (sljit_sw)(val->v.reg * tvalue_size);

        /* 写入 value_.n (8字节 double) */
        sljit_emit_fop1(compiler, SLJIT_MOV_F64,
                        SLJIT_MEM1(SLJIT_S0), dest_ofs,
                        freg, 0);

        /* 写入 tt_ = LUA_VNUMFLT */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                       SLJIT_IMM, (sljit_sw)LUA_VNUMFLT);
        sljit_emit_op1(compiler, SLJIT_MOV32,
                       SLJIT_MEM1(SLJIT_S0), dest_ofs + value_size,
                       SLJIT_R2, 0);

        /* 非 spilled: 将 double 位模式拷贝回 phys_reg 整数寄存器 */
        if (!val->is_spilled) {
            sljit_emit_fcopy(compiler, SLJIT_COPY_FROM_F64, freg, val->phys_reg);
        }
    }
}

/**
 * @brief 存储布尔值到 Lua 栈 TValue
 * @details 写入 tt_ = LUA_VFALSE/LUA_VTRUE; 非spilled时phys_reg存0(false)/1(true)
 * @param ctx JIT 编译上下文
 * @param val_ptr 目标 IR 值指针 (IR_VAL_REG 类型)
 * @param is_true 0=false, 1=true
 */
static void ljit_cg_emit_store_bool_operand(struct ljit_ctx *ctx, void *val_ptr, int is_true) {
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    ljit_ir_val_t *val = (ljit_ir_val_t *)val_ptr;
    if (val->type == IR_VAL_REG) {
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);
        sljit_sw dest_ofs = (sljit_sw)(val->v.reg * tvalue_size);
        sljit_sw bool_tt = is_true ? (sljit_sw)LUA_VTRUE : (sljit_sw)LUA_VFALSE;
        sljit_sw bool_val = is_true ? 1 : 0;

        /* 写入 value_ 为0/1（便于phys_reg加载） */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), dest_ofs,
                       SLJIT_IMM, bool_val);

        /* 写入 tt_ = LUA_VTRUE 或 LUA_VFALSE */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, bool_tt);
        sljit_emit_op1(compiler, SLJIT_MOV32,
                       SLJIT_MEM1(SLJIT_S0), dest_ofs + value_size,
                       SLJIT_R2, 0);

        /* 非 spilled: 将0/1加载到phys_reg */
        if (!val->is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, val->phys_reg, 0,
                           SLJIT_IMM, bool_val);
        }
    }
}

/**
 * @brief 生成二元运算的通用 C 回退路径
 * @details 按照 ljit_icall_pow 的模式: 设置 R0=L, R1=ra, R2=rb, R3=rc, 然后调用指定 C 函数
 *          对于立即数操作数 (IR_VAL_INT/IR_VAL_NUM)，先写入目标寄存器的栈槽，再传址
 * @param ctx JIT 编译上下文
 * @param node IR 节点
 * @param cfunc C 函数地址
 */
static void ljit_cg_emit_generic_binary(ljit_ctx_t *ctx, ljit_ir_node_t *node, sljit_sw cfunc) {
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    int tvalue_size = sizeof(TValue);

    /* 处理立即数: 先写入到 dest 栈槽 (C函数会先拷贝参数再覆盖dest，别名安全) */
    int src1_is_imm = (node->src1.type == IR_VAL_INT || node->src1.type == IR_VAL_NUM);
    int src2_is_imm = (node->src2.type == IR_VAL_INT || node->src2.type == IR_VAL_NUM);
    sljit_sw dest_ofs = (sljit_sw)(node->dest.v.reg * tvalue_size);

    if (src1_is_imm) {
        ljit_cg_emit_imm_to_stack(compiler, &node->src1, dest_ofs);
    }
    if (src2_is_imm) {
        /* 如果两个都是立即数（理论上不会发生，但保险起见），src2写到dest+一个tvalue_size之后 */
        /* 注: 当前翻译器不会产生两个立即数的二元运算，所以src2_is_imm时src1不是imm，可安全使用dest_ofs */
        /* 但如果两个都是imm，需要额外空间。保险起见，第二个imm写到dest_ofs前一个槽 */
        if (src1_is_imm) {
            ljit_cg_emit_imm_to_stack(compiler, &node->src2, dest_ofs - tvalue_size);
        } else {
            ljit_cg_emit_imm_to_stack(compiler, &node->src2, dest_ofs);
        }
    }

    /* R0 = L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra 地址 */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, dest_ofs);

    /* R2 = rb 地址 */
    if (src1_is_imm) {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, dest_ofs);
    } else {
        ljit_cg_emit_tvalue_addr(compiler, ctx, SLJIT_R2, &node->src1);
    }

    /* R3 = rc 地址 */
    if (src2_is_imm) {
        if (src1_is_imm) {
            sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, dest_ofs - tvalue_size);
        } else {
            sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, dest_ofs);
        }
    } else {
        ljit_cg_emit_tvalue_addr(compiler, ctx, SLJIT_R3, &node->src2);
    }

    /* 调用 C 函数: (L, ra, rb, rc) */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, cfunc);

    /* C 函数已正确设置 ra 的 TValue。非spilled时从栈加载 value_ 到 phys_reg */
    if (node->dest.type == IR_VAL_REG && !node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0,
                       SLJIT_MEM1(SLJIT_S0), dest_ofs);
    }

    /* 标记 dest 类型为 ANY (因为 C 路径可能产生任意类型) */
    ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_ANY);
}

/**
 * @brief 生成一元运算的通用 C 回退路径
 * @param ctx JIT 编译上下文
 * @param node IR 节点
 * @param cfunc C 函数地址 (签名: void (lua_State *L, TValue *ra, TValue *rb))
 */
static void ljit_cg_emit_generic_unary(ljit_ctx_t *ctx, ljit_ir_node_t *node, sljit_sw cfunc) {
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    int tvalue_size = sizeof(TValue);
    sljit_sw dest_ofs = (sljit_sw)(node->dest.v.reg * tvalue_size);

    /* 处理立即数: src1如果是IR_VAL_INT/IR_VAL_NUM，先写入dest栈槽 */
    int src1_is_imm = (node->src1.type == IR_VAL_INT || node->src1.type == IR_VAL_NUM);
    if (src1_is_imm) {
        ljit_cg_emit_imm_to_stack(compiler, &node->src1, dest_ofs);
    }

    /* R0 = L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra 地址 */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                   SLJIT_IMM, dest_ofs);

    /* R2 = rb 地址 */
    if (src1_is_imm) {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0,
                       SLJIT_IMM, dest_ofs);
    } else {
        ljit_cg_emit_tvalue_addr(compiler, ctx, SLJIT_R2, &node->src1);
    }

    /* 调用 C 函数: (L, ra, rb) */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W), SLJIT_IMM, cfunc);

    /* 非spilled时从栈加载 value_ 到 phys_reg */
    if (node->dest.type == IR_VAL_REG && !node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0,
                       SLJIT_MEM1(SLJIT_S0), dest_ofs);
    }

    ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_ANY);
}

/* ========================================================================
 * 算术运算代码生成 (二元)
 * ======================================================================== */

/**
 * @brief ADD: 整数快速路径 / 浮点快速路径 / 通用C回退
 * @details INT路径: t1==INT && t2==INT -> SLJIT_ADD, store INT
 *          NUM路径: t1==NUM && t2==NUM -> SLJIT_ADD_F64, store NUMFLT
 *          其他:   -> ljit_icall_arith_add (C回退)
 */
void ljit_cg_emit_add(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "ADD: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        /* INT 快速路径: 整数加法 */
        JIT_DBG(MOD_CG_ARITH, "ADD: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else if (t1 == JIT_TYPE_NUM && t2 == JIT_TYPE_NUM) {
        /* NUM 快速路径: 浮点加法 */
        JIT_DBG(MOD_CG_ARITH, "ADD: NUM_FASTPATH"); ljit_stat_num_fastpath++;
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR0, &node->src1);
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR1, &node->src2);
        sljit_emit_fop2(compiler, SLJIT_ADD_F64, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
        ljit_cg_emit_store_float_operand(ctx, &node->dest, SLJIT_FR0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_NUM);
    } else if ((t1 == JIT_TYPE_INT || t1 == JIT_TYPE_ANY) &&
               (t2 == JIT_TYPE_INT || t2 == JIT_TYPE_ANY) &&
               node->src1.type == IR_VAL_REG &&
               (node->src2.type == IR_VAL_REG || node->src2.type == IR_VAL_INT)) {
        /*
         * 运行时类型守卫整数快速路径.
         * 编译时类型为 ANY (如函数参数), 运行时检查是否为整数.
         * 若是整数则直接 SLJIT_ADD, 否则回退到通用 icall.
         * src2 为 IR_VAL_INT 时不需要守卫 (编译时已知是整数).
         */
        JIT_DBG(MOD_CG_ARITH, "ADD: GUARDED_INT_FASTPATH (src2_is_int=%d)",
            node->src2.type == IR_VAL_INT);
        ljit_stat_guarded_fastpath++;
        struct sljit_compiler *c = compiler;
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);

        /* 守卫: 检查 src1 的 tt_ 是否为 LUA_VNUMINT */
        sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                       SLJIT_MEM1(SLJIT_S0),
                       (sljit_sw)(node->src1.v.reg * tvalue_size + value_size));
        struct sljit_jump *guard1 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
            SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);

        struct sljit_jump *guard2 = NULL;
        if (node->src2.type == IR_VAL_REG) {
            /* src2 是寄存器: 也需要运行时类型检查 */
            sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                           SLJIT_MEM1(SLJIT_S0),
                           (sljit_sw)(node->src2.v.reg * tvalue_size + value_size));
            guard2 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        }

        /* 守卫通过: 加载整数值, 做加法, 存储结果 */
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);

        /* 快速路径完成, 跳过通用路径 */
        struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);

        /* 守卫失败: 回退到通用 icall */
        struct sljit_label *generic_label = sljit_emit_label(c);
        sljit_set_label(guard1, generic_label);
        if (guard2) sljit_set_label(guard2, generic_label);
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_add);

        struct sljit_label *done_label = sljit_emit_label(c);
        sljit_set_label(done, done_label);
    } else {
        /* 通用 C 回退 */
        JIT_DBG(MOD_CG_ARITH, "ADD: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_add);
    }
}

/**
 * @brief SUB: 整数快速路径 / 浮点快速路径 / 通用C回退
 * @details INT路径: t1==INT && t2==INT -> SLJIT_SUB, store INT
 *          NUM路径: t1==NUM && t2==NUM -> SLJIT_SUB_F64, store NUMFLT
 *          其他:   -> ljit_icall_arith_sub (C回退)
 */
void ljit_cg_emit_sub(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "SUB: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "SUB: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(compiler, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else if (t1 == JIT_TYPE_NUM && t2 == JIT_TYPE_NUM) {
        JIT_DBG(MOD_CG_ARITH, "SUB: NUM_FASTPATH"); ljit_stat_num_fastpath++;
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR0, &node->src1);
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR1, &node->src2);
        sljit_emit_fop2(compiler, SLJIT_SUB_F64, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
        ljit_cg_emit_store_float_operand(ctx, &node->dest, SLJIT_FR0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_NUM);
    } else if ((t1 == JIT_TYPE_INT || t1 == JIT_TYPE_ANY) &&
               (t2 == JIT_TYPE_INT || t2 == JIT_TYPE_ANY) &&
               node->src1.type == IR_VAL_REG &&
               (node->src2.type == IR_VAL_REG || node->src2.type == IR_VAL_INT)) {
        /*
         * 运行时类型守卫整数快速路径.
         * 编译时类型为 ANY (如函数参数), 运行时检查是否为整数.
         * 若是整数则直接 SLJIT_SUB, 否则回退到通用 icall.
         * src2 为 IR_VAL_INT 时不需要守卫 (编译时已知是整数).
         */
        JIT_DBG(MOD_CG_ARITH, "SUB: GUARDED_INT_FASTPATH (src2_is_int=%d)",
            node->src2.type == IR_VAL_INT);
        ljit_stat_guarded_fastpath++;
        struct sljit_compiler *c = compiler;
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);

        /* 守卫: 检查 src1 的 tt_ 是否为 LUA_VNUMINT */
        sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                       SLJIT_MEM1(SLJIT_S0),
                       (sljit_sw)(node->src1.v.reg * tvalue_size + value_size));
        struct sljit_jump *guard1 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
            SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);

        struct sljit_jump *guard2 = NULL;
        if (node->src2.type == IR_VAL_REG) {
            /* src2 是寄存器: 也需要运行时类型检查 */
            sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                           SLJIT_MEM1(SLJIT_S0),
                           (sljit_sw)(node->src2.v.reg * tvalue_size + value_size));
            guard2 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        }

        /* 守卫通过: 加载整数值, 做减法, 存储结果 */
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(c, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);

        /* 快速路径完成, 跳过通用路径 */
        struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);

        /* 守卫失败: 回退到通用 icall */
        struct sljit_label *generic_label = sljit_emit_label(c);
        sljit_set_label(guard1, generic_label);
        if (guard2) sljit_set_label(guard2, generic_label);
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_sub);

        struct sljit_label *done_label = sljit_emit_label(c);
        sljit_set_label(done, done_label);
    } else {
        JIT_DBG(MOD_CG_ARITH, "SUB: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_sub);
    }
}

/**
 * @brief MUL: 整数快速路径 / 浮点快速路径 / 通用C回退
 * @details INT路径: t1==INT && t2==INT -> SLJIT_MUL, store INT
 *          NUM路径: t1==NUM && t2==NUM -> SLJIT_MUL_F64, store NUMFLT
 *          其他:   -> ljit_icall_arith_mul (C回退)
 */
void ljit_cg_emit_mul(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "MUL: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "MUL: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(compiler, SLJIT_MUL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else if (t1 == JIT_TYPE_NUM && t2 == JIT_TYPE_NUM) {
        JIT_DBG(MOD_CG_ARITH, "MUL: NUM_FASTPATH"); ljit_stat_num_fastpath++;
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR0, &node->src1);
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR1, &node->src2);
        sljit_emit_fop2(compiler, SLJIT_MUL_F64, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
        ljit_cg_emit_store_float_operand(ctx, &node->dest, SLJIT_FR0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_NUM);
    } else if ((t1 == JIT_TYPE_INT || t1 == JIT_TYPE_ANY) &&
               (t2 == JIT_TYPE_INT || t2 == JIT_TYPE_ANY) &&
               node->src1.type == IR_VAL_REG &&
               (node->src2.type == IR_VAL_REG || node->src2.type == IR_VAL_INT)) {
        /* 运行时类型守卫整数快速路径 */
        JIT_DBG(MOD_CG_ARITH, "MUL: GUARDED_INT_FASTPATH (src2_is_int=%d)",
            node->src2.type == IR_VAL_INT);
        ljit_stat_guarded_fastpath++;
        struct sljit_compiler *c = compiler;
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);
        sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                       SLJIT_MEM1(SLJIT_S0),
                       (sljit_sw)(node->src1.v.reg * tvalue_size + value_size));
        struct sljit_jump *guard1 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
            SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        struct sljit_jump *guard2 = NULL;
        if (node->src2.type == IR_VAL_REG) {
            sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                           SLJIT_MEM1(SLJIT_S0),
                           (sljit_sw)(node->src2.v.reg * tvalue_size + value_size));
            guard2 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        }
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(c, SLJIT_MUL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
        struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
        struct sljit_label *generic_label = sljit_emit_label(c);
        sljit_set_label(guard1, generic_label);
        if (guard2) sljit_set_label(guard2, generic_label);
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_mul);
        struct sljit_label *done_label = sljit_emit_label(c);
        sljit_set_label(done, done_label);
    } else {
        JIT_DBG(MOD_CG_ARITH, "MUL: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_mul);
    }
}

/**
 * @brief DIV (/): Lua 中总是产生浮点结果
 * @details INT路径: 两个 INT 操作数也需要转换为 double 后做浮点除法 (因为 / 总是产生float)
 *          NUM路径: SLJIT_DIV_F64
 *          其他:   -> ljit_icall_arith_div (C回退)
 */
void ljit_cg_emit_div(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "DIV: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if ((t1 == JIT_TYPE_INT || t1 == JIT_TYPE_NUM) && (t2 == JIT_TYPE_INT || t2 == JIT_TYPE_NUM)) {
        /* 数值快速路径: int/int, int/num, num/int, num/num 均走浮点除法 */
        JIT_DBG(MOD_CG_ARITH, "DIV: NUM_FASTPATH"); ljit_stat_num_fastpath++;
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR0, &node->src1);
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR1, &node->src2);
        sljit_emit_fop2(compiler, SLJIT_DIV_F64, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
        ljit_cg_emit_store_float_operand(ctx, &node->dest, SLJIT_FR0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_NUM);
    } else if ((t1 == JIT_TYPE_ANY || t2 == JIT_TYPE_ANY) &&
               node->src1.type == IR_VAL_REG &&
               (node->src2.type == IR_VAL_REG || node->src2.type == IR_VAL_INT)) {
        /* 运行时守卫: 检查操作数是否为数值类型 (INT 或 NUMFLT), 是则走浮点除法 */
        JIT_DBG(MOD_CG_ARITH, "DIV: GUARDED_NUM_FASTPATH"); ljit_stat_guarded_fastpath++;
        struct sljit_compiler *c = compiler;
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);

        /* 守卫 src1: 检查 tt_ 是否为 LUA_VNUMINT 或 LUA_VNUMFLT */
        sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                       SLJIT_MEM1(SLJIT_S0),
                       (sljit_sw)(node->src1.v.reg * tvalue_size + value_size));
        /* 检查是否为整数 (LUA_VNUMINT) */
        struct sljit_jump *guard1_int = sljit_emit_cmp(c, SLJIT_EQUAL,
            SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        /* 检查是否为浮点 (LUA_VNUMFLT) */
        struct sljit_jump *guard1_float = sljit_emit_cmp(c, SLJIT_EQUAL,
            SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMFLT);
        /* 都不是数值类型, 回退到 icall */
        struct sljit_jump *guard1_fail = sljit_emit_jump(c, SLJIT_JUMP);

        struct sljit_jump *guard2_fail = NULL;
        if (node->src2.type == IR_VAL_REG) {
            struct sljit_label *guard1_ok = sljit_emit_label(c);
            sljit_set_label(guard1_int, guard1_ok);
            sljit_set_label(guard1_float, guard1_ok);

            /* 守卫 src2: 检查 tt_ 是否为数值类型 */
            sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                           SLJIT_MEM1(SLJIT_S0),
                           (sljit_sw)(node->src2.v.reg * tvalue_size + value_size));
            struct sljit_jump *guard2_int = sljit_emit_cmp(c, SLJIT_EQUAL,
                SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
            struct sljit_jump *guard2_float = sljit_emit_cmp(c, SLJIT_EQUAL,
                SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMFLT);
            guard2_fail = sljit_emit_jump(c, SLJIT_JUMP);

            struct sljit_label *guard2_ok = sljit_emit_label(c);
            sljit_set_label(guard2_int, guard2_ok);
            sljit_set_label(guard2_float, guard2_ok);
        } else {
            /* src2 是 IR_VAL_INT: 编译时已知是整数, 无需守卫 */
            struct sljit_label *guard1_ok = sljit_emit_label(c);
            sljit_set_label(guard1_int, guard1_ok);
            sljit_set_label(guard1_float, guard1_ok);
        }

        /* 所有守卫通过: 加载为浮点数, 做浮点除法 */
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR0, &node->src1);
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR1, &node->src2);
        sljit_emit_fop2(c, SLJIT_DIV_F64, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
        ljit_cg_emit_store_float_operand(ctx, &node->dest, SLJIT_FR0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_NUM);

        struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);

        /* 守卫失败: 回退到通用 icall */
        struct sljit_label *fallback_label = sljit_emit_label(c);
        sljit_set_label(guard1_fail, fallback_label);
        if (guard2_fail) sljit_set_label(guard2_fail, fallback_label);
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_div);

        struct sljit_label *done_label = sljit_emit_label(c);
        sljit_set_label(done, done_label);
    } else {
        JIT_DBG(MOD_CG_ARITH, "DIV: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_div);
    }
}

/**
 * @brief IDIV (//): 整数除法
 * @details INT路径: t1==INT && t2==INT -> SLJIT_DIVMOD_SW, store INT (R0=商)
 *          其他: -> ljit_icall_arith_idiv (C回退处理类型转换、除零、元方法)
 *          注意: 硬件除零会触发信号，Phase 1 暂时依赖 C 回退处理除零情况，
 *                后续可添加除零检查分支
 */
void ljit_cg_emit_idiv(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "IDIV: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        /* INT 快速路径: 整数除法，商在 R0 */
        JIT_DBG(MOD_CG_ARITH, "IDIV: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op0(compiler, SLJIT_DIVMOD_SW);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else if ((t1 == JIT_TYPE_INT || t1 == JIT_TYPE_ANY) &&
               (t2 == JIT_TYPE_INT || t2 == JIT_TYPE_ANY) &&
               node->src1.type == IR_VAL_REG &&
               (node->src2.type == IR_VAL_REG || node->src2.type == IR_VAL_INT)) {
        /* 运行时类型守卫整数快速路径 */
        JIT_DBG(MOD_CG_ARITH, "IDIV: GUARDED_INT_FASTPATH (src2_is_int=%d)",
            node->src2.type == IR_VAL_INT);
        ljit_stat_guarded_fastpath++;
        struct sljit_compiler *c = compiler;
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);
        sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                       SLJIT_MEM1(SLJIT_S0),
                       (sljit_sw)(node->src1.v.reg * tvalue_size + value_size));
        struct sljit_jump *guard1 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
            SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        struct sljit_jump *guard2 = NULL;
        if (node->src2.type == IR_VAL_REG) {
            sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                           SLJIT_MEM1(SLJIT_S0),
                           (sljit_sw)(node->src2.v.reg * tvalue_size + value_size));
            guard2 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        }
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op0(c, SLJIT_DIVMOD_SW);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
        struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
        struct sljit_label *generic_label = sljit_emit_label(c);
        sljit_set_label(guard1, generic_label);
        if (guard2) sljit_set_label(guard2, generic_label);
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_idiv);
        struct sljit_label *done_label = sljit_emit_label(c);
        sljit_set_label(done, done_label);
    } else {
        /* 通用 C 回退: 处理浮点/混合类型/除零/元方法 */
        JIT_DBG(MOD_CG_ARITH, "IDIV: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_idiv);
    }
}

/**
 * @brief MOD (%): 取模，int%int=int, float%float=float(需要fmod)
 * @details INT路径: t1==INT && t2==INT -> SLJIT_DIVMOD_SW, store INT (R1=余数)
 *          其他: -> ljit_icall_arith_mod (C回退)
 */
void ljit_cg_emit_mod(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "MOD: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "MOD: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op0(compiler, SLJIT_DIVMOD_SW);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R1);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else if ((t1 == JIT_TYPE_INT || t1 == JIT_TYPE_ANY) &&
               (t2 == JIT_TYPE_INT || t2 == JIT_TYPE_ANY) &&
               node->src1.type == IR_VAL_REG &&
               (node->src2.type == IR_VAL_REG || node->src2.type == IR_VAL_INT)) {
        /* 运行时类型守卫整数快速路径 */
        JIT_DBG(MOD_CG_ARITH, "MOD: GUARDED_INT_FASTPATH (src2_is_int=%d)",
            node->src2.type == IR_VAL_INT);
        ljit_stat_guarded_fastpath++;
        struct sljit_compiler *c = compiler;
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);
        sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                       SLJIT_MEM1(SLJIT_S0),
                       (sljit_sw)(node->src1.v.reg * tvalue_size + value_size));
        struct sljit_jump *guard1 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
            SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        struct sljit_jump *guard2 = NULL;
        if (node->src2.type == IR_VAL_REG) {
            sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                           SLJIT_MEM1(SLJIT_S0),
                           (sljit_sw)(node->src2.v.reg * tvalue_size + value_size));
            guard2 = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        }
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op0(c, SLJIT_DIVMOD_SW);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R1);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
        struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
        struct sljit_label *generic_label = sljit_emit_label(c);
        sljit_set_label(guard1, generic_label);
        if (guard2) sljit_set_label(guard2, generic_label);
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_mod);
        struct sljit_label *done_label = sljit_emit_label(c);
        sljit_set_label(done, done_label);
    } else {
        JIT_DBG(MOD_CG_ARITH, "MOD: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_mod);
    }
}

/* ========================================================================
 * 一元运算代码生成
 * ======================================================================== */

/**
 * @brief UNM (一元负号): -x
 * @details INT路径: t1==INT -> 0 - src, store INT
 *          NUM路径: t1==NUM -> SLJIT_NEG_F64, store NUMFLT
 *          其他: -> ljit_icall_unm (C回退)
 */
void ljit_cg_emit_unm(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);

    JIT_DBG(MOD_CG_ARITH, "UNM: pc=%d, t1=%d", node->original_pc, t1);

    if (t1 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "UNM: INT_FASTPATH"); ljit_stat_int_fastpath++;
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src1);
        sljit_emit_op2(compiler, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else if (t1 == JIT_TYPE_NUM) {
        JIT_DBG(MOD_CG_ARITH, "UNM: NUM_FASTPATH"); ljit_stat_num_fastpath++;
        ljit_cg_emit_load_float_operand(ctx, SLJIT_FR0, &node->src1);
        sljit_emit_fop1(compiler, SLJIT_NEG_F64, SLJIT_FR0, 0, SLJIT_FR0, 0);
        ljit_cg_emit_store_float_operand(ctx, &node->dest, SLJIT_FR0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_NUM);
    } else if ((t1 == JIT_TYPE_INT || t1 == JIT_TYPE_ANY) &&
               node->src1.type == IR_VAL_REG) {
        /* 运行时类型守卫: 检查 src1 是否为整数，是则 0 - src1 */
        JIT_DBG(MOD_CG_ARITH, "UNM: GUARDED_INT_FASTPATH"); ljit_stat_guarded_fastpath++;
        struct sljit_compiler *c = compiler;
        int tvalue_size = sizeof(TValue);
        int value_size = sizeof(Value);
        sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                       SLJIT_MEM1(SLJIT_S0),
                       (sljit_sw)(node->src1.v.reg * tvalue_size + value_size));
        struct sljit_jump *guard = sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
            SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);
        sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src1);
        sljit_emit_op2(c, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
        struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
        struct sljit_label *generic_label = sljit_emit_label(c);
        sljit_set_label(guard, generic_label);
        ljit_cg_emit_generic_unary(ctx, node, (sljit_sw)ljit_icall_unm);
        struct sljit_label *done_label = sljit_emit_label(c);
        sljit_set_label(done, done_label);
    } else {
        JIT_DBG(MOD_CG_ARITH, "UNM: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_unary(ctx, node, (sljit_sw)ljit_icall_unm);
    }
}

/**
 * @brief NOT (逻辑非): not x
 * @details NIL路径: not nil → true
 *          已知真值类型(INT/NUM/STR/TAB/FUNC等): 直接设为false
 *          BOOL/未知类型: -> C回退 (动态翻转布尔值需要条件分支写tt_, Phase 1保守处理)
 */
void ljit_cg_emit_not(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);

    JIT_DBG(MOD_CG_ARITH, "NOT: pc=%d, t1=%d", node->original_pc, t1);

    if (t1 == JIT_TYPE_NIL) {
        /* NIL 快速路径: not nil → true */
        JIT_DBG(MOD_CG_ARITH, "NOT: NIL_FASTPATH");
        ljit_cg_emit_store_bool_operand(ctx, &node->dest, 1);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_BOOL);
    } else if (t1 != JIT_TYPE_ANY && t1 != JIT_TYPE_BOOL) {
        /* 已知真值类型(INT/NUM/STR/TAB/FUNC/USERDATA等): 结果为false */
        JIT_DBG(MOD_CG_ARITH, "NOT: TRUE_FASTPATH");
        ljit_cg_emit_store_bool_operand(ctx, &node->dest, 0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_BOOL);
    } else {
        /* BOOL/ANY: 走C回退处理动态值 */
        JIT_DBG(MOD_CG_ARITH, "NOT: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_unary(ctx, node, (sljit_sw)ljit_icall_not);
    }
}

/**
 * @brief POW (^): 幂运算，始终走C路径因为涉及浮点和元方法
 * @details 统一使用 generic_binary 模式，支持立即数操作数；结果类型为NUM
 */
void ljit_cg_emit_pow(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    JIT_DBG(MOD_CG_ARITH, "POW: pc=%d, GENERIC", node->original_pc);

    ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_pow);

    /* POW 结果始终为数值类型，更新为NUM */
    ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_NUM);
}

void ljit_cg_emit_nop(void *node_ptr, void *ctx_ptr) {
    /* Do nothing */
    (void)node_ptr;
    (void)ctx_ptr;
}

/* ========================================================================
 * 位运算代码生成
 * 注意: 位运算只能用于整数，没有NUM路径，非INT则走C回退
 * ======================================================================== */

/**
 * @brief BAND (&): 按位与
 * @details INT路径: t1==INT && t2==INT -> SLJIT_AND, store INT
 *          其他: -> ljit_icall_arith_band (C回退处理类型转换和元方法)
 */
void ljit_cg_emit_band(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "BAND: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "BAND: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else {
        JIT_DBG(MOD_CG_ARITH, "BAND: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_band);
    }
}

/**
 * @brief BOR (|): 按位或
 * @details INT路径: t1==INT && t2==INT -> SLJIT_OR, store INT
 *          其他: -> ljit_icall_arith_bor (C回退)
 */
void ljit_cg_emit_bor(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "BOR: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "BOR: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(compiler, SLJIT_OR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else {
        JIT_DBG(MOD_CG_ARITH, "BOR: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_bor);
    }
}

/**
 * @brief BXOR (~): 按位异或
 * @details INT路径: t1==INT && t2==INT -> SLJIT_XOR, store INT
 *          其他: -> ljit_icall_arith_bxor (C回退)
 */
void ljit_cg_emit_bxor(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "BXOR: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "BXOR: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else {
        JIT_DBG(MOD_CG_ARITH, "BXOR: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_bxor);
    }
}

/**
 * @brief SHL (<<): 左移
 * @details INT路径: t1==INT && t2==INT -> SLJIT_SHL, store INT
 *          其他: -> ljit_icall_arith_shl (C回退)
 */
void ljit_cg_emit_shl(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "SHL: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "SHL: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(compiler, SLJIT_SHL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else {
        JIT_DBG(MOD_CG_ARITH, "SHL: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_shl);
    }
}

/**
 * @brief SHR (>>): 逻辑右移（无符号）
 * @details INT路径: t1==INT && t2==INT -> SLJIT_LSHR, store INT
 *          其他: -> ljit_icall_arith_shr (C回退)
 */
void ljit_cg_emit_shr(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);
    ljit_type_t t2 = ljit_cg_get_val_type(ctx, &node->src2);

    JIT_DBG(MOD_CG_ARITH, "SHR: pc=%d, t1=%d, t2=%d", node->original_pc, t1, t2);

    if (t1 == JIT_TYPE_INT && t2 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "SHR: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_emit_op2(compiler, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else {
        JIT_DBG(MOD_CG_ARITH, "SHR: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_binary(ctx, node, (sljit_sw)ljit_icall_arith_shr);
    }
}

/**
 * @brief BNOT (按位取反): ~x
 * @details INT路径: t1==INT -> XOR -1, store INT
 *          其他: -> ljit_icall_bnot (C回退)
 */
void ljit_cg_emit_bnot(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_type_t t1 = ljit_cg_get_val_type(ctx, &node->src1);

    JIT_DBG(MOD_CG_ARITH, "BNOT: pc=%d, t1=%d", node->original_pc, t1);

    if (t1 == JIT_TYPE_INT) {
        JIT_DBG(MOD_CG_ARITH, "BNOT: INT_FASTPATH"); ljit_stat_int_fastpath++;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, -1);
        sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
        ljit_cg_update_reg_type(ctx, node->dest.v.reg, JIT_TYPE_INT);
    } else {
        JIT_DBG(MOD_CG_ARITH, "BNOT: GENERIC"); ljit_stat_generic++;
        ljit_cg_emit_generic_unary(ctx, node, (sljit_sw)ljit_icall_bnot);
    }
}
