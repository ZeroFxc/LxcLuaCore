#include "translate.h"
#include "dump.h"

int translate_and_dump(const Proto *f, const char *outfile) {
    return dump_proto(f, outfile);
}
