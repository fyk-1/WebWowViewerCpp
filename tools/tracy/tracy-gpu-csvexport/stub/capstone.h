// Local stand-in for capstone.h. csvexport never disassembles; this satisfies the compiler
// and linker for the call sites in server/TracyWorker.cpp only. See README.md.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef size_t csh;

typedef enum cs_err { CS_ERR_OK = 0, CS_ERR_ARCH = 2 } cs_err;
typedef enum cs_arch { CS_ARCH_ARM, CS_ARCH_ARM64, CS_ARCH_X86 } cs_arch;
typedef enum cs_mode { CS_MODE_ARM = 0, CS_MODE_32 = 1 << 2, CS_MODE_64 = 1 << 3 } cs_mode;
typedef enum cs_opt_type { CS_OPT_DETAIL = 2 } cs_opt_type;
typedef enum cs_opt_value { CS_OPT_ON = 3 } cs_opt_value;

enum { CS_GRP_JUMP = 1, CS_GRP_CALL = 2 };
enum { X86_OP_IMM = 2 };
enum { ARM_OP_IMM = 2 };
enum { ARM64_OP_IMM = 2 };

typedef struct cs_op { int type; int64_t imm; } cs_op;
typedef struct cs_arch_detail { uint8_t op_count; cs_op operands[8]; } cs_arch_detail;

typedef struct cs_detail
{
    uint8_t groups[8];
    uint8_t groups_count;
    cs_arch_detail x86, arm, arm64;
} cs_detail;

typedef struct cs_insn { uint64_t address; cs_detail* detail; } cs_insn;

#ifdef __cplusplus
extern "C" {
#endif
cs_err cs_open( cs_arch arch, cs_mode mode, csh* handle );
cs_err cs_option( csh handle, cs_opt_type type, size_t value );
size_t cs_disasm( csh handle, const uint8_t* code, size_t code_size, uint64_t address, size_t count, cs_insn** insn );
void cs_free( cs_insn* insn, size_t count );
cs_err cs_close( csh* handle );

#ifdef __cplusplus
}
#endif
