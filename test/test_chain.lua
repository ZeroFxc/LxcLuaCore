local x = 5
print("x -gt 10:", [ x -gt 10 ])
print("x -lt 20:", [ x -lt 20 ])
print("x -eq 5:", [ x -eq 5 ])
print("chain:", [ x -gt 10 -a x -lt 20 -o x -eq 5 ])
print("expected: true")