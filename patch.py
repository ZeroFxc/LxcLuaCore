with open('lparser.c', 'r') as f:
    content = f.read()

content = content.replace('''    if (ls->t.token == TK_RETURN) {
      statement(ls);
      return;  /* 'return' must be last statement */
    }''', '')

content = content.replace('''      if (ls->t.token == TK_RETURN) {
        statement(ls);
        break;
      }''', '')

content = content.replace('''          if (ls->t.token == TK_RETURN) {
             statement(ls);
             break;
          }''', '')

content = content.replace('''        if (ls->t.token == TK_RETURN) {
          statement(ls);
          break;
        }''', '')

content = content.replace('''       if (ls->t.token == TK_RETURN) {
         statement(ls);
       } else {
         statement(ls);
       }''', '       statement(ls);')

with open('lparser.c', 'w') as f:
    f.write(content)
