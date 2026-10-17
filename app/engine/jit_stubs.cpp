// Replaces upstream place/emit_jump_cache_probe.cpp, whose static_assert needs a 32-bit host (x86-32 JIT only).
// Never called: the interpreter does not emit code.
#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {
uint8_t* EmitJumpCacheProbe(uint8_t* cursor, BlockContext*) { return cursor; }
}
