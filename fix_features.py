"""Fix compilation errors in lspsrv_features.c add_features.py generated code"""
import os

features_path = r'e:\Soft\Proje\LXCLUA-NCore\lua\src\lspsrv\lspsrv_features.c'
proto_path = r'e:\Soft\Proje\LXCLUA-NCore\lua\src\lspsrv\lspsrv_proto.c'
header_path = r'e:\Soft\Proje\LXCLUA-NCore\lua\src\lspsrv\lspsrv.h'

# 1. Fix lspsrv_features.c
with open(features_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Fix empty char constant: '' to '\''
content = content.replace("== ''')", "== '\\'')")

# Fix newline inside string: "\n" was split across lines - fix by using \\n
# The issue is that Python wrote literal \n which became actual newline in C string
content = content.replace('if (strcmp(ch, "\n") == 0) {', 'if (strcmp(ch, "\\n") == 0) {')
# Actually the raw newline needs to be replaced. Let me find the exact pattern
# The generated code has an actual newline inside the C string literal

# Fix int *** to int ** for call hierarchy functions (flat arrays, not arrays of arrays)
content = content.replace('int ***out_from_lines', 'int **out_from_lines')
content = content.replace('int ***out_from_cols', 'int **out_from_cols')
content = content.replace('int ***out_to_lines', 'int **out_to_lines')
content = content.replace('int ***out_to_cols', 'int **out_to_cols')

# Fix the specific newline-in-string issue more precisely
# Replace the pattern where strcmp has actual newline in string
import re
# Fix the raw newline inside C string for lsp_on_type_formatting
# The pattern is: if (strcmp(ch, "\n<actual newline>") == 0) {
# Replace with: if (strcmp(ch, "\n") == 0) {
content = re.sub(r'if \(strcmp\(ch, "\n\n"\) == 0\)', r'if (strcmp(ch, "\n") == 0)', content)

with open(features_path, 'w', encoding='utf-8') as f:
    f.write(content)

print("Fixed lspsrv_features.c")

# 2. Fix lspsrv_proto.c - change int ** variables to int * for call hierarchy
with open(proto_path, 'r', encoding='utf-8') as f:
    pcontent = f.read()

# The proto.c declares int **from_lines etc. for call hierarchy
# These should be int * since the functions now return int ** (flat arrays)
pcontent = pcontent.replace(
    'int **from_lines = NULL, **from_cols = NULL, **to_lines = NULL, **to_cols = NULL;\n        int nch = 0;\n        if (doc) lsp_call_hierarchy_incoming(doc, ch_line, ch_col, &from_lines, &from_cols, &to_lines, &to_cols, &nch);',
    'int *from_lines = NULL, *from_cols = NULL, *to_lines = NULL, *to_cols = NULL;\n        int nch = 0;\n        if (doc) lsp_call_hierarchy_incoming(doc, ch_line, ch_col, &from_lines, &from_cols, &to_lines, &to_cols, &nch);'
)

# Fix the first occurrence (incoming calls) and second (outgoing calls)
pcontent = pcontent.replace(
    '((*from_lines)[i])', '((from_lines)[i])'
)
pcontent = pcontent.replace(
    '((*from_cols)[i])', '((from_cols)[i])'
)
pcontent = pcontent.replace(
    '((*to_lines)[i])', '((to_lines)[i])'
)
pcontent = pcontent.replace(
    '((*to_cols)[i])', '((to_cols)[i])'
)

# Fix the lsp_free calls - remove the extra dereference
pcontent = pcontent.replace(
    'lsp_free(*from_lines); lsp_free(*from_cols);',
    'lsp_free(from_lines); lsp_free(from_cols);'
)
pcontent = pcontent.replace(
    'lsp_free(*to_lines); lsp_free(*to_cols);',
    'lsp_free(to_lines); lsp_free(to_cols);'
)

# Fix outgoing call hierarchy the same way
pcontent = pcontent.replace(
    'int nch = 0;\n        if (doc) lsp_call_hierarchy_outgoing(doc, ch_line, ch_col, &from_lines, &from_cols, &to_lines, &to_cols, &nch);',
    'int nch = 0;\n        if (doc) lsp_call_hierarchy_outgoing(doc, ch_line, ch_col, &from_lines, &from_cols, &to_lines, &to_cols, &nch);'
)

with open(proto_path, 'w', encoding='utf-8') as f:
    f.write(pcontent)

print("Fixed lspsrv_proto.c")

# 3. Fix lspsrv.h - change int *** to int **
with open(header_path, 'r', encoding='utf-8') as f:
    hcontent = f.read()

hcontent = hcontent.replace('int ***out_from_lines, int ***out_from_cols, int ***out_to_lines, int ***out_to_cols', 
                            'int **out_from_lines, int **out_from_cols, int **out_to_lines, int **out_to_cols')

with open(header_path, 'w', encoding='utf-8') as f:
    f.write(hcontent)

print("Fixed lspsrv.h")
print("All fixes applied!")