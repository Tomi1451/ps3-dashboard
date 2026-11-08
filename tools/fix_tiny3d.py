#!/usr/bin/env python3
"""Replace Tiny3D's RSX command-buffer callback with a version that is safe on modern GCC.

The original inline asm never tells the compiler that the callback needs
r3 = context and r4 = count, and it only lists r31/r0/lr as clobbered.
Newer compilers are free to reuse r3/r4 (and keep values in r5-r12, ctr, cr)
across the call, so the moment the command buffer wraps the GPU hangs.
"""
import re, sys
path = sys.argv[1]
src = open(path).read()
start = src.index("s32 __attribute__((noinline)) tiny_rsxContextCallback")
end = src.index("void commandBufferPut")
fixed = r'''s32 __attribute__((noinline)) tiny_rsxContextCallback(tiny_gcmContextData *context,u32 count)
{
  /* PS Dashboard fix: pin the arguments in r3/r4 and declare every
     register the called function may change (PPC64 ELF ABI volatile set). */
  register uint64_t r3 asm("r3") = (uint64_t) context;
  register uint64_t r4 asm("r4") = (uint64_t) count;
  uint64_t opd = (uint64_t) context->callback;
  asm volatile (
    "stdu  1,-128(1)\n"
    "mr    31,2\n"
    "lwz   0,0(%2)\n"
    "lwz   2,4(%2)\n"
    "mtctr 0\n"
    "bctrl\n"
    "mr    2,31\n"
    "addi  1,1,128\n"
    : "+r"(r3), "+r"(r4)
    : "b"(opd)
    : "r0", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "r31",
      "lr", "ctr", "xer", "cr0", "cr1", "cr5", "cr6", "cr7", "memory"
  );
  return (s32) r3;
}

'''
src = src[:start] + fixed + src[end:]
if "#include <stdint.h>" not in src:
    src = "#include <stdint.h>\n" + src
open(path, "w").write(src)
print("patched", path)
