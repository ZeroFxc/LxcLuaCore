#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "lobject.h"

// Translates and dumps the given Proto structure into a Lua table format.
int translate_and_dump(const Proto *f, const char *outfile);

#endif // TRANSLATE_H
