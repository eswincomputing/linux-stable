/*
 * Functions for setting immediate field of CONST16 instruction.
 */


/* Maximum format size in bits for formats containing CONST16. */
#define XTIMM_MAX_FORMAT_LENGTH 128

/* format length from first byte, only formats containing CONST16 are recognized. */
static uint32_t
xtimm_get_format_length (uint32_t insn0)
{
  if ((insn0 & 0x8) == 0)
    return 24;
  if ((insn0 & 0x100000f) == 0xf)
    return 128;
  if ((insn0 & 0x100000f) == 0xf)
    return 128;
  if ((insn0 & 0x100000f) == 0xf)
    return 128;
  if ((insn0 & 0x100000f) == 0xf)
    return 128;
  if ((insn0 & 0xf) == 0xe)
    return 128;
  if ((insn0 & 0x100000f) == 0xf)
    return 128;
  if ((insn0 & 0x100000f) == 0xf)
    return 128;
  if ((insn0 & 0x100000f) == 0xf)
    return 128;
  if ((insn0 & 0x300000f) == 0x100000f)
    return 64;
  if ((insn0 & 0x300000f) == 0x100000f)
    return 64;
  if ((insn0 & 0x300000f) == 0x100000f)
    return 64;
  return 0;
}

/* xtimm_*_immediate_field_set sets immediate field of CONST16 instruction
   in insn buffer at slot. Only checks necessary for figuring out which format
   this is are performed. The small version sets CONST16 immediate field in
   formats up to 32 bits in size. Large version operates only on formats larger
   than 32 bit. */

static uint32_t
xtimm_fmt_small_immediate_field_set (uint32_t *insn, int slot, uint32_t value)
{
      insn[0] = ((value << 16) >> 8) | (insn[0] & ~0xFFFF00);
      return 1;
}

