#include "backend.h"
#include "x86_64.h"
#include <string.h>

void backend_init_x86_64(Backend *be, const char *output) {
    memset(be, 0, sizeof(*be));
    x86_64_init(be, output);
}
