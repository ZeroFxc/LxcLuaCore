with open("src/jit/sljitLir.h", "r") as f:
    content = f.read()

content = content.replace("""#define SLJIT_ARGS4V(arg1, arg2, arg3, arg4) \\

#define SLJIT_ARGS5(ret, arg1, arg2, arg3, arg4, arg5) \\
	(SLJIT_ARGS4(ret, arg1, arg2, arg3, arg4) | SLJIT_ARG_VALUE(SLJIT_ARG_TO_TYPE(arg5), 5))
#define SLJIT_ARGS5V(arg1, arg2, arg3, arg4, arg5) \\
	(SLJIT_ARGS4V(arg1, arg2, arg3, arg4) | SLJIT_ARG_VALUE(SLJIT_ARG_TO_TYPE(arg5), 5))
	(SLJIT_ARGS3V(arg1, arg2, arg3) | SLJIT_ARG_VALUE(SLJIT_ARG_TO_TYPE(arg4), 4))""", """#define SLJIT_ARGS4V(arg1, arg2, arg3, arg4) \\
	(SLJIT_ARGS3V(arg1, arg2, arg3) | SLJIT_ARG_VALUE(SLJIT_ARG_TO_TYPE(arg4), 4))
#define SLJIT_ARGS5(ret, arg1, arg2, arg3, arg4, arg5) \\
	(SLJIT_ARGS4(ret, arg1, arg2, arg3, arg4) | SLJIT_ARG_VALUE(SLJIT_ARG_TO_TYPE(arg5), 5))
#define SLJIT_ARGS5V(arg1, arg2, arg3, arg4, arg5) \\
	(SLJIT_ARGS4V(arg1, arg2, arg3, arg4) | SLJIT_ARG_VALUE(SLJIT_ARG_TO_TYPE(arg5), 5))""")

with open("src/jit/sljitLir.h", "w") as f:
    f.write(content)
