#include <stdio.h>
#include <string.h>
#include "dump.h"
#include "lopcodes.h"

static const char* opnames[] = {
  "VOP_MOVE",
  "VOP_LOADK",
  "VOP_LOADBOOL",
  "VOP_LOADNIL",
  "VOP_GETUPVAL",
  "VOP_GETGLOBAL",
  "VOP_GETTABLE",
  "VOP_SETGLOBAL",
  "VOP_SETUPVAL",
  "VOP_SETTABLE",
  "VOP_NEWTABLE",
  "VOP_SELF",
  "VOP_ADD",
  "VOP_SUB",
  "VOP_MUL",
  "VOP_DIV",
  "VOP_MOD",
  "VOP_POW",
  "VOP_UNM",
  "VOP_NOT",
  "VOP_LEN",
  "VOP_CONCAT",
  "VOP_JMP",
  "VOP_EQ",
  "VOP_LT",
  "VOP_LE",
  "VOP_TEST",
  "VOP_TESTSET",
  "VOP_CALL",
  "VOP_TAILCALL",
  "VOP_RETURN",
  "VOP_FORLOOP",
  "VOP_FORPREP",
  "VOP_TFORLOOP",
  "VOP_SETLIST",
  "VOP_CLOSE",
  "VOP_CLOSURE",
  "VOP_VARARG"
};

static void dump_string(FILE *out, const char *s) {
    fprintf(out, "\"");
    while (*s) {
        if (*s == '"') fprintf(out, "\\\"");
        else if (*s == '\\') fprintf(out, "\\\\");
        else if (*s == '\n') fprintf(out, "\\n");
        else if (*s == '\r') fprintf(out, "\\r");
        else if (*s == 0x1A) fprintf(out, "\\026");
        else fprintf(out, "%c", *s);
        s++;
    }
    fprintf(out, "\"");
}

static void dump_function(FILE *out, const Proto *f, int indent) {
    char ind[64];
    memset(ind, ' ', indent);
    ind[indent] = '\0';

    fprintf(out, "%s{\n", ind);
    fprintf(out, "%s  numparams = %d,\n", ind, f->numparams);
    fprintf(out, "%s  is_vararg = %d,\n", ind, f->is_vararg);
    fprintf(out, "%s  maxstacksize = %d,\n", ind, f->maxstacksize);
    fprintf(out, "%s  nups = %d,\n", ind, f->nups);

    // Dump instructions
    fprintf(out, "%s  code = {\n", ind);
    for (int i = 0; i < f->sizecode; i++) {
        Instruction inst = f->code[i];
        OpCode o = GET_OPCODE(inst);
        int a = GETARG_A(inst);
        int b = GETARG_B(inst);
        int c = GETARG_C(inst);
        int bx = GETARG_Bx(inst);
        int sbx = GETARG_sBx(inst);

        fprintf(out, "%s    { op = \"%s\", a = %d", ind, opnames[o], a);

        switch (getOpMode(o)) {
            case iABC:
                if (getBMode(o) != OpArgN) fprintf(out, ", b = %d", b);
                if (getCMode(o) != OpArgN) fprintf(out, ", c = %d", c);
                break;
            case iABx:
                if (getBMode(o) != OpArgN) fprintf(out, ", bx = %d", bx);
                break;
            case iAsBx:
                if (getBMode(o) != OpArgN) fprintf(out, ", sbx = %d", sbx);
                break;
        }

        if (o == OP_CLOSURE) {
            Proto *p = f->p[bx];
            fprintf(out, ", upval_insts = { ");
            for (int j = 0; j < p->nups; j++) {
                i++;
                Instruction pseudo = f->code[i];
                OpCode po = GET_OPCODE(pseudo);
                int pb = GETARG_B(pseudo);
                fprintf(out, "{ op = \"%s\", b = %d }%s", opnames[po], pb, (j == p->nups - 1) ? "" : ", ");
            }
            fprintf(out, " }");
        }

        fprintf(out, " }%s\n", (i >= f->sizecode - 1) ? "" : ",");
    }
    fprintf(out, "%s  },\n", ind);

    // Dump constants
    fprintf(out, "%s  constants = {\n", ind);
    for (int i = 0; i < f->sizek; i++) {
        TValue *k = &f->k[i];
        fprintf(out, "%s    ", ind);
        if (ttisnil(k)) {
            fprintf(out, "nil");
        } else if (ttisboolean(k)) {
            fprintf(out, bvalue(k) ? "true" : "false");
        } else if (ttisnumber(k)) {
            fprintf(out, "%.14g", nvalue(k));
        } else if (ttisstring(k)) {
            dump_string(out, svalue(k));
        } else {
            fprintf(out, "nil -- unknown constant type");
        }
        fprintf(out, "%s\n", (i == f->sizek - 1) ? "" : ",");
    }
    fprintf(out, "%s  },\n", ind);

    // Dump upvalues
    fprintf(out, "%s  upvalues = {\n", ind);
    for (int i = 0; i < f->sizeupvalues; i++) {
        fprintf(out, "%s    ", ind);
        dump_string(out, getstr(f->upvalues[i]));
        fprintf(out, "%s\n", (i == f->sizeupvalues - 1) ? "" : ",");
    }
    fprintf(out, "%s  },\n", ind);
    fprintf(out, "%s  sizeupvalues = %d,\n", ind, f->sizeupvalues);

    // Dump nested protos
    fprintf(out, "%s  protos = {\n", ind);
    for (int i = 0; i < f->sizep; i++) {
        dump_function(out, f->p[i], indent + 4);
        if (i < f->sizep - 1) fprintf(out, ",\n");
        else fprintf(out, "\n");
    }
    fprintf(out, "%s  }\n", ind);
    fprintf(out, "%s}", ind);
}

int dump_proto(const Proto *f, const char *outfile) {
    FILE *out = fopen(outfile, "w");
    if (!out) {
        fprintf(stderr, "Could not open output file %s\n", outfile);
        return 1;
    }

    dump_function(out, f, 0);
    fprintf(out, "\n");

    fclose(out);
    return 0;
}
