#include "capstone.h"

cs_err cs_open( cs_arch arch, cs_mode mode, csh* handle ) { (void)arch; (void)mode; (void)handle; return CS_ERR_ARCH; }
cs_err cs_option( csh handle, cs_opt_type type, size_t value ) { (void)handle; (void)type; (void)value; return CS_ERR_OK; }
size_t cs_disasm( csh handle, const uint8_t* code, size_t code_size, uint64_t address, size_t count, cs_insn** insn ) { (void)handle; (void)code; (void)code_size; (void)address; (void)count; (void)insn; return 0; }
void cs_free( cs_insn* insn, size_t count ) { (void)insn; (void)count; }
cs_err cs_close( csh* handle ) { (void)handle; return CS_ERR_OK; }