static uint32_t
xtimm_fmt_large_immediate_field_set (uint32_t *insn, int slot, uint32_t value)
{
  if ((insn[0] & 0x100000f) == 0xf && (insn[3] & 0x410c0000) == 0x41000000) { /* F0 */
      insn[0] = ((value << 24) >> 16) | (((value << 23) >> 31) << 26) | (insn[0] & ~0x400FF00);
      insn[1] = (((value << 21) >> 31) << 29) | (insn[1] & ~0x20000000);
      insn[2] = (((value << 18) >> 31) << 16) | (((value << 19) >> 30) << 17) | (((value << 17) >> 31) << 19) | (((value << 22) >> 31) << 20) | (((value << 16) >> 31) << 22) | (insn[2] & ~0x5F0000);
      return 1;
  } else if ((insn[0] & 0x100000f) == 0xf && (insn[3] & 0x40000000) == 0) { /* F11 */
      insn[0] = ((value << 24) >> 16) | (insn[0] & ~0xFF00);
      insn[1] = (((value << 21) >> 29) << 25) | (((value << 20) >> 31) << 29) | (insn[1] & ~0x2E000000);
      insn[2] = (((value << 19) >> 31) << 13) | (((value << 17) >> 30) << 17) | (((value << 16) >> 31) << 30) | (insn[2] & ~0x40062000);
      return 1;
  } else if ((insn[0] & 0x100000f) == 0xf && (insn[3] & 0x410e0000) == 0x41040000) { /* F1 */
      insn[0] = ((value << 24) >> 16) | (((value << 23) >> 31) << 26) | (insn[0] & ~0x400FF00);
      insn[1] = (((value << 22) >> 31) << 3) | (((value << 21) >> 31) << 29) | (insn[1] & ~0x20000008);
      insn[2] = (((value << 16) >> 27) << 15) | (insn[2] & ~0xF8000);
      return 1;
  } else if ((insn[0] & 0x100000f) == 0xf && (insn[3] & 0x410e8000) == 0x41080000) { /* F2 */
      insn[0] = ((value << 24) >> 16) | (insn[0] & ~0xFF00);
      insn[1] = (((value << 17) >> 30) << 29) | (insn[1] & ~0x60000000);
      insn[2] = (((value << 19) >> 28) << 15) | (((value << 16) >> 31) << 19) | (((value << 23) >> 31) << 20) | (insn[2] & ~0x1F8000);
      return 1;
  } else if ((insn[0] & 0xf) == 0xe) { /* F3 */
      insn[0] = ((value << 24) >> 16) | (((value << 23) >> 31) << 26) | (insn[0] & ~0x400FF00);
      insn[1] = (((value << 21) >> 31) << 29) | (insn[1] & ~0x20000000);
      insn[2] = (((value << 22) >> 31) << 14) | (((value << 19) >> 30) << 17) | ((value >> 13) << 30) | (insn[2] & ~0xC0064000);
      insn[3] = ((value << 16) >> 31) | (insn[3] & ~0x1);
      return 1;
  } else if ((insn[0] & 0x100000f) == 0xf && (insn[3] & 0x41000000) == 0x40000000) { /* F4 */
      insn[0] = ((value << 24) >> 16) | (((value << 22) >> 31) << 26) | (insn[0] & ~0x400FF00);
      insn[1] = (((value << 21) >> 31) << 29) | (insn[1] & ~0x20000000);
      insn[2] = (((value << 23) >> 31) << 14) | (insn[2] & ~0x4000);
      insn[3] = ((value << 16) >> 27) | (insn[3] & ~0x1F);
      return 1;
  } else if ((insn[0] & 0x100000f) == 0xf && (insn[3] & 0x410e8000) == 0x41088000) { /* F6 */
      insn[0] = ((value << 24) >> 16) | (insn[0] & ~0xFF00);
      insn[1] = (((value << 17) >> 30) << 29) | (insn[1] & ~0x60000000);
      insn[2] = (((value << 16) >> 31) << 7) | (((value << 19) >> 28) << 15) | (((value << 23) >> 31) << 20) | (insn[2] & ~0x178080);
      return 1;
  } else if ((insn[0] & 0x100000f) == 0xf && (insn[3] & 0x410e0000) == 0x41060000) { /* F7 */
      insn[0] = ((value << 24) >> 16) | (((value << 23) >> 31) << 26) | (insn[0] & ~0x400FF00);
      insn[1] = (((value << 22) >> 31) << 3) | (((value << 21) >> 31) << 29) | (insn[1] & ~0x20000008);
      insn[2] = (((value << 17) >> 30) << 15) | (((value << 19) >> 30) << 17) | (((value << 16) >> 31) << 19) | (insn[2] & ~0xF8000);
      return 1;
  } else if ((insn[0] & 0x300000f) == 0x100000f && (insn[1] & 0x42000000) == 0x40000000) { /* N1 */
      insn[0] = ((value << 23) >> 15) | (insn[0] & ~0x1FF00);
      insn[1] = (((value << 21) >> 30) << 1) | (((value << 19) >> 30) << 5) | (((value << 16) >> 29) << 9) | (insn[1] & ~0xE66);
      return 1;
  } else if ((insn[0] & 0x300000f) == 0x100000f && (insn[1] & 0x42000000) == 0x42000000) { /* N2 */
      insn[0] = ((value << 24) >> 16) | (((value << 23) >> 31) << 26) | (insn[0] & ~0x400FF00);
      insn[1] = (((value << 19) >> 30) << 1) | (((value << 21) >> 30) << 3) | (((value << 17) >> 30) << 5) | (((value << 16) >> 31) << 8) | (insn[1] & ~0x17E);
      return 1;
  } else if ((insn[0] & 0x300000f) == 0x100000f && (insn[1] & 0x40000000) == 0) { /* N0 */
      insn[0] = ((value << 23) >> 15) | (insn[0] & ~0x1FF00);
      insn[1] = (((value << 21) >> 30) << 1) | (((value << 16) >> 27) << 18) | (insn[1] & ~0x7C0006);
      return 1;
  }
  return 0;
}

