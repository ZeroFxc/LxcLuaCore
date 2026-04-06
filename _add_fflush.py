import re

with open('lpromise.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Add fflush after each [DBG] fprintf
c = c.replace('fprintf(stderr, "[DBG]', 'fprintf(stderr, "[DBG]')
# Simpler: just find and replace the pattern
lines = c.split('\n')
new_lines = []
for line in lines:
    if '[DBG]' in line and 'fprintf' in line and 'fflush' not in line:
        line = line.rstrip().rstrip(';') + '); fflush(stderr);'
    new_lines.append(line)
c = '\n'.join(new_lines)

with open('lpromise.c', 'w', encoding='utf-8') as f:
    f.write(c)
print('Done')
