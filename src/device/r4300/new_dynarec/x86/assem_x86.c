/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - assem_x86.c                                             *
 *   Copyright (C) 2009-2011 Ari64                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void jump_vaddr_eax(void);
void jump_vaddr_ecx(void);
void jump_vaddr_edx(void);
void jump_vaddr_ebx(void);
void jump_vaddr_ebp(void);
void jump_vaddr_edi(void);
void invalidate_block_eax(void);
void invalidate_block_ecx(void);
void invalidate_block_edx(void);
void invalidate_block_ebx(void);
void invalidate_block_ebp(void);
void invalidate_block_esi(void);
void invalidate_block_edi(void);

// We need these for cmovcc instructions on x86
static const u_int const_zero=0;
static const u_int const_one=1;

static const u_int jump_vaddr_reg[8] = {
  (int)jump_vaddr_eax,
  (int)jump_vaddr_ecx,
  (int)jump_vaddr_edx,
  (int)jump_vaddr_ebx,
  0,
  (int)jump_vaddr_ebp,
  0,
  (int)jump_vaddr_edi };

static const u_int invalidate_block_reg[8] = {
  (int)invalidate_block_eax,
  (int)invalidate_block_ecx,
  (int)invalidate_block_edx,
  (int)invalidate_block_ebx,
  0,
  (int)invalidate_block_ebp,
  (int)invalidate_block_esi,
  (int)invalidate_block_edi };

/* Linker */
static void set_jump_target(int addr,int target)
{
  u_char *ptr=(u_char *)addr;
  if(*ptr==0x0f)
  {
    assert(ptr[1]>=0x80&&ptr[1]<=0x8f);
    u_int *ptr2=(u_int *)(ptr+2);
    *ptr2=target-(int)ptr2-4;
  }
  else if(*ptr==0xe8||*ptr==0xe9) {
    u_int *ptr2=(u_int *)(ptr+1);
    *ptr2=target-(int)ptr2-4;
  }
  else
  {
    assert(*ptr==0xc7); /* mov immediate (store address) */
    u_int *ptr2=(u_int *)(ptr+6);
    *ptr2=target;
  }
}

static void *add_pointer(void *src, void* addr)
{
  int *ptr=(int*)src;
  int *ptr2=(int*)((u_int)ptr+(u_int)*ptr+4);
  assert((*ptr2&0xFF)==0x68);                   //push
  assert((*(int*)((u_int)ptr2+5)&0xFF)==0x68);  //push
  u_int offset=(u_int)addr-(u_int)ptr-4;
  *ptr=offset;
  return (void*)ptr2;
}
static void *kill_pointer(void *stub)
{
  int *i_ptr=*((int **)((int)stub+6));
  *i_ptr=(int)stub-(int)i_ptr-4;
  return i_ptr;
}
static int get_pointer(void *stub)
{
  int *i_ptr=*((int **)((int)stub+6));
  return *i_ptr+(int)i_ptr+4;
}

/* Register allocation */

// Note: registers are allocated clean (unmodified state)
// if you intend to modify the register, you must call dirty_reg().
static void alloc_reg(struct regstat *cur,int i,signed char reg)
{
  int r,hr;
  int preferred_reg = (reg&3)+(reg>28)*4-(reg==32)+2*(reg==36)-(reg==40);

  // Don't allocate unused registers
  if((cur->u>>reg)&1) return;

  // see if it's already allocated
  for(hr=0;hr<HOST_REGS;hr++)
  {
    if(cur->regmap[hr]==reg) return;
  }

  // Keep the same mapping if the register was already allocated in a loop
  preferred_reg = loop_reg(i,reg,preferred_reg);

  // Try to allocate the preferred register
  if(cur->regmap[preferred_reg]==-1) {
    cur->regmap[preferred_reg]=reg;
    cur->dirty&=~(1<<preferred_reg);
    cur->isconst&=~(1<<preferred_reg);
    return;
  }
  r=cur->regmap[preferred_reg];
  if(r<64&&((cur->u>>r)&1)) {
    cur->regmap[preferred_reg]=reg;
    cur->dirty&=~(1<<preferred_reg);
    cur->isconst&=~(1<<preferred_reg);
    return;
  }
  if(r>=64&&((cur->uu>>(r&63))&1)) {
    cur->regmap[preferred_reg]=reg;
    cur->dirty&=~(1<<preferred_reg);
    cur->isconst&=~(1<<preferred_reg);
    return;
  }

  // Try to allocate EAX, EBX, ECX, or EDX
  // We prefer these because they can do byte and halfword loads
  for(hr=0;hr<4;hr++) {
    if(cur->regmap[hr]==-1) {
      cur->regmap[hr]=reg;
      cur->dirty&=~(1<<hr);
      cur->isconst&=~(1<<hr);
      return;
    }
  }

  // Clear any unneeded registers
  // We try to keep the mapping consistent, if possible, because it
  // makes branches easier (especially loops).  So we try to allocate
  // first (see above) before removing old mappings.  If this is not
  // possible then go ahead and clear out the registers that are no
  // longer needed.
  for(hr=0;hr<HOST_REGS;hr++)
  {
    r=cur->regmap[hr];
    if(r>=0) {
      if(r<64) {
        if((cur->u>>r)&1)
          if(i==0||(unneeded_reg[i-1]>>r)&1) {cur->regmap[hr]=-1;break;}
      }
      else
      {
        if((cur->uu>>(r&63))&1)
          if(i==0||(unneeded_reg_upper[i-1]>>(r&63))&1) {cur->regmap[hr]=-1;break;}
      }
    }
  }
  // Try to allocate any available register, but prefer
  // registers that have not been used recently.
  if(i>0) {
    for(hr=0;hr<HOST_REGS;hr++) {
      if(hr!=EXCLUDE_REG&&cur->regmap[hr]==-1) {
        if(regs[i-1].regmap[hr]!=rs1[i-1]&&regs[i-1].regmap[hr]!=rs2[i-1]&&regs[i-1].regmap[hr]!=rt1[i-1]&&regs[i-1].regmap[hr]!=rt2[i-1]) {
          cur->regmap[hr]=reg;
          cur->dirty&=~(1<<hr);
          cur->isconst&=~(1<<hr);
          return;
        }
      }
    }
  }
  // Try to allocate any available register
  for(hr=0;hr<HOST_REGS;hr++) {
    if(hr!=EXCLUDE_REG&&cur->regmap[hr]==-1) {
      cur->regmap[hr]=reg;
      cur->dirty&=~(1<<hr);
      cur->isconst&=~(1<<hr);
      return;
    }
  }

  // Ok, now we have to evict someone
  // Pick a register we hopefully won't need soon
  u_char hsn[MAXREG+1];
  memset(hsn,10,sizeof(hsn));
  int j;
  lsn(hsn,i,&preferred_reg);
  if(i>0) {
    // Don't evict the cycle count at entry points, otherwise the entry
    // stub will have to write it.
    if(bt[i]&&hsn[CCREG]>2) hsn[CCREG]=2;
    if(i>1&&hsn[CCREG]>2&&(itype[i-2]==RJUMP||itype[i-2]==UJUMP||itype[i-2]==CJUMP||itype[i-2]==SJUMP||itype[i-2]==FJUMP)) hsn[CCREG]=2;
    for(j=10;j>=3;j--)
    {
      // Alloc preferred register if available
      if(hsn[r=cur->regmap[preferred_reg]&63]==j) {
        for(hr=0;hr<HOST_REGS;hr++) {
          // Evict both parts of a 64-bit register
          if((cur->regmap[hr]&63)==r) {
            cur->regmap[hr]=-1;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
          }
        }
        cur->regmap[preferred_reg]=reg;
        return;
      }
      for(r=1;r<=MAXREG;r++)
      {
        if(hsn[r]==j&&r!=rs1[i-1]&&r!=rs2[i-1]&&r!=rt1[i-1]&&r!=rt2[i-1]) {
          for(hr=0;hr<HOST_REGS;hr++) {
            if(hr!=HOST_CCREG||j<hsn[CCREG]) {
              if(cur->regmap[hr]==r+64) {
                cur->regmap[hr]=reg;
                cur->dirty&=~(1<<hr);
                cur->isconst&=~(1<<hr);
                return;
              }
            }
          }
          for(hr=0;hr<HOST_REGS;hr++) {
            if(hr!=HOST_CCREG||j<hsn[CCREG]) {
              if(cur->regmap[hr]==r) {
                cur->regmap[hr]=reg;
                cur->dirty&=~(1<<hr);
                cur->isconst&=~(1<<hr);
                return;
              }
            }
          }
        }
      }
    }
  }
  for(j=10;j>=0;j--)
  {
    for(r=1;r<=MAXREG;r++)
    {
      if(hsn[r]==j) {
        for(hr=0;hr<HOST_REGS;hr++) {
          if(cur->regmap[hr]==r+64) {
            cur->regmap[hr]=reg;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
            return;
          }
        }
        for(hr=0;hr<HOST_REGS;hr++) {
          if(cur->regmap[hr]==r) {
            cur->regmap[hr]=reg;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
            return;
          }
        }
      }
    }
  }
  DebugMessage(M64MSG_ERROR, "This shouldn't happen (alloc_reg)");exit(1);
}

static void alloc_reg64(struct regstat *cur,int i,signed char reg)
{
  int preferred_reg = 5+reg%3;
  int r,hr;

  // allocate the lower 32 bits
  alloc_reg(cur,i,reg);

  // Don't allocate unused registers
  if((cur->uu>>reg)&1) return;

  // see if the upper half is already allocated
  for(hr=0;hr<HOST_REGS;hr++)
  {
    if(cur->regmap[hr]==reg+64) return;
  }

  // Keep the same mapping if the register was already allocated in a loop
  preferred_reg = loop_reg(i,reg,preferred_reg);

  // Try to allocate the preferred register
  if(cur->regmap[preferred_reg]==-1) {
    cur->regmap[preferred_reg]=reg|64;
    cur->dirty&=~(1<<preferred_reg);
    cur->isconst&=~(1<<preferred_reg);
    return;
  }
  r=cur->regmap[preferred_reg];
  if(r<64&&((cur->u>>r)&1)) {
    cur->regmap[preferred_reg]=reg|64;
    cur->dirty&=~(1<<preferred_reg);
    cur->isconst&=~(1<<preferred_reg);
    return;
  }
  if(r>=64&&((cur->uu>>(r&63))&1)) {
    cur->regmap[preferred_reg]=reg|64;
    cur->dirty&=~(1<<preferred_reg);
    cur->isconst&=~(1<<preferred_reg);
    return;
  }

  // Try to allocate EBP, ESI or EDI
  for(hr=5;hr<8;hr++) {
    if(cur->regmap[hr]==-1) {
      cur->regmap[hr]=reg|64;
      cur->dirty&=~(1<<hr);
      cur->isconst&=~(1<<hr);
      return;
    }
  }

  // Clear any unneeded registers
  // We try to keep the mapping consistent, if possible, because it
  // makes branches easier (especially loops).  So we try to allocate
  // first (see above) before removing old mappings.  If this is not
  // possible then go ahead and clear out the registers that are no
  // longer needed.
  for(hr=HOST_REGS-1;hr>=0;hr--)
  {
    r=cur->regmap[hr];
    if(r>=0) {
      if(r<64) {
        if((cur->u>>r)&1) {cur->regmap[hr]=-1;break;}
      }
      else
      {
        if((cur->uu>>(r&63))&1) {cur->regmap[hr]=-1;break;}
      }
    }
  }
  // Try to allocate any available register, but prefer
  // registers that have not been used recently.
  if(i>0) {
    for(hr=0;hr<HOST_REGS;hr++) {
      if(hr!=EXCLUDE_REG&&cur->regmap[hr]==-1) {
        if(regs[i-1].regmap[hr]!=rs1[i-1]&&regs[i-1].regmap[hr]!=rs2[i-1]&&regs[i-1].regmap[hr]!=rt1[i-1]&&regs[i-1].regmap[hr]!=rt2[i-1]) {
          cur->regmap[hr]=reg|64;
          cur->dirty&=~(1<<hr);
          cur->isconst&=~(1<<hr);
          return;
        }
      }
    }
  }
  // Try to allocate any available register
  for(hr=0;hr<HOST_REGS;hr++) {
    if(hr!=EXCLUDE_REG&&cur->regmap[hr]==-1) {
      cur->regmap[hr]=reg|64;
      cur->dirty&=~(1<<hr);
      cur->isconst&=~(1<<hr);
      return;
    }
  }

  // Ok, now we have to evict someone
  // Pick a register we hopefully won't need soon
  u_char hsn[MAXREG+1];
  memset(hsn,10,sizeof(hsn));
  int j;
  lsn(hsn,i,&preferred_reg);
  if(i>0) {
    // Don't evict the cycle count at entry points, otherwise the entry
    // stub will have to write it.
    if(bt[i]&&hsn[CCREG]>2) hsn[CCREG]=2;
    if(i>1&&hsn[CCREG]>2&&(itype[i-2]==RJUMP||itype[i-2]==UJUMP||itype[i-2]==CJUMP||itype[i-2]==SJUMP||itype[i-2]==FJUMP)) hsn[CCREG]=2;
    for(j=10;j>=3;j--)
    {
      // Alloc preferred register if available
      if(hsn[r=cur->regmap[preferred_reg]&63]==j) {
        for(hr=0;hr<HOST_REGS;hr++) {
          // Evict both parts of a 64-bit register
          if((cur->regmap[hr]&63)==r) {
            cur->regmap[hr]=-1;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
          }
        }
        cur->regmap[preferred_reg]=reg|64;
        return;
      }
      for(r=1;r<=MAXREG;r++)
      {
        if(hsn[r]==j&&r!=rs1[i-1]&&r!=rs2[i-1]&&r!=rt1[i-1]&&r!=rt2[i-1]) {
          for(hr=0;hr<HOST_REGS;hr++) {
            if(hr!=HOST_CCREG||j<hsn[CCREG]) {
              if(cur->regmap[hr]==r+64) {
                cur->regmap[hr]=reg|64;
                cur->dirty&=~(1<<hr);
                cur->isconst&=~(1<<hr);
                return;
              }
            }
          }
          for(hr=0;hr<HOST_REGS;hr++) {
            if(hr!=HOST_CCREG||j<hsn[CCREG]) {
              if(cur->regmap[hr]==r) {
                cur->regmap[hr]=reg|64;
                cur->dirty&=~(1<<hr);
                cur->isconst&=~(1<<hr);
                return;
              }
            }
          }
        }
      }
    }
  }
  for(j=10;j>=0;j--)
  {
    for(r=1;r<=MAXREG;r++)
    {
      if(hsn[r]==j) {
        for(hr=0;hr<HOST_REGS;hr++) {
          if(cur->regmap[hr]==r+64) {
            cur->regmap[hr]=reg|64;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
            return;
          }
        }
        for(hr=0;hr<HOST_REGS;hr++) {
          if(cur->regmap[hr]==r) {
            cur->regmap[hr]=reg|64;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
            return;
          }
        }
      }
    }
  }
  DebugMessage(M64MSG_ERROR, "This shouldn't happen");exit(1);
}

// Allocate a temporary register.  This is done without regard to
// dirty status or whether the register we request is on the unneeded list
// Note: This will only allocate one register, even if called multiple times
static void alloc_reg_temp(struct regstat *cur,int i,signed char reg)
{
  int r,hr;
  int preferred_reg = -1;

  // see if it's already allocated
  for(hr=0;hr<HOST_REGS;hr++)
  {
    if(hr!=EXCLUDE_REG&&cur->regmap[hr]==reg) return;
  }

  // Try to allocate any available register, starting with EDI, ESI, EBP...
  // We prefer EDI, ESI, EBP since the others are used for byte/halfword stores
  for(hr=HOST_REGS-1;hr>=0;hr--) {
    if(hr!=EXCLUDE_REG&&cur->regmap[hr]==-1) {
      cur->regmap[hr]=reg;
      cur->dirty&=~(1<<hr);
      cur->isconst&=~(1<<hr);
      return;
    }
  }

  // Find an unneeded register
  for(hr=HOST_REGS-1;hr>=0;hr--)
  {
    r=cur->regmap[hr];
    if(r>=0) {
      if(r<64) {
        if((cur->u>>r)&1) {
          if(i==0||((unneeded_reg[i-1]>>r)&1)) {
            cur->regmap[hr]=reg;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
            return;
          }
        }
      }
      else
      {
        if((cur->uu>>(r&63))&1) {
          if(i==0||((unneeded_reg_upper[i-1]>>(r&63))&1)) {
            cur->regmap[hr]=reg;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
            return;
          }
        }
      }
    }
  }

  // Ok, now we have to evict someone
  // Pick a register we hopefully won't need soon
  // TODO: we might want to follow unconditional jumps here
  // TODO: get rid of dupe code and make this into a function
  u_char hsn[MAXREG+1];
  memset(hsn,10,sizeof(hsn));
  int j;
  lsn(hsn,i,&preferred_reg);
  if(i>0) {
    // Don't evict the cycle count at entry points, otherwise the entry
    // stub will have to write it.
    if(bt[i]&&hsn[CCREG]>2) hsn[CCREG]=2;
    if(i>1&&hsn[CCREG]>2&&(itype[i-2]==RJUMP||itype[i-2]==UJUMP||itype[i-2]==CJUMP||itype[i-2]==SJUMP||itype[i-2]==FJUMP)) hsn[CCREG]=2;
    for(j=10;j>=3;j--)
    {
      for(r=1;r<=MAXREG;r++)
      {
        if(hsn[r]==j&&r!=rs1[i-1]&&r!=rs2[i-1]&&r!=rt1[i-1]&&r!=rt2[i-1]) {
          for(hr=0;hr<HOST_REGS;hr++) {
            if(hr!=HOST_CCREG||hsn[CCREG]>2) {
              if(cur->regmap[hr]==r+64) {
                cur->regmap[hr]=reg;
                cur->dirty&=~(1<<hr);
                cur->isconst&=~(1<<hr);
                return;
              }
            }
          }
          for(hr=0;hr<HOST_REGS;hr++) {
            if(hr!=HOST_CCREG||hsn[CCREG]>2) {
              if(cur->regmap[hr]==r) {
                cur->regmap[hr]=reg;
                cur->dirty&=~(1<<hr);
                cur->isconst&=~(1<<hr);
                return;
              }
            }
          }
        }
      }
    }
  }
  for(j=10;j>=0;j--)
  {
    for(r=1;r<=MAXREG;r++)
    {
      if(hsn[r]==j) {
        for(hr=0;hr<HOST_REGS;hr++) {
          if(cur->regmap[hr]==r+64) {
            cur->regmap[hr]=reg;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
            return;
          }
        }
        for(hr=0;hr<HOST_REGS;hr++) {
          if(cur->regmap[hr]==r) {
            cur->regmap[hr]=reg;
            cur->dirty&=~(1<<hr);
            cur->isconst&=~(1<<hr);
            return;
          }
        }
      }
    }
  }
  DebugMessage(M64MSG_ERROR, "This shouldn't happen");exit(1);
}
// Allocate a specific x86 register.
static void alloc_x86_reg(struct regstat *cur,int i,signed char reg,int hr)
{
  int n;
  int dirty=0;

  // see if it's already allocated (and dealloc it)
  for(n=0;n<HOST_REGS;n++)
  {
    if(n!=ESP&&cur->regmap[n]==reg) {
      dirty=(cur->dirty>>n)&1;
      cur->regmap[n]=-1;
    }
  }

  cur->regmap[hr]=reg;
  cur->dirty&=~(1<<hr);
  cur->dirty|=dirty<<hr;
  cur->isconst&=~(1<<hr);
}

// Alloc cycle count into dedicated register
static void alloc_cc(struct regstat *cur,int i)
{
  alloc_x86_reg(cur,i,CCREG,HOST_CCREG);
}

/* Special alloc */

static void multdiv_alloc_x86(struct regstat *current,int i)
{
  //  case 0x18: MULT
  //  case 0x19: MULTU
  //  case 0x1A: DIV
  //  case 0x1B: DIVU
  //  case 0x1C: DMULT
  //  case 0x1D: DMULTU
  //  case 0x1E: DDIV
  //  case 0x1F: DDIVU
  clear_const(current,rs1[i]);
  clear_const(current,rs2[i]);
  if(rs1[i]&&rs2[i])
  {
    if((opcode2[i]&4)==0) // 32-bit
    {
#ifdef INTERPRET_MULT
      if((opcode2[i]==0x18) || (opcode2[i]==0x19)) { //MULT/MULTU
        alloc_reg(current,i,HIREG);
        alloc_reg(current,i,LOREG);
      }
      else
#endif
#ifdef INTERPRET_DIV
      if((opcode2[i]==0x1A) || (opcode2[i]==0x1B)) { //DIV/DIVU
        alloc_reg(current,i,HIREG);
        alloc_reg(current,i,LOREG);
      }
      else
#endif
      {
        current->u&=~(1LL<<HIREG);
        current->u&=~(1LL<<LOREG);
        alloc_x86_reg(current,i,HIREG,EDX);
        alloc_x86_reg(current,i,LOREG,EAX);
      }
      alloc_reg(current,i,rs1[i]);
      alloc_reg(current,i,rs2[i]);
      current->is32|=1LL<<HIREG;
      current->is32|=1LL<<LOREG;
      dirty_reg(current,HIREG);
      dirty_reg(current,LOREG);
    }
    else // 64-bit
    {
#ifndef INTERPRET_MULT64
      if((opcode2[i]==0x1C)||(opcode2[i]==0x1D)) // DMULT/DMULTU
      {
        current->u&=~(1LL<<HIREG);
        current->uu&=~(1LL<<HIREG);
        alloc_x86_reg(current,i,HIREG|64,EDX);
        alloc_x86_reg(current,i,HIREG,EAX);
        alloc_reg64(current,i,rs1[i]);
        alloc_reg64(current,i,rs2[i]);
        alloc_reg_temp(current,i,-1);
        current->is32&=~(1LL<<HIREG);
        current->is32&=~(1LL<<LOREG);
        dirty_reg(current,HIREG);
        dirty_reg(current,LOREG);
        minimum_free_regs[i]=1;
      }
      else
#endif
      {
        // avoid running out of registers
        if(((current->u>>LOREG)&1)||((current->uu>>LOREG)&1))
          alloc_reg64(current,i,HIREG);
        else
          alloc_reg(current,i,HIREG);

        alloc_reg64(current,i,LOREG);
        alloc_reg64(current,i,rs1[i]);
        alloc_reg64(current,i,rs2[i]);
        current->is32&=~(1LL<<HIREG);
        current->is32&=~(1LL<<LOREG);
        dirty_reg(current,HIREG);
        dirty_reg(current,LOREG);
      }
    }
  }
  else
  {
    // Multiply by zero is zero.
    // MIPS does not have a divide by zero exception.
    // The result is undefined, we return zero.
    alloc_reg(current,i,HIREG);
    alloc_reg(current,i,LOREG);
    current->is32|=1LL<<HIREG;
    current->is32|=1LL<<LOREG;
    dirty_reg(current,HIREG);
    dirty_reg(current,LOREG);
  }
}
#define multdiv_alloc multdiv_alloc_x86

/* Assembler */

static const char regname[8][4] = {
 "eax",
 "ecx",
 "edx",
 "ebx",
 "esp",
 "ebp",
 "esi",
 "edi"};

static void output_byte(u_char byte)
{
  *(out++)=byte;
}
static void output_modrm(u_char mod,u_char rm,u_char ext)
{
  assert(mod<4);
  assert(rm<8);
  assert(ext<8);
  u_char byte=(mod<<6)|(ext<<3)|rm;
  *(out++)=byte;
}
static void output_sib(u_char scale,u_char index,u_char base)
{
  assert(scale<4);
  assert(index<8);
  assert(base<8);
  u_char byte=(scale<<6)|(index<<3)|base;
  *(out++)=byte;
}
static void output_w32(u_int word)
{
  *((u_int *)out)=word;
  out+=4;
}

static void emit_mov(int rs,int rt)
{
  assem_debug("mov %%%s,%%%s",regname[rs],regname[rt]);
  output_byte(0x89);
  output_modrm(3,rt,rs);
}

static void emit_add(int rs1,int rs2,int rt)
{
  if(rs1==rt) {
    assem_debug("add %%%s,%%%s",regname[rs2],regname[rs1]);
    output_byte(0x01);
    output_modrm(3,rs1,rs2);
  }else if(rs2==rt) {
    assem_debug("add %%%s,%%%s",regname[rs1],regname[rs2]);
    output_byte(0x01);
    output_modrm(3,rs2,rs1);
  }else {
    assem_debug("mov %%%s,%%%s",regname[rs1],regname[rt]);
    output_byte(0x89);
    output_modrm(3,rt,rs1);
    assem_debug("add %%%s,%%%s",regname[rs2],regname[rt]);
    output_byte(0x01);
    output_modrm(3,rt,rs2);
  }
}

static void emit_adc(int rs1,int rs2,int rt)
{
  if(rs1==rt) {
    assem_debug("adc %%%s,%%%s",regname[rs2],regname[rs1]);
    output_byte(0x11);
    output_modrm(3,rs1,rs2);
  }else if(rs2==rt) {
    assem_debug("adc %%%s,%%%s",regname[rs1],regname[rs2]);
    output_byte(0x11);
    output_modrm(3,rs2,rs1);
  }else {
    assem_debug("mov %%%s,%%%s",regname[rs1],regname[rt]);
    output_byte(0x89);
    output_modrm(3,rt,rs1);
    assem_debug("adc %%%s,%%%s",regname[rs2],regname[rt]);
    output_byte(0x11);
    output_modrm(3,rt,rs2);
  }
}

static void emit_adds(int rs1,int rs2,int rt)
{
  emit_add(rs1,rs2,rt);
}

static void emit_lea8(int rs1,int rt)
{
  assem_debug("lea 0(%%%s,8),%%%s",regname[rs1],regname[rt]);
  output_byte(0x8D);
  output_modrm(0,4,rt);
  output_sib(3,rs1,5);
  output_w32(0);
}
static void emit_leairrx1(int imm,int rs1,int rs2,int rt)
{
  assem_debug("lea %x(%%%s,%%%s,1),%%%s",imm,regname[rs1],regname[rs2],regname[rt]);
  output_byte(0x8D);
  if(imm!=0||rs1==EBP) {
    output_modrm(2,4,rt);
    output_sib(0,rs2,rs1);
    output_w32(imm);
  }else{
    output_modrm(0,4,rt);
    output_sib(0,rs2,rs1);
  }
}
static void emit_leairrx4(int imm,int rs1,int rs2,int rt)
{
  assem_debug("lea %x(%%%s,%%%s,4),%%%s",imm,regname[rs1],regname[rs2],regname[rt]);
  output_byte(0x8D);
  if(imm!=0||rs1==EBP) {
    output_modrm(2,4,rt);
    output_sib(2,rs2,rs1);
    output_w32(imm);
  }else{
    output_modrm(0,4,rt);
    output_sib(2,rs2,rs1);
  }
}

static void emit_neg(int rs, int rt)
{
  if(rs!=rt) emit_mov(rs,rt);
  assem_debug("neg %%%s",regname[rt]);
  output_byte(0xF7);
  output_modrm(3,rt,3);
}

static void emit_negs(int rs, int rt)
{
  emit_neg(rs,rt);
}

static void emit_sub(int rs1,int rs2,int rt)
{
  if(rs1==rt) {
    assem_debug("sub %%%s,%%%s",regname[rs2],regname[rs1]);
    output_byte(0x29);
    output_modrm(3,rs1,rs2);
  } else if(rs2==rt) {
    emit_neg(rs2,rs2);
    emit_add(rs2,rs1,rs2);
  } else {
    emit_mov(rs1,rt);
    emit_sub(rt,rs2,rt);
  }
}

static void emit_subs(int rs1,int rs2,int rt)
{
  emit_sub(rs1,rs2,rt);
}

static void emit_zeroreg(int rt)
{
  output_byte(0x31);
  output_modrm(3,rt,rt);
  assem_debug("xor %%%s,%%%s",regname[rt],regname[rt]);
}

static void emit_loadreg(int r, int hr)
{
  if((r&63)==0)
    emit_zeroreg(hr);
  else {
    int addr=((int)g_dev.r4300.new_dynarec_hot_state.regs)+((r&63)<<3)+((r&64)>>4);
    if((r&63)==HIREG) addr=(int)&g_dev.r4300.new_dynarec_hot_state.hi+((r&64)>>4);
    if((r&63)==LOREG) addr=(int)&g_dev.r4300.new_dynarec_hot_state.lo+((r&64)>>4);
    if(r==CCREG) addr=(int)&g_dev.r4300.new_dynarec_hot_state.cycle_count;
    if(r==CSREG) addr=(int)&g_dev.r4300.new_dynarec_hot_state.cp0_regs[CP0_STATUS_REG];
    if(r==FSREG) addr=(int)&g_dev.r4300.new_dynarec_hot_state.fcr31;
    assem_debug("mov %x+%d,%%%s",addr,r,regname[hr]);
    output_byte(0x8B);
    output_modrm(0,5,hr);
    output_w32(addr);
  }
}
static void emit_storereg(int r, int hr)
{
  int addr=((int)g_dev.r4300.new_dynarec_hot_state.regs)+((r&63)<<3)+((r&64)>>4);
  if((r&63)==HIREG) addr=(int)&g_dev.r4300.new_dynarec_hot_state.hi+((r&64)>>4);
  if((r&63)==LOREG) addr=(int)&g_dev.r4300.new_dynarec_hot_state.lo+((r&64)>>4);
  if(r==CCREG) addr=(int)&g_dev.r4300.new_dynarec_hot_state.cycle_count;
  if(r==FSREG) addr=(int)&g_dev.r4300.new_dynarec_hot_state.fcr31;
  assert((r&63)!=CSREG);
  assert((r&63)!=0);
  assert((r&63)<=CCREG);
  assem_debug("mov %%%s,%x+%d",regname[hr],addr,r);
  output_byte(0x89);
  output_modrm(0,5,hr);
  output_w32(addr);
}

static void emit_test(int rs, int rt)
{
  assem_debug("test %%%s,%%%s",regname[rs],regname[rt]);
  output_byte(0x85);
  output_modrm(3,rs,rt);
}

static void emit_testimm(int rs,int imm)
{
  assem_debug("test $0x%x,%%%s",imm,regname[rs]);
  if(imm<128&&imm>=-128&&rs<4) {
    output_byte(0xF6);
    output_modrm(3,rs,0);
    output_byte(imm);
  }
  else
  {
    output_byte(0xF7);
    output_modrm(3,rs,0);
    output_w32(imm);
  }
}

static void emit_not(int rs,int rt)
{
  if(rs!=rt) emit_mov(rs,rt);
  assem_debug("not %%%s",regname[rt]);
  output_byte(0xF7);
  output_modrm(3,rt,2);
}

static void emit_and(u_int rs1,u_int rs2,u_int rt)
{
  assert(rs1<8);
  assert(rs2<8);
  assert(rt<8);
  if(rs1==rt) {
    assem_debug("and %%%s,%%%s",regname[rs2],regname[rt]);
    output_byte(0x21);
    output_modrm(3,rs1,rs2);
  }
  else
  if(rs2==rt) {
    assem_debug("and %%%s,%%%s",regname[rs1],regname[rt]);
    output_byte(0x21);
    output_modrm(3,rs2,rs1);
  }
  else {
    emit_mov(rs1,rt);
    emit_and(rt,rs2,rt);
  }
}

static void emit_or(u_int rs1,u_int rs2,u_int rt)
{
  assert(rs1<8);
  assert(rs2<8);
  assert(rt<8);
  if(rs1==rt) {
    assem_debug("or %%%s,%%%s",regname[rs2],regname[rt]);
    output_byte(0x09);
    output_modrm(3,rs1,rs2);
  }
  else
  if(rs2==rt) {
    assem_debug("or %%%s,%%%s",regname[rs1],regname[rt]);
    output_byte(0x09);
    output_modrm(3,rs2,rs1);
  }
  else {
    emit_mov(rs1,rt);
    emit_or(rt,rs2,rt);
  }
}
static void emit_or_and_set_flags(int rs1,int rs2,int rt)
{
  emit_or(rs1,rs2,rt);
}

static void emit_xor(u_int rs1,u_int rs2,u_int rt)
{
  assert(rs1<8);
  assert(rs2<8);
  assert(rt<8);
  if(rs1==rt) {
    assem_debug("xor %%%s,%%%s",regname[rs2],regname[rt]);
    output_byte(0x31);
    output_modrm(3,rs1,rs2);
  }
  else
  if(rs2==rt) {
    assem_debug("xor %%%s,%%%s",regname[rs1],regname[rt]);
    output_byte(0x31);
    output_modrm(3,rs2,rs1);
  }
  else {
    emit_mov(rs1,rt);
    emit_xor(rt,rs2,rt);
  }
}

static void emit_movimm(int imm,u_int rt)
{
  assem_debug("mov $%d,%%%s",imm,regname[rt]);
  assert(rt<8);
  output_byte(0xB8+rt);
  output_w32(imm);
}

static void emit_addimm(int rs,int imm,int rt)
{
  if(rs==rt) {
    if(imm!=0) {
      assem_debug("add $%d,%%%s",imm,regname[rt]);
      if(imm<128&&imm>=-128) {
        output_byte(0x83);
        output_modrm(3,rt,0);
        output_byte(imm);
      }
      else
      {
        output_byte(0x81);
        output_modrm(3,rt,0);
        output_w32(imm);
      }
    }
  }
  else {
    if(imm!=0) {
      assem_debug("lea %d(%%%s),%%%s",imm,regname[rs],regname[rt]);
      output_byte(0x8D);
      if(imm<128&&imm>=-128) {
        output_modrm(1,rs,rt);
        output_byte(imm);
      }else{
        output_modrm(2,rs,rt);
        output_w32(imm);
      }
    }else{
      emit_mov(rs,rt);
    }
  }
}

static void emit_addimm_and_set_flags(int imm,int rt)
{
  assem_debug("add $%d,%%%s",imm,regname[rt]);
  if(imm<128&&imm>=-128) {
    output_byte(0x83);
    output_modrm(3,rt,0);
    output_byte(imm);
  }
  else
  {
    output_byte(0x81);
    output_modrm(3,rt,0);
    output_w32(imm);
  }
}
static void emit_addimm_no_flags(int imm,int rt)
{
  if(imm!=0) {
    assem_debug("lea %d(%%%s),%%%s",imm,regname[rt],regname[rt]);
    output_byte(0x8D);
    if(imm<128&&imm>=-128) {
      output_modrm(1,rt,rt);
      output_byte(imm);
    }else{
      output_modrm(2,rt,rt);
      output_w32(imm);
    }
  }
}

static void emit_adcimm(int imm,u_int rt)
{
  assem_debug("adc $%d,%%%s",imm,regname[rt]);
  assert(rt<8);
  if(imm<128&&imm>=-128) {
    output_byte(0x83);
    output_modrm(3,rt,2);
    output_byte(imm);
  }
  else
  {
    output_byte(0x81);
    output_modrm(3,rt,2);
    output_w32(imm);
  }
}
static void emit_sbbimm(int imm,u_int rt)
{
  assem_debug("sbb $%d,%%%s",imm,regname[rt]);
  assert(rt<8);
  if(imm<128&&imm>=-128) {
    output_byte(0x83);
    output_modrm(3,rt,3);
    output_byte(imm);
  }
  else
  {
    output_byte(0x81);
    output_modrm(3,rt,3);
    output_w32(imm);
  }
}

static void emit_addimm64_32(int rsh,int rsl,int imm,int rth,int rtl)
{
  if(rsh==rth&&rsl==rtl) {
    assem_debug("add $%d,%%%s",imm,regname[rtl]);
    if(imm<128&&imm>=-128) {
      output_byte(0x83);
      output_modrm(3,rtl,0);
      output_byte(imm);
    }
    else
    {
      output_byte(0x81);
      output_modrm(3,rtl,0);
      output_w32(imm);
    }
    assem_debug("adc $%d,%%%s",imm>>31,regname[rth]);
    output_byte(0x83);
    output_modrm(3,rth,2);
    output_byte(imm>>31);
  }
  else {
    emit_mov(rsh,rth);
    emit_mov(rsl,rtl);
    emit_addimm64_32(rth,rtl,imm,rth,rtl);
  }
}

static void emit_sub64_32(int rs1l,int rs1h,int rs2l,int rs2h,int rtl,int rth)
{
  if((rs1l==rtl)&&(rs1h==rth)) {
    assem_debug("sub %%%s,%%%s",regname[rs2l],regname[rs1l]);
    output_byte(0x29);
    output_modrm(3,rs1l,rs2l);
    assem_debug("sbb %%%s,%%%s",regname[rs2h],regname[rs1h]);
    output_byte(0x19);
    output_modrm(3,rs1h,rs2h);
  } else if((rs2l==rtl)&&(rs2h==rth)) {
    emit_neg(rs2l,rs2l);
    emit_adcimm(-1,rs2h);
    assem_debug("add %%%s,%%%s",regname[rs1l],regname[rs2l]);
    output_byte(0x01);
    output_modrm(3,rs2l,rs1l);
    emit_not(rs2h,rs2h);
    assem_debug("adc %%%s,%%%s",regname[rs1h],regname[rs2h]);
    output_byte(0x11);
    output_modrm(3,rs2h,rs1h);
  } else {
    emit_mov(rs1l,rtl);
    assem_debug("sub %%%s,%%%s",regname[rs2l],regname[rtl]);
    output_byte(0x29);
    output_modrm(3,rtl,rs2l);
    emit_mov(rs1h,rth);
    assem_debug("sbb %%%s,%%%s",regname[rs2h],regname[rth]);
    output_byte(0x19);
    output_modrm(3,rth,rs2h);
  }
}

static void emit_sbb(int rs1,int rs2)
{
  assem_debug("sbb %%%s,%%%s",regname[rs1],regname[rs2]);
  output_byte(0x19);
  output_modrm(3,rs2,rs1);
}

static void emit_andimm(int rs,int imm,int rt)
{
  if(imm==0) {
    emit_zeroreg(rt);
  }
  else if(rs==rt) {
    assem_debug("and $%d,%%%s",imm,regname[rt]);
    if(imm<128&&imm>=-128) {
      output_byte(0x83);
      output_modrm(3,rt,4);
      output_byte(imm);
    }
    else
    {
      output_byte(0x81);
      output_modrm(3,rt,4);
      output_w32(imm);
    }
  }
  else {
    emit_mov(rs,rt);
    emit_andimm(rt,imm,rt);
  }
}

static void emit_orimm(int rs,int imm,int rt)
{
  if(rs==rt) {
    if(imm!=0) {
      assem_debug("or $%d,%%%s",imm,regname[rt]);
      if(imm<128&&imm>=-128) {
        output_byte(0x83);
        output_modrm(3,rt,1);
        output_byte(imm);
      }
      else
      {
        output_byte(0x81);
        output_modrm(3,rt,1);
        output_w32(imm);
      }
    }
  }
  else {
    emit_mov(rs,rt);
    emit_orimm(rt,imm,rt);
  }
}

static void emit_xorimm(int rs,int imm,int rt)
{
  if(rs==rt) {
    if(imm!=0) {
      assem_debug("xor $%d,%%%s",imm,regname[rt]);
      if(imm<128&&imm>=-128) {
        output_byte(0x83);
        output_modrm(3,rt,6);
        output_byte(imm);
      }
      else
      {
        output_byte(0x81);
        output_modrm(3,rt,6);
        output_w32(imm);
      }
    }
  }
  else {
    emit_mov(rs,rt);
    emit_xorimm(rt,imm,rt);
  }
}

static void emit_shlimm(int rs,u_int imm,int rt)
{
  if(rs==rt) {
    assem_debug("shl %%%s,%d",regname[rt],imm);
    assert(imm>0);
    if(imm==1) output_byte(0xD1);
    else output_byte(0xC1);
    output_modrm(3,rt,4);
    if(imm>1) output_byte(imm);
  }
  else {
    emit_mov(rs,rt);
    emit_shlimm(rt,imm,rt);
  }
}

static void emit_shrimm(int rs,u_int imm,int rt)
{
  if(rs==rt) {
    assem_debug("shr %%%s,%d",regname[rt],imm);
    assert(imm>0);
    if(imm==1) output_byte(0xD1);
    else output_byte(0xC1);
    output_modrm(3,rt,5);
    if(imm>1) output_byte(imm);
  }
  else {
    emit_mov(rs,rt);
    emit_shrimm(rt,imm,rt);
  }
}

static void emit_sarimm(int rs,u_int imm,int rt)
{
  if(rs==rt) {
    assem_debug("sar %%%s,%d",regname[rt],imm);
    assert(imm>0);
    if(imm==1) output_byte(0xD1);
    else output_byte(0xC1);
    output_modrm(3,rt,7);
    if(imm>1) output_byte(imm);
  }
  else {
    emit_mov(rs,rt);
    emit_sarimm(rt,imm,rt);
  }
}

static void emit_rorimm(int rs,u_int imm,int rt)
{
  if(rs==rt) {
    assem_debug("ror %%%s,%d",regname[rt],imm);
    assert(imm>0);
    if(imm==1) output_byte(0xD1);
    else output_byte(0xC1);
    output_modrm(3,rt,1);
    if(imm>1) output_byte(imm);
  }
  else {
    emit_mov(rs,rt);
    emit_rorimm(rt,imm,rt);
  }
}

static void emit_shldimm(int rs,int rs2,u_int imm,int rt)
{
  if(rs==rt) {
    assem_debug("shld %%%s,%%%s,%d",regname[rt],regname[rs2],imm);
    assert(imm>0);
    output_byte(0x0F);
    output_byte(0xA4);
    output_modrm(3,rt,rs2);
    output_byte(imm);
  }
  else {
    emit_mov(rs,rt);
    emit_shldimm(rt,rs2,imm,rt);
  }
}

static void emit_shrdimm(int rs,int rs2,u_int imm,int rt)
{
  if(rs==rt) {
    assem_debug("shrd %%%s,%%%s,%d",regname[rt],regname[rs2],imm);
    assert(imm>0);
    output_byte(0x0F);
    output_byte(0xAC);
    output_modrm(3,rt,rs2);
    output_byte(imm);
  }
  else {
    emit_mov(rs,rt);
    emit_shrdimm(rt,rs2,imm,rt);
  }
}

static void emit_shlcl(int r)
{
  assem_debug("shl %%%s,%%cl",regname[r]);
  output_byte(0xD3);
  output_modrm(3,r,4);
}
static void emit_shrcl(int r)
{
  assem_debug("shr %%%s,%%cl",regname[r]);
  output_byte(0xD3);
  output_modrm(3,r,5);
}
static void emit_sarcl(int r)
{
  assem_debug("sar %%%s,%%cl",regname[r]);
  output_byte(0xD3);
  output_modrm(3,r,7);
}

static void emit_shldcl(int r1,int r2)
{
  assem_debug("shld %%%s,%%%s,%%cl",regname[r1],regname[r2]);
  output_byte(0x0F);
  output_byte(0xA5);
  output_modrm(3,r1,r2);
}
static void emit_shrdcl(int r1,int r2)
{
  assem_debug("shrd %%%s,%%%s,%%cl",regname[r1],regname[r2]);
  output_byte(0x0F);
  output_byte(0xAD);
  output_modrm(3,r1,r2);
}

static void emit_cmpimm(int rs,int imm)
{
  assem_debug("cmp $%d,%%%s",imm,regname[rs]);
  if(imm<128&&imm>=-128) {
    output_byte(0x83);
    output_modrm(3,rs,7);
    output_byte(imm);
  }
  else
  {
    output_byte(0x81);
    output_modrm(3,rs,7);
    output_w32(imm);
  }
}

static void emit_cmovne(const u_int *addr,int rt)
{
  assem_debug("cmovne %x,%%%s",(int)addr,regname[rt]);
  if(addr==&const_zero) assem_debug(" [zero]");
  else if(addr==&const_one) assem_debug(" [one]");
  else assem_debug("");
  output_byte(0x0F);
  output_byte(0x45);
  output_modrm(0,5,rt);
  output_w32((int)addr);
}
static void emit_cmovl(const u_int *addr,int rt)
{
  assem_debug("cmovl %x,%%%s",(int)addr,regname[rt]);
  if(addr==&const_zero) assem_debug(" [zero]");
  else if(addr==&const_one) assem_debug(" [one]");
  else assem_debug("");
  output_byte(0x0F);
  output_byte(0x4C);
  output_modrm(0,5,rt);
  output_w32((int)addr);
}
static void emit_cmovs(const u_int *addr,int rt)
{
  assem_debug("cmovs %x,%%%s",(int)addr,regname[rt]);
  if(addr==&const_zero) assem_debug(" [zero]");
  else if(addr==&const_one) assem_debug(" [one]");
  else assem_debug("");
  output_byte(0x0F);
  output_byte(0x48);
  output_modrm(0,5,rt);
  output_w32((int)addr);
}
static void emit_cmovne_reg(int rs,int rt)
{
  assem_debug("cmovne %%%s,%%%s",regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0x45);
  output_modrm(3,rs,rt);
}
static void emit_cmovl_reg(int rs,int rt)
{
  assem_debug("cmovl %%%s,%%%s",regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0x4C);
  output_modrm(3,rs,rt);
}
static void emit_cmovs_reg(int rs,int rt)
{
  assem_debug("cmovs %%%s,%%%s",regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0x48);
  output_modrm(3,rs,rt);
}
static void emit_cmovnc_reg(int rs,int rt)
{
  assem_debug("cmovae %%%s,%%%s",regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0x43);
  output_modrm(3,rs,rt);
}
static void emit_cmova_reg(int rs,int rt)
{
  assem_debug("cmova %%%s,%%%s",regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0x47);
  output_modrm(3,rs,rt);
}
static void emit_cmovp_reg(int rs,int rt)
{
  assem_debug("cmovp %%%s,%%%s",regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0x4A);
  output_modrm(3,rs,rt);
}
static void emit_cmovnp_reg(int rs,int rt)
{
  assem_debug("cmovnp %%%s,%%%s",regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0x4B);
  output_modrm(3,rs,rt);
}
static void emit_setl(int rt)
{
  assem_debug("setl %%%s",regname[rt]);
  output_byte(0x0F);
  output_byte(0x9C);
  output_modrm(3,rt,2);
}
static void emit_movzbl_reg(int rs, int rt)
{
  assem_debug("movzbl %%%s,%%%s",regname[rs]+1,regname[rt]);
  output_byte(0x0F);
  output_byte(0xB6);
  output_modrm(3,rs,rt);
}

static void emit_slti32(int rs,int imm,int rt)
{
  if(rs!=rt) emit_zeroreg(rt);
  emit_cmpimm(rs,imm);
  if(rt<4) {
    emit_setl(rt);
    if(rs==rt) emit_movzbl_reg(rt,rt);
  }
  else
  {
    if(rs==rt) emit_movimm(0,rt);
    emit_cmovl(&const_one,rt);
  }
}
static void emit_sltiu32(int rs,int imm,int rt)
{
  if(rs!=rt) emit_zeroreg(rt);
  emit_cmpimm(rs,imm);
  if(rs==rt) emit_movimm(0,rt);
  emit_adcimm(0,rt);
}
static void emit_slti64_32(int rsh,int rsl,int imm,int rt)
{
  assert(rsh!=rt);
  emit_slti32(rsl,imm,rt);
  if(imm>=0)
  {
    emit_test(rsh,rsh);
    emit_cmovne(&const_zero,rt);
    emit_cmovs(&const_one,rt);
  }
  else
  {
    emit_cmpimm(rsh,-1);
    emit_cmovne(&const_zero,rt);
    emit_cmovl(&const_one,rt);
  }
}
static void emit_sltiu64_32(int rsh,int rsl,int imm,int rt)
{
  assert(rsh!=rt);
  emit_sltiu32(rsl,imm,rt);
  if(imm>=0)
  {
    emit_test(rsh,rsh);
    emit_cmovne(&const_zero,rt);
  }
  else
  {
    emit_cmpimm(rsh,-1);
    emit_cmovne(&const_one,rt);
  }
}

static void emit_cmp(int rs,int rt)
{
  assem_debug("cmp %%%s,%%%s",regname[rt],regname[rs]);
  output_byte(0x39);
  output_modrm(3,rs,rt);
}
static void emit_set_gz32(int rs, int rt)
{
  //assem_debug("set_gz32");
  emit_cmpimm(rs,1);
  emit_movimm(1,rt);
  emit_cmovl(&const_zero,rt);
}
static void emit_set_nz32(int rs, int rt)
{
  //assem_debug("set_nz32");
  emit_cmpimm(rs,1);
  emit_movimm(1,rt);
  emit_sbbimm(0,rt);
}
static void emit_set_gz64_32(int rsh, int rsl, int rt)
{
  //assem_debug("set_gz64");
  emit_set_gz32(rsl,rt);
  emit_test(rsh,rsh);
  emit_cmovne(&const_one,rt);
  emit_cmovs(&const_zero,rt);
}
static void emit_set_nz64_32(int rsh, int rsl, int rt)
{
  //assem_debug("set_nz64");
  emit_or_and_set_flags(rsh,rsl,rt);
  emit_cmovne(&const_one,rt);
}
static void emit_set_if_less32(int rs1, int rs2, int rt)
{
  //assem_debug("set if less (%%%s,%%%s),%%%s",regname[rs1],regname[rs2],regname[rt]);
  if(rs1!=rt&&rs2!=rt) emit_zeroreg(rt);
  emit_cmp(rs1,rs2);
  if(rs1==rt||rs2==rt) emit_movimm(0,rt);
  emit_cmovl(&const_one,rt);
}
static void emit_set_if_carry32(int rs1, int rs2, int rt)
{
  //assem_debug("set if carry (%%%s,%%%s),%%%s",regname[rs1],regname[rs2],regname[rt]);
  if(rs1!=rt&&rs2!=rt) emit_zeroreg(rt);
  emit_cmp(rs1,rs2);
  if(rs1==rt||rs2==rt) emit_movimm(0,rt);
  emit_adcimm(0,rt);
}
static void emit_set_if_less64_32(int u1, int l1, int u2, int l2, int rt)
{
  //assem_debug("set if less64 (%%%s,%%%s,%%%s,%%%s),%%%s",regname[u1],regname[l1],regname[u2],regname[l2],regname[rt]);
  assert(u1!=rt);
  assert(u2!=rt);
  emit_cmp(l1,l2);
  emit_mov(u1,rt);
  emit_sbb(u2,rt);
  emit_movimm(0,rt);
  emit_cmovl(&const_one,rt);
}
static void emit_set_if_carry64_32(int u1, int l1, int u2, int l2, int rt)
{
  //assem_debug("set if carry64 (%%%s,%%%s,%%%s,%%%s),%%%s",regname[u1],regname[l1],regname[u2],regname[l2],regname[rt]);
  assert(u1!=rt);
  assert(u2!=rt);
  emit_cmp(l1,l2);
  emit_mov(u1,rt);
  emit_sbb(u2,rt);
  emit_movimm(0,rt);
  emit_adcimm(0,rt);
}

static void emit_call(int a)
{
  assem_debug("call %x (%x+%x)",a,(int)out+5,a-(int)out-5);
  output_byte(0xe8);
  output_w32(a-(int)out-4);
}
static void emit_jmp(int a)
{
  assem_debug("jmp %x (%x+%x)",a,(int)out+5,a-(int)out-5);
  output_byte(0xe9);
  output_w32(a-(int)out-4);
}
static void emit_jne(int a)
{
  assem_debug("jne %x",a);
  output_byte(0x0f);
  output_byte(0x85);
  output_w32(a-(int)out-4);
}
static void emit_jeq(int a)
{
  assem_debug("jeq %x",a);
  output_byte(0x0f);
  output_byte(0x84);
  output_w32(a-(int)out-4);
}
static void emit_js(int a)
{
  assem_debug("js %x",a);
  output_byte(0x0f);
  output_byte(0x88);
  output_w32(a-(int)out-4);
}
static void emit_jns(int a)
{
  assem_debug("jns %x",a);
  output_byte(0x0f);
  output_byte(0x89);
  output_w32(a-(int)out-4);
}
static void emit_jl(int a)
{
  assem_debug("jl %x",a);
  output_byte(0x0f);
  output_byte(0x8c);
  output_w32(a-(int)out-4);
}
static void emit_jge(int a)
{
  assem_debug("jge %x",a);
  output_byte(0x0f);
  output_byte(0x8d);
  output_w32(a-(int)out-4);
}
static void emit_jno(int a)
{
  assem_debug("jno %x",a);
  output_byte(0x0f);
  output_byte(0x81);
  output_w32(a-(int)out-4);
}
static void emit_jc(int a)
{
  assem_debug("jc %x",a);
  output_byte(0x0f);
  output_byte(0x82);
  output_w32(a-(int)out-4);
}
static void emit_jae(int a)
{
  assem_debug("jae %x",a);
  output_byte(0x0f);
  output_byte(0x83);
  output_w32(a-(int)out-4);
}
static void emit_jb(int a)
{
  assem_debug("jb %x",a);
  output_byte(0x0f);
  output_byte(0x82);
  output_w32(a-(int)out-4);
}

static void emit_pushimm(int imm)
{
  assem_debug("push $%x",imm);
  output_byte(0x68);
  output_w32(imm);
}
static void emit_pushmem(int addr)
{
  assem_debug("push *%x",addr);
  output_byte(0xFF);
  output_modrm(0,5,6);
  output_w32(addr);
}
static void emit_pusha(void)
{
  assem_debug("pusha");
  output_byte(0x60);
}
static void emit_popa(void)
{
  assem_debug("popa");
  output_byte(0x61);
}
static void emit_pushreg(u_int r)
{
  assem_debug("push %%%s",regname[r]);
  assert(r<8);
  output_byte(0x50+r);
}
static void emit_popreg(u_int r)
{
  assem_debug("pop %%%s",regname[r]);
  assert(r<8);
  output_byte(0x58+r);
}
static void emit_callreg(u_int r)
{
  assem_debug("call *%%%s",regname[r]);
  assert(r<8);
  output_byte(0xFF);
  output_modrm(3,r,2);
}
static void emit_jmpreg(u_int r)
{
  assem_debug("jmp *%%%s",regname[r]);
  assert(r<8);
  output_byte(0xFF);
  output_modrm(3,r,4);
}
static void emit_jmpmem_indexed(u_int addr,u_int r)
{
  assem_debug("jmp *%x(%%%s)",addr,regname[r]);
  assert(r<8);
  output_byte(0xFF);
  output_modrm(2,r,4);
  output_w32(addr);
}

static void emit_readword(int addr, int rt)
{
  assem_debug("mov %x,%%%s",addr,regname[rt]);
  output_byte(0x8B);
  output_modrm(0,5,rt);
  output_w32(addr);
}
static void emit_readword_indexed(int addr, int rs, int rt)
{
  assem_debug("mov %x+%%%s,%%%s",addr,regname[rs],regname[rt]);
  output_byte(0x8B);
  if(addr<128&&addr>=-128) {
    output_modrm(1,rs,rt);
    if(rs==ESP) output_sib(0,4,4);
    output_byte(addr);
  }
  else
  {
    output_modrm(2,rs,rt);
    if(rs==ESP) output_sib(0,4,4);
    output_w32(addr);
  }
}
static void emit_readword_tlb(int addr, int map, int rt)
{
  if(map<0) emit_readword(addr+(int)g_dev.rdram.dram-0x80000000, rt);
  else
  {
    assem_debug("mov (%x,%%%s,4),%%%s",addr,regname[map],regname[rt]);
    output_byte(0x8B);
    output_modrm(0,4,rt);
    output_sib(2,map,5);
    output_w32(addr);
  }
}
static void emit_readword_indexed_tlb(int addr, int rs, int map, int rt)
{
  if(map<0) emit_readword_indexed(addr+(int)g_dev.rdram.dram-0x80000000, rs, rt);
  else {
    assem_debug("mov %x(%%%s,%%%s,4),%%%s",addr,regname[rs],regname[map],regname[rt]);
    assert(rs!=ESP);
    output_byte(0x8B);
    if(addr==0&&rs!=EBP) {
      output_modrm(0,4,rt);
      output_sib(2,map,rs);
    }
    else if(addr<128&&addr>=-128) {
      output_modrm(1,4,rt);
      output_sib(2,map,rs);
      output_byte(addr);
    }
    else
    {
      output_modrm(2,4,rt);
      output_sib(2,map,rs);
      output_w32(addr);
    }
  }
}
static void emit_readptr(int addr, int rt)
{
  emit_readword(addr,rt);
}
static void emit_movmem_indexedx4(int addr, int rs, int rt)
{
  assem_debug("mov (%x,%%%s,4),%%%s",addr,regname[rs],regname[rt]);
  output_byte(0x8B);
  output_modrm(0,4,rt);
  output_sib(2,rs,5);
  output_w32(addr);
}
static void emit_readdword_tlb(int addr, int map, int rh, int rl)
{
  if(map<0) {
    if(rh>=0) emit_readword(addr+(int)g_dev.rdram.dram-0x80000000, rh);
#ifdef NOT_MODLOADER
    emit_readword(addr+(int)g_dev.rdram.dram-0x7FFFFFFC, rl);
#else
    emit_readword(addr+(int)g_dev.rdram.dram-0xFFFFFFFC, rl);
#endif
  }
  else {
    if(rh>=0) emit_movmem_indexedx4(addr, map, rh);
    emit_movmem_indexedx4(addr+4, map, rl);
  }
}
static void emit_readdword_indexed_tlb(int addr, int rs, int map, int rh, int rl)
{
  assert(rh!=rs);
  if(rh>=0) emit_readword_indexed_tlb(addr, rs, map, rh);
  emit_readword_indexed_tlb(addr+4, rs, map, rl);
}
static void emit_movsbl(int addr, int rt)
{
  assem_debug("movsbl %x,%%%s",addr,regname[rt]);
  output_byte(0x0F);
  output_byte(0xBE);
  output_modrm(0,5,rt);
  output_w32(addr);
}
static void emit_movsbl_indexed(int addr, int rs, int rt)
{
  assem_debug("movsbl %x+%%%s,%%%s",addr,regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0xBE);
  output_modrm(2,rs,rt);
  output_w32(addr);
}
static void emit_movsbl_tlb(int addr, int map, int rt)
{
  if(map<0) emit_movsbl(addr+(int)g_dev.rdram.dram-0x80000000, rt);
  else
  {
    assem_debug("movsbl (%x,%%%s,4),%%%s",addr,regname[map],regname[rt]);
    output_byte(0x0F);
    output_byte(0xBE);
    output_modrm(0,4,rt);
    output_sib(2,map,5);
    output_w32(addr);
  }
}
static void emit_movsbl_indexed_tlb(int addr, int rs, int map, int rt)
{
  if(map<0) emit_movsbl_indexed(addr+(int)g_dev.rdram.dram-0x80000000, rs, rt);
  else {
    assem_debug("movsbl %x(%%%s,%%%s,4),%%%s",addr,regname[rs],regname[map],regname[rt]);
    assert(rs!=ESP);
    output_byte(0x0F);
    output_byte(0xBE);
    if(addr==0&&rs!=EBP) {
      output_modrm(0,4,rt);
      output_sib(2,map,rs);
    }
    else if(addr<128&&addr>=-128) {
      output_modrm(1,4,rt);
      output_sib(2,map,rs);
      output_byte(addr);
    }
    else
    {
      output_modrm(2,4,rt);
      output_sib(2,map,rs);
      output_w32(addr);
    }
  }
}
static void emit_movswl(int addr, int rt)
{
  assem_debug("movswl %x,%%%s",addr,regname[rt]);
  output_byte(0x0F);
  output_byte(0xBF);
  output_modrm(0,5,rt);
  output_w32(addr);
}
static void emit_movswl_indexed(int addr, int rs, int rt)
{
  assem_debug("movswl %x+%%%s,%%%s",addr,regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0xBF);
  output_modrm(2,rs,rt);
  output_w32(addr);
}
static void emit_movswl_tlb(int addr, int map, int rt)
{
  if(map<0) emit_movswl(addr+(int)g_dev.rdram.dram-0x80000000, rt);
  else
  {
    assem_debug("movswl (%x,%%%s,4),%%%s",addr,regname[map],regname[rt]);
    output_byte(0x0F);
    output_byte(0xBF);
    output_modrm(0,4,rt);
    output_sib(2,map,5);
    output_w32(addr);
  }
}
static void emit_movswl_indexed_tlb(int addr, int rs, int map, int rt)
{
  if(map<0) emit_movswl_indexed(addr+(int)g_dev.rdram.dram-0x80000000, rs, rt);
  else {
    assem_debug("movswl %x(%%%s,%%%s,4),%%%s",addr,regname[rs],regname[map],regname[rt]);
    assert(rs!=ESP);
    output_byte(0x0F);
    output_byte(0xBF);
    if(addr==0&&rs!=EBP) {
      output_modrm(0,4,rt);
      output_sib(2,map,rs);
    }
    else if(addr<128&&addr>=-128) {
      output_modrm(1,4,rt);
      output_sib(2,map,rs);
      output_byte(addr);
    }
    else
    {
      output_modrm(2,4,rt);
      output_sib(2,map,rs);
      output_w32(addr);
    }
  }
}
static void emit_movzbl(int addr, int rt)
{
  assem_debug("movzbl %x,%%%s",addr,regname[rt]);
  output_byte(0x0F);
  output_byte(0xB6);
  output_modrm(0,5,rt);
  output_w32(addr);
}
static void emit_movzbl_indexed(int addr, int rs, int rt)
{
  assem_debug("movzbl %x+%%%s,%%%s",addr,regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0xB6);
  output_modrm(2,rs,rt);
  output_w32(addr);
}
static void emit_movzbl_tlb(int addr, int map, int rt)
{
  if(map<0) emit_movzbl(addr+(int)g_dev.rdram.dram-0x80000000, rt);
  else
  {
    assem_debug("movzbl (%x,%%%s,4),%%%s",addr,regname[map],regname[rt]);
    output_byte(0x0F);
    output_byte(0xB6);
    output_modrm(0,4,rt);
    output_sib(2,map,5);
    output_w32(addr);
  }
}
static void emit_movzbl_indexed_tlb(int addr, int rs, int map, int rt)
{
  if(map<0) emit_movzbl_indexed(addr+(int)g_dev.rdram.dram-0x80000000, rs, rt);
  else {
    assem_debug("movzbl %x(%%%s,%%%s,4),%%%s",addr,regname[rs],regname[map],regname[rt]);
    assert(rs!=ESP);
    output_byte(0x0F);
    output_byte(0xB6);
    if(addr==0&&rs!=EBP) {
      output_modrm(0,4,rt);
      output_sib(2,map,rs);
    }
    else if(addr<128&&addr>=-128) {
      output_modrm(1,4,rt);
      output_sib(2,map,rs);
      output_byte(addr);
    }
    else
    {
      output_modrm(2,4,rt);
      output_sib(2,map,rs);
      output_w32(addr);
    }
  }
}
static void emit_movzwl(int addr, int rt)
{
  assem_debug("movzwl %x,%%%s",addr,regname[rt]);
  output_byte(0x0F);
  output_byte(0xB7);
  output_modrm(0,5,rt);
  output_w32(addr);
}
static void emit_movzwl_indexed(int addr, int rs, int rt)
{
  assem_debug("movzwl %x+%%%s,%%%s",addr,regname[rs],regname[rt]);
  output_byte(0x0F);
  output_byte(0xB7);
  output_modrm(2,rs,rt);
  output_w32(addr);
}
static void emit_movzwl_tlb(int addr, int map, int rt)
{
  if(map<0) emit_movzwl(addr+(int)g_dev.rdram.dram-0x80000000, rt);
  else
  {
    assem_debug("movzwl (%x,%%%s,4),%%%s",addr,regname[map],regname[rt]);
    output_byte(0x0F);
    output_byte(0xB7);
    output_modrm(0,4,rt);
    output_sib(2,map,5);
    output_w32(addr);
  }
}
static void emit_movzwl_indexed_tlb(int addr, int rs, int map, int rt)
{
  if(map<0) emit_movzwl_indexed(addr+(int)g_dev.rdram.dram-0x80000000, rs, rt);
  else {
    assem_debug("movzwl %x(%%%s,%%%s,4),%%%s",addr,regname[rs],regname[map],regname[rt]);
    assert(rs!=ESP);
    output_byte(0x0F);
    output_byte(0xB7);
    if(addr==0&&rs!=EBP) {
      output_modrm(0,4,rt);
      output_sib(2,map,rs);
    }
    else if(addr<128&&addr>=-128) {
      output_modrm(1,4,rt);
      output_sib(2,map,rs);
      output_byte(addr);
    }
    else
    {
      output_modrm(2,4,rt);
      output_sib(2,map,rs);
      output_w32(addr);
    }
  }
}
/*
static void emit_movzwl_reg(int rs, int rt)
{
  assem_debug("movzwl %%%s,%%%s",regname[rs]+1,regname[rt]);
  output_byte(0x0F);
  output_byte(0xB7);
  output_modrm(3,rs,rt);
}*/

static void emit_xchg(int rs, int rt)
{
  assem_debug("xchg %%%s,%%%s",regname[rs],regname[rt]);
  if(rs==EAX) {
    output_byte(0x90+rt);
  }
  else
  {
    output_byte(0x87);
    output_modrm(3,rs,rt);
  }
}
static void emit_writeword(int rt, int addr)
{
  assem_debug("movl %%%s,%x",regname[rt],addr);
  output_byte(0x89);
  output_modrm(0,5,rt);
  output_w32(addr);
}
static void emit_writeword_indexed(int rt, int addr, int rs)
{
  assem_debug("mov %%%s,%x+%%%s",regname[rt],addr,regname[rs]);
  output_byte(0x89);
  if(addr<128&&addr>=-128) {
    output_modrm(1,rs,rt);
    if(rs==ESP) output_sib(0,4,4);
    output_byte(addr);
  }
  else
  {
    output_modrm(2,rs,rt);
    if(rs==ESP) output_sib(0,4,4);
    output_w32(addr);
  }
}
static void emit_writeword_indexed_tlb(int rt, int addr, int rs, int map)
{
  if(map<0) emit_writeword_indexed(rt, addr+(int)g_dev.rdram.dram-0x80000000, rs);
  else {
    assem_debug("mov %%%s,%x(%%%s,%%%s,1)",regname[rt],addr,regname[rs],regname[map]);
    assert(rs!=ESP);
    output_byte(0x89);
    if(addr==0&&rs!=EBP) {
      output_modrm(0,4,rt);
      output_sib(0,map,rs);
    }
    else if(addr<128&&addr>=-128) {
      output_modrm(1,4,rt);
      output_sib(0,map,rs);
      output_byte(addr);
    }
    else
    {
      output_modrm(2,4,rt);
      output_sib(0,map,rs);
      output_w32(addr);
    }
  }
}
static void emit_writedword_indexed_tlb(int rh, int rl, int addr, int rs, int map)
{
  assert(rh>=0);
  emit_writeword_indexed_tlb(rh, addr, rs, map);
  emit_writeword_indexed_tlb(rl, addr+4, rs, map);
}
static void emit_writehword(int rt, int addr)
{
  assem_debug("movw %%%s,%x",regname[rt]+1,addr);
  output_byte(0x66);
  output_byte(0x89);
  output_modrm(0,5,rt);
  output_w32(addr);
}
static void emit_writehword_indexed(int rt, int addr, int rs)
{
  assem_debug("movw %%%s,%x+%%%s",regname[rt]+1,addr,regname[rs]);
  output_byte(0x66);
  output_byte(0x89);
  if(addr<128&&addr>=-128) {
    output_modrm(1,rs,rt);
    output_byte(addr);
  }
  else
  {
    output_modrm(2,rs,rt);
    output_w32(addr);
  }
}
static void emit_writehword_indexed_tlb(int rt, int addr, int rs, int map)
{
  if(map<0) emit_writehword_indexed(rt, addr+(int)g_dev.rdram.dram-0x80000000, rs);
  else {
    assem_debug("movw %%%s,%x(%%%s,%%%s,1)",regname[rt]+1,addr,regname[rs],regname[map]);
    assert(rs!=ESP);
    output_byte(0x66);
    output_byte(0x89);
    if(addr==0&&rs!=EBP) {
      output_modrm(0,4,rt);
      output_sib(0,map,rs);
    }
    else if(addr<128&&addr>=-128) {
      output_modrm(1,4,rt);
      output_sib(0,map,rs);
      output_byte(addr);
    }
    else
    {
      output_modrm(2,4,rt);
      output_sib(0,map,rs);
      output_w32(addr);
    }
  }
}
static void emit_writebyte(int rt, int addr)
{
  if(rt<4) {
    assem_debug("movb %%%cl,%x",regname[rt][1],addr);
    output_byte(0x88);
    output_modrm(0,5,rt);
    output_w32(addr);
  }
  else
  {
    emit_xchg(EAX,rt);
    emit_writebyte(EAX,addr);
    emit_xchg(EAX,rt);
  }
}
static void emit_writebyte_indexed(int rt, int addr, int rs)
{
  if(rt<4) {
    assem_debug("movb %%%cl,%x+%%%s",regname[rt][1],addr,regname[rs]);
    output_byte(0x88);
    if(addr<128&&addr>=-128) {
      output_modrm(1,rs,rt);
      output_byte(addr);
    }
    else
    {
      output_modrm(2,rs,rt);
      output_w32(addr);
    }
  }
  else
  {
    emit_xchg(EAX,rt);
    emit_writebyte_indexed(EAX,addr,rs==EAX?rt:rs);
    emit_xchg(EAX,rt);
  }
}
static void emit_writebyte_indexed_tlb(int rt, int addr, int rs, int map)
{
  if(map<0) emit_writebyte_indexed(rt, addr+(int)g_dev.rdram.dram-0x80000000, rs);
  else
  if(rt<4) {
    assem_debug("movb %%%cl,%x(%%%s,%%%s,1)",regname[rt][1],addr,regname[rs],regname[map]);
    assert(rs!=ESP);
    output_byte(0x88);
    if(addr==0&&rs!=EBP) {
      output_modrm(0,4,rt);
      output_sib(0,map,rs);
    }
    else if(addr<128&&addr>=-128) {
      output_modrm(1,4,rt);
      output_sib(0,map,rs);
      output_byte(addr);
    }
    else
    {
      output_modrm(2,4,rt);
      output_sib(0,map,rs);
      output_w32(addr);
    }
  }
  else
  {
    emit_xchg(EAX,rt);
    emit_writebyte_indexed_tlb(EAX,addr,rs==EAX?rt:rs,map==EAX?rt:map);
    emit_xchg(EAX,rt);
  }
}
static void emit_writeword_imm(int imm, int addr)
{
  assem_debug("movl $%x,%x",imm,addr);
  output_byte(0xC7);
  output_modrm(0,5,0);
  output_w32(addr);
  output_w32(imm);
}
static void emit_writeword_imm_esp(int imm, int addr)
{
  assem_debug("mov $%x,%x(%%esp)",imm,addr);
  assert(addr>=-128&&addr<128);
  output_byte(0xC7);
  output_modrm(1,4,0);
  output_sib(0,4,4);
  output_byte(addr);
  output_w32(imm);
}
static void emit_writebyte_imm(int imm, int addr)
{
  assem_debug("movb $%x,%x",imm,addr);
  assert(imm>=-128&&imm<128);
  output_byte(0xC6);
  output_modrm(0,5,0);
  output_w32(addr);
  output_byte(imm);
}

static void emit_mul(int rs)
{
  assem_debug("mul %%%s",regname[rs]);
  output_byte(0xF7);
  output_modrm(3,rs,4);
}
static void emit_imul(int rs)
{
  assem_debug("imul %%%s",regname[rs]);
  output_byte(0xF7);
  output_modrm(3,rs,5);
}
static void emit_div(int rs)
{
  assem_debug("div %%%s",regname[rs]);
  output_byte(0xF7);
  output_modrm(3,rs,6);
}
static void emit_idiv(int rs)
{
  assem_debug("idiv %%%s",regname[rs]);
  output_byte(0xF7);
  output_modrm(3,rs,7);
}
static void emit_cdq(void)
{
  assem_debug("cdq");
  output_byte(0x99);
}

// Load 2 immediates optimizing for small code size
static void emit_mov2imm_compact(int imm1,u_int rt1,int imm2,u_int rt2)
{
  emit_movimm(imm1,rt1);
  if(imm2-imm1<128&&imm2-imm1>=-128) emit_addimm(rt1,imm2-imm1,rt2);
  else emit_movimm(imm2,rt2);
}

// special case for checking pending_exception
static void emit_cmpmem_imm(int addr, int imm)
{
  assert(imm<128&&imm>=-127);
  assem_debug("cmp $%d,%x",imm,addr);
  output_byte(0x83);
  output_modrm(0,5,7);
  output_w32(addr);
  output_byte(imm);
}

// special case for checking invalid_code
static void emit_cmpmem_indexedsr12_imm(int addr,int r,int imm)
{
  assert(imm<128&&imm>=-127);
  assert(r>=0&&r<8);
  emit_shrimm(r,12,r);
  assem_debug("cmp $%d,%x+%%%s",imm,addr,regname[r]);
  output_byte(0x80);
  output_modrm(2,r,7);
  output_w32(addr);
  output_byte(imm);
}

// special case for checking hash_table
static void emit_cmpmem_indexed(int addr,int rs,int rt)
{
  assert(rs>=0&&rs<8);
  assert(rt>=0&&rt<8);
  assem_debug("cmp %x+%%%s,%%%s",addr,regname[rs],regname[rt]);
  output_byte(0x39);
  output_modrm(2,rs,rt);
  output_w32(addr);
}

// Used to preload hash table entries
#ifdef IMM_PREFETCH
static void emit_prefetch(void *addr)
{
  assem_debug("prefetch %x",(int)addr);
  output_byte(0x0F);
  output_byte(0x18);
  output_modrm(0,5,1);
  output_w32((int)addr);
}
#endif

/*void emit_submem(int r,int addr)
{
  assert(r>=0&&r<8);
  assem_debug("sub %x,%%%s",addr,regname[r]);
  output_byte(0x2B);
  output_modrm(0,5,r);
  output_w32((int)addr);
}
static void emit_subfrommem(int addr,int r)
{
  assert(r>=0&&r<8);
  assem_debug("sub %%%s,%x",regname[r],addr);
  output_byte(0x29);
  output_modrm(0,5,r);
  output_w32((int)addr);
}*/

static void emit_flds(int r)
{
  assem_debug("flds (%%%s)",regname[r]);
  output_byte(0xd9);
  if(r!=EBP) output_modrm(0,r,0);
  else {output_modrm(1,EBP,0);output_byte(0);}
}
static void emit_fldl(int r)
{
  assem_debug("fldl (%%%s)",regname[r]);
  output_byte(0xdd);
  if(r!=EBP) output_modrm(0,r,0);
  else {output_modrm(1,EBP,0);output_byte(0);}
}
static void emit_fucomip(u_int r)
{
  assem_debug("fucomip %d",r);
  assert(r<8);
  output_byte(0xdf);
  output_byte(0xe8+r);
}
static void emit_fchs(void)
{
  assem_debug("fchs");
  output_byte(0xd9);
  output_byte(0xe0);
}
static void emit_fabs(void)
{
  assem_debug("fabs");
  output_byte(0xd9);
  output_byte(0xe1);
}
static void emit_fsqrt(void)
{
  assem_debug("fsqrt");
  output_byte(0xd9);
  output_byte(0xfa);
}
static void emit_fadds(int r)
{
  assem_debug("fadds (%%%s)",regname[r]);
  output_byte(0xd8);
  if(r!=EBP) output_modrm(0,r,0);
  else {output_modrm(1,EBP,0);output_byte(0);}
}
static void emit_faddl(int r)
{
  assem_debug("faddl (%%%s)",regname[r]);
  output_byte(0xdc);
  if(r!=EBP) output_modrm(0,r,0);
  else {output_modrm(1,EBP,0);output_byte(0);}
}
static void emit_fadd(int r)
{
  assem_debug("fadd st%d",r);
  output_byte(0xd8);
  output_byte(0xc0+r);
}
static void emit_fsubs(int r)
{
  assem_debug("fsubs (%%%s)",regname[r]);
  output_byte(0xd8);
  if(r!=EBP) output_modrm(0,r,4);
  else {output_modrm(1,EBP,4);output_byte(0);}
}
static void emit_fsubl(int r)
{
  assem_debug("fsubl (%%%s)",regname[r]);
  output_byte(0xdc);
  if(r!=EBP) output_modrm(0,r,4);
  else {output_modrm(1,EBP,4);output_byte(0);}
}
static void emit_fsub(int r)
{
  assem_debug("fsub st%d",r);
  output_byte(0xd8);
  output_byte(0xe0+r);
}
static void emit_fmuls(int r)
{
  assem_debug("fmuls (%%%s)",regname[r]);
  output_byte(0xd8);
  if(r!=EBP) output_modrm(0,r,1);
  else {output_modrm(1,EBP,1);output_byte(0);}
}
static void emit_fmull(int r)
{
  assem_debug("fmull (%%%s)",regname[r]);
  output_byte(0xdc);
  if(r!=EBP) output_modrm(0,r,1);
  else {output_modrm(1,EBP,1);output_byte(0);}
}
static void emit_fmul(int r)
{
  assem_debug("fmul st%d",r);
  output_byte(0xd8);
  output_byte(0xc8+r);
}
static void emit_fdivs(int r)
{
  assem_debug("fdivs (%%%s)",regname[r]);
  output_byte(0xd8);
  if(r!=EBP) output_modrm(0,r,6);
  else {output_modrm(1,EBP,6);output_byte(0);}
}
static void emit_fdivl(int r)
{
  assem_debug("fdivl (%%%s)",regname[r]);
  output_byte(0xdc);
  if(r!=EBP) output_modrm(0,r,6);
  else {output_modrm(1,EBP,6);output_byte(0);}
}
static void emit_fdiv(int r)
{
  assem_debug("fdiv st%d",r);
  output_byte(0xd8);
  output_byte(0xf0+r);
}
static void emit_fpop(void)
{
  // fstp st(0)
  assem_debug("fpop");
  output_byte(0xdd);
  output_byte(0xd8);
}
static void emit_fildl(int r)
{
  assem_debug("fildl (%%%s)",regname[r]);
  output_byte(0xdb);
  if(r!=EBP) output_modrm(0,r,0);
  else {output_modrm(1,EBP,0);output_byte(0);}
}
static void emit_fildll(int r)
{
  assem_debug("fildll (%%%s)",regname[r]);
  output_byte(0xdf);
  if(r!=EBP) output_modrm(0,r,5);
  else {output_modrm(1,EBP,5);output_byte(0);}
}
static void emit_fistpl(int r)
{
  assem_debug("fistpl (%%%s)",regname[r]);
  output_byte(0xdb);
  if(r!=EBP) output_modrm(0,r,3);
  else {output_modrm(1,EBP,3);output_byte(0);}
}
static void emit_fistpll(int r)
{
  assem_debug("fistpll (%%%s)",regname[r]);
  output_byte(0xdf);
  if(r!=EBP) output_modrm(0,r,7);
  else {output_modrm(1,EBP,7);output_byte(0);}
}
static void emit_fstps(int r)
{
  assem_debug("fstps (%%%s)",regname[r]);
  output_byte(0xd9);
  if(r!=EBP) output_modrm(0,r,3);
  else {output_modrm(1,EBP,3);output_byte(0);}
}
static void emit_fstpl(int r)
{
  assem_debug("fstpl (%%%s)",regname[r]);
  output_byte(0xdd);
  if(r!=EBP) output_modrm(0,r,3);
  else {output_modrm(1,EBP,3);output_byte(0);}
}
static void emit_fnstcw_stack(void)
{
  assem_debug("fnstcw (%%esp)");
  output_byte(0xd9);
  output_modrm(0,4,7);
  output_sib(0,4,4);
}
static void emit_fldcw_stack(void)
{
  assem_debug("fldcw (%%esp)");
  output_byte(0xd9);
  output_modrm(0,4,5);
  output_sib(0,4,4);
}
static void emit_fldcw_indexed(int addr,int r)
{
  assem_debug("fldcw %x(%%%s)",addr,regname[r]);
  output_byte(0xd9);
  output_modrm(0,4,5);
  output_sib(2,r,5);
  output_w32(addr);
}
static void emit_fldcw(int addr)
{
  assem_debug("fldcw %x",addr);
  output_byte(0xd9);
  output_modrm(0,5,5);
  output_w32(addr);
}
#ifdef __SSE__
static void emit_movss_load(u_int addr,u_int ssereg)
{
  assem_debug("movss (%%%s),xmm%d",regname[addr],ssereg);
  assert(ssereg<8);
  output_byte(0xf3);
  output_byte(0x0f);
  output_byte(0x10);
  if(addr!=EBP) output_modrm(0,addr,ssereg);
  else {output_modrm(1,EBP,ssereg);output_byte(0);}
}
static void emit_movsd_load(u_int addr,u_int ssereg)
{
  assem_debug("movsd (%%%s),xmm%d",regname[addr],ssereg);
  assert(ssereg<8);
  output_byte(0xf2);
  output_byte(0x0f);
  output_byte(0x10);
  if(addr!=EBP) output_modrm(0,addr,ssereg);
  else {output_modrm(1,EBP,ssereg);output_byte(0);}
}
static void emit_movd_store(u_int ssereg,u_int addr)
{
  assem_debug("movd xmm%d,(%%%s)",ssereg,regname[addr]);
  assert(ssereg<8);
  output_byte(0x66);
  output_byte(0x0f);
  output_byte(0x7e);
  if(addr!=EBP) output_modrm(0,addr,ssereg);
  else {output_modrm(1,EBP,ssereg);output_byte(0);}
}
static void emit_cvttps2dq(u_int ssereg1,u_int ssereg2)
{
  assem_debug("cvttps2dq xmm%d,xmm%d",ssereg1,ssereg2);
  assert(ssereg1<8);
  assert(ssereg2<8);
  output_byte(0xf3);
  output_byte(0x0f);
  output_byte(0x5b);
  output_modrm(3,ssereg1,ssereg2);
}
static void emit_cvttpd2dq(u_int ssereg1,u_int ssereg2)
{
  assem_debug("cvttpd2dq xmm%d,xmm%d",ssereg1,ssereg2);
  assert(ssereg1<8);
  assert(ssereg2<8);
  output_byte(0x66);
  output_byte(0x0f);
  output_byte(0xe6);
  output_modrm(3,ssereg1,ssereg2);
}
#endif

static void save_regs(u_int reglist)
{
  (void)reglist;
  emit_pusha();
}

static void restore_regs(u_int reglist)
{
  (void)reglist;
  emit_popa();
}

/* Stubs/epilogue */

static void emit_extjump2(int addr, int target, int linker)
{
  u_char *ptr=(u_char *)addr;
  if(*ptr==0x0f)
  {
    assert(ptr[1]>=0x80&&ptr[1]<=0x8f);
    addr+=2;
  }
  else
  {
    assert(*ptr==0xe8||*ptr==0xe9);
    addr++;
  }
  emit_pushimm(target);
  emit_pushimm(addr);
  emit_jmp(linker);
}

static void do_invstub(int n)
{
  set_jump_target(stubs[n][1],(int)out);
  emit_call(invalidate_block_reg[stubs[n][4]]);
  emit_jmp(stubs[n][2]); // return address
}

static int do_dirty_stub(int i, struct ll_entry * head)
{
  assem_debug("do_dirty_stub %x",head->vaddr);
  emit_movimm((int)head,EAX);
  emit_call((int)&verify_code);
  int entry=(int)out;
  load_regs_entry(i);
  if(entry==(int)out) entry=instr_addr[i];
  emit_jmp(instr_addr[i]);
  return entry;
}

static void do_dirty_stub_ds(struct ll_entry * head)
{
  assem_debug("do_dirty_stub_ds %x",head->vaddr);
  emit_movimm((int)head,EAX);
  emit_call((int)&verify_code);
}

/* TLB */

static int do_tlb_r(int s,int ar,int map,int cache,int x,int c,u_int addr)
{
  if(c) {
    if((signed int)addr>=(signed int)0xC0000000) {
      emit_readword((int)(g_dev.r4300.new_dynarec_hot_state.memory_map+(addr>>12)),map);
    }
    else
      return -1; // No mapping
  }
  else {
    if(s!=map) emit_mov(s,map);
    emit_shrimm(map,12,map);
    // Schedule this while we wait on the load
    //if(x) emit_xorimm(addr,x,addr);
    emit_movmem_indexedx4((int)g_dev.r4300.new_dynarec_hot_state.memory_map,map,map);
  }
  return map;
}
static int do_tlb_r_branch(int map, int c, u_int addr, int *jaddr)
{
  if(!c||(signed int)addr>=(signed int)0xC0000000) {
    emit_test(map,map);
    *jaddr=(int)out;
    emit_js(0);
  }
  return map;
}

static int do_tlb_w(int s,int ar,int map,int cache,int x,int c,u_int addr)
{
  if(c) {
    if(addr<0x80800000||addr>=0xC0000000) {
      emit_readword((int)(g_dev.r4300.new_dynarec_hot_state.memory_map+(addr>>12)),map);
    }
    else
      return -1; // No mapping
  }
  else {
    if(s!=map) emit_mov(s,map);
    //if(s!=ar) emit_mov(s,ar);
    emit_shrimm(map,12,map);
    // Schedule this while we wait on the load
    //if(x) emit_xorimm(s,x,addr);
    emit_movmem_indexedx4((int)g_dev.r4300.new_dynarec_hot_state.memory_map,map,map);
  }
  emit_shlimm(map,2,map);
  return map;
}
static void do_tlb_w_branch(int map, int c, u_int addr, int *jaddr)
{
  if(!c||addr<0x80800000||addr>=0xC0000000) {
    *jaddr=(int)out;
    emit_jc(0);
  }
}

// We don't need this for x86
static void generate_map_const(u_int addr,int reg) {
  // void *mapaddr=g_dev.r4300.new_dynarec_hot_state.memory_map+(addr>>12);
}

static void set_rounding_mode(int s,int temp)
{
  assert(temp>=0);
  emit_movimm(3,temp);
  emit_and(s,temp,temp);
  emit_fldcw_indexed((int)&g_dev.r4300.new_dynarec_hot_state.rounding_modes,temp);
}

/* Special assem */

static void shift_assemble_x86(int i,struct regstat *i_regs)
{
  if(rt1[i]) {
    if(opcode2[i]<=0x07) // SLLV/SRLV/SRAV
    {
      signed char s,t,shift;
      t=get_reg(i_regs->regmap,rt1[i]);
      s=get_reg(i_regs->regmap,rs1[i]);
      shift=get_reg(i_regs->regmap,rs2[i]);
      if(t>=0){
        if(rs1[i]==0)
        {
          emit_zeroreg(t);
        }
        else if(rs2[i]==0)
        {
          assert(s>=0);
          if(s!=t) emit_mov(s,t);
        }
        else
        {
          signed char temp=get_reg(i_regs->regmap,-1);
          assert(s>=0);
          if(t==ECX&&s!=ECX) {
            if(shift!=ECX) emit_mov(shift,ECX);
            if(rt1[i]==rs2[i]) {shift=temp;}
            if(s!=shift) emit_mov(s,shift);
          }
          else
          {
            if(rt1[i]==rs2[i]) {emit_mov(shift,temp);shift=temp;}
            if(s!=t) emit_mov(s,t);
            if(shift!=ECX) {
              if(i_regs->regmap[ECX]<0)
                emit_mov(shift,ECX);
              else
                emit_xchg(shift,ECX);
            }
          }
          if(opcode2[i]==4) // SLLV
          {
            emit_shlcl(t==ECX?shift:t);
          }
          if(opcode2[i]==6) // SRLV
          {
            emit_shrcl(t==ECX?shift:t);
          }
          if(opcode2[i]==7) // SRAV
          {
            emit_sarcl(t==ECX?shift:t);
          }
          if(shift!=ECX&&i_regs->regmap[ECX]>=0) emit_xchg(shift,ECX);
        }
      }
    } else { // DSLLV/DSRLV/DSRAV
      signed char sh,sl,th,tl,shift;
      th=get_reg(i_regs->regmap,rt1[i]|64);
      tl=get_reg(i_regs->regmap,rt1[i]);
      sh=get_reg(i_regs->regmap,rs1[i]|64);
      sl=get_reg(i_regs->regmap,rs1[i]);
      shift=get_reg(i_regs->regmap,rs2[i]);
      if(tl>=0){
        if(rs1[i]==0)
        {
          emit_zeroreg(tl);
          if(th>=0) emit_zeroreg(th);
        }
        else if(rs2[i]==0)
        {
          assert(sl>=0);
          if(sl!=tl) emit_mov(sl,tl);
          if(th>=0&&sh!=th) emit_mov(sh,th);
        }
        else
        {
          int temp=get_reg(i_regs->regmap,-1);
          int real_tl=tl;
          int real_th=th;
          if(th<0&&opcode2[i]!=0x14) {th=temp;} // DSLLV doesn't need a temporary register
          assert(sl>=0);
          assert(sh>=0);
          if(tl==ECX&&sl!=ECX) {
            if(shift!=ECX) emit_mov(shift,ECX);
            if((sl!=shift)&&(shift!=tl)) emit_mov(sl,shift);
            if(th>=0 && sh!=th) emit_mov(sh,th);
            if(shift==tl) {
              emit_pushreg(sl);
              tl=sl;
            }
          }
          else if(th==ECX&&sh!=ECX) {
            if(shift!=ECX) emit_mov(shift,ECX);
            if(sh!=shift) emit_mov(sh,shift);
            if((sl!=tl)&&(shift!=tl)) emit_mov(sl,tl);
            if(shift==tl) {
              emit_pushreg(sl);
              tl=sl;
            }
          }
          else
          {
            if((sl!=tl)&&(shift!=tl)) emit_mov(sl,tl);
            if(th>=0 && sh!=th) emit_mov(sh,th);
            if(shift!=ECX) {
              if(shift==tl) {
                emit_pushreg(sl);
                tl=sl;
              }
              emit_xchg(shift,ECX);
            }
          }
          if(opcode2[i]==0x14) // DSLLV
          {
            if(th>=0) emit_shldcl(th==ECX?shift:th,tl==ECX?shift:tl);
            emit_shlcl(tl==ECX?shift:tl);
            emit_testimm(ECX,32);
            if(th>=0) emit_cmovne_reg(tl==ECX?shift:tl,th==ECX?shift:th);
            emit_cmovne(&const_zero,tl==ECX?shift:tl);
          }
          if(opcode2[i]==0x16) // DSRLV
          {
            assert(th>=0);
            emit_shrdcl(tl==ECX?shift:tl,th==ECX?shift:th);
            emit_shrcl(th==ECX?shift:th);
            emit_testimm(ECX,32);
            emit_cmovne_reg(th==ECX?shift:th,tl==ECX?shift:tl);
            if(real_th>=0) emit_cmovne(&const_zero,th==ECX?shift:th);
          }
          if(opcode2[i]==0x17) // DSRAV
          {
            assert(th>=0);
            emit_shrdcl(tl==ECX?shift:tl,th==ECX?shift:th);
            if(real_th>=0) {
              assert(temp>=0);
              emit_mov(th==ECX?shift:th,temp==ECX?shift:temp);
            }
            emit_sarcl(th==ECX?shift:th);
            if(real_th>=0) emit_sarimm(temp==ECX?shift:temp,31,temp==ECX?shift:temp);
            emit_testimm(ECX,32);
            emit_cmovne_reg(th==ECX?shift:th,tl==ECX?shift:tl);
            if(real_th>=0) emit_cmovne_reg(temp==ECX?shift:temp,th==ECX?shift:th);
          }
          if(real_tl==ECX&&sl!=ECX&&shift==real_tl) {
            emit_mov(sl,real_tl);
            emit_popreg(sl);
          }
          else if(th==ECX&&sh!=ECX&&shift==real_tl) {
            emit_mov(sl,real_tl);
            emit_popreg(sl);
          }
          else
          {
            if(shift!=ECX) {
              emit_xchg(shift,ECX);
              if(real_tl==shift) {
                emit_mov(sl,real_tl);
                emit_popreg(sl);
              }
            }
          }
        }
      }
    }
  }
}
#define shift_assemble shift_assemble_x86

static void loadlr_assemble_x86(int i,struct regstat *i_regs)
{
  signed char s,th,tl,temp,temp2,temp2h,addr,map=-1,cache=-1;
  int offset,type=0,memtarget=0,c=0;
  intptr_t jaddr=0;
  u_int hr,reglist=0;
  th=get_reg(i_regs->regmap,rt1[i]|64);
  tl=get_reg(i_regs->regmap,rt1[i]);
  s=get_reg(i_regs->regmap,rs1[i]);
  temp=get_reg(i_regs->regmap,-1);
  temp2=get_reg(i_regs->regmap,FTEMP);
  temp2h=get_reg(i_regs->regmap,FTEMP|64);
  addr=get_reg(i_regs->regmap,AGEN1+(i&1));
  assert(addr<0);
  assert(temp>=0);
  assert(temp2>=0);
  assert(temp2h>=0);
  offset=imm[i];

  for(hr=0;hr<HOST_REGS;hr++) {
    if(i_regs->regmap[hr]>=0) reglist|=1<<hr;
  }
  reglist|=1<<temp;
  if(s>=0) {
    c=(i_regs->wasconst>>s)&1;
    memtarget=c&&((signed int)(constmap[i][s]+offset))<(signed int)0x80800000;
    if(c&&using_tlb&&((signed int)(constmap[i][s]+offset))>=(signed int)0xC0000000) memtarget=1;
  }
  if(offset||s<0||c) addr=temp2;
  else addr=s;
  int dummy=(rt1[i]==0)||(tl!=get_reg(i_regs->regmap,rt1[i])); // ignore loads to r0 and unneeded reg

  switch(opcode[i]) {
    case 0x22: type=LOADWL_STUB; break;
    case 0x26: type=LOADWR_STUB; break;
    case 0x1A: type=LOADDL_STUB; break;
    case 0x1B: type=LOADDR_STUB; break;
  }

#ifndef INTERPRET_LOADLR
  int ldlr=(opcode[i]==0x1A||opcode[i]==0x1B); // LDL/LDR always do inline_readstub if non constant
  if(!using_tlb) {
    if(!c&&!ldlr) {
      emit_cmpimm(addr,0x800000);
      jaddr=(intptr_t)out;
      emit_jno(0);
    }
  }else{ // using tlb
    map=get_reg(i_regs->regmap,TLREG);
    cache=get_reg(i_regs->regmap,MMREG);
    assert(map>=0);
    reglist&=~(1<<map);
    if((!c&&!ldlr)||memtarget) {
      map=do_tlb_r(addr,temp2,map,cache,0,c,constmap[i][s]+offset);
      do_tlb_r_branch(map,c,constmap[i][s]+offset,&jaddr);
    }
  }
  if((!c||memtarget)&&!dummy) {
    if(opcode[i]==0x22||opcode[i]==0x26) { // LWL/LWR
      assert(tl>=0);
      if(!c) {
        emit_lea8(addr,temp);
        emit_andimm(addr,~3,temp2);
        emit_readword_indexed_tlb(0,temp2,map,temp2);
        emit_andimm(temp,24,temp);
        if (opcode[i]==0x26) emit_xorimm(temp,24,temp); // LWR
        if(temp!=ECX) emit_xchg(ECX,temp);
        emit_movimm(-1,temp2h==ECX?temp:temp2h);
        if (opcode[i]==0x26) {
          emit_shrcl(temp2h==ECX?temp:temp2h);
          emit_shrcl(temp2==ECX?temp:temp2);
        }else{
          emit_shlcl(temp2h==ECX?temp:temp2h);
          emit_shlcl(temp2==ECX?temp:temp2);
        }
        emit_mov(temp2h==ECX?temp:temp2h,ECX);
        emit_not(ECX,ECX);
        if(temp!=ECX) emit_xchg(temp,ECX);
        emit_and(temp,tl,tl);
        emit_or(temp2,tl,tl);
      }
      else
      {
        int shift=((constmap[i][s]+offset)&3)<<3;
        uint32_t mask=~UINT32_C(0);
        if (opcode[i]==0x26) { //LWR
          shift^=24;
          mask>>=shift;
        } else { //LWL
          mask<<=shift;
        }

        if(shift) {
          emit_readword_tlb((constmap[i][s]+offset)&~3,map,temp2);
          if (opcode[i]==0x26) emit_shrimm(temp2,shift,temp2);
          else emit_shlimm(temp2,shift,temp2);
          emit_andimm(tl,~mask,tl);
          emit_or(temp2,tl,tl);
        }
        else
          emit_readword_tlb((constmap[i][s]+offset)&~3,map,tl);
      }
    }
    if(opcode[i]==0x1A||opcode[i]==0x1B) { // LDL/LDR
      assert(tl>=0);
      assert(th>=0);
      if(!c) {
        //TODO: implement recompiled code
        inline_readstub(type,i,0,addr,i_regs,rt1[i],ccadj[i],reglist);
      }
      else
      {
        int shift=((constmap[i][s]+offset)&7)<<3;
        uint64_t mask=~UINT64_C(0);
        if (opcode[i]==0x1B) { //LDR
          shift^=56;
          mask>>=shift;
        } else { //LDL
          mask<<=shift;
        }

        if(shift) {
          emit_readdword_tlb((constmap[i][s]+offset)&~7,map,temp2h,temp2);
          if (opcode[i]==0x1B) {
            emit_shrdimm(temp2h,temp2,shift,temp2h);
            emit_shrimm(temp2,shift,temp2);
          } else {
            emit_shldimm(temp2h,temp2,shift,temp2h);
            emit_shlimm(temp2,shift,temp2);
          }
          emit_andimm(tl,~mask,tl);
          emit_andimm(th,~mask>>32,th);
          emit_or(temp2,tl,tl);
          emit_or(temp2h,th,th);
        }
        else
          emit_readdword_tlb((constmap[i][s]+offset)&~7,map,th,tl);
      }
    }
  }
  if(jaddr) {
    add_stub(type,jaddr,(intptr_t)out,i,addr,(intptr_t)i_regs,ccadj[i],reglist);
  } else if(c&&!memtarget) {
    inline_readstub(type,i,(constmap[i][s]+offset),addr,i_regs,rt1[i],ccadj[i],reglist);
  }
#else
  inline_readstub(type,i,c?(constmap[i][s]+offset):0,addr,i_regs,rt1[i],ccadj[i],reglist);
#endif
}
#define loadlr_assemble loadlr_assemble_x86

static void fconv_assemble_x86(int i,struct regstat *i_regs)
{
  signed char temp=get_reg(i_regs->regmap,-1);
  assert(temp>=0);
  // Check cop1 unusable
  if(!cop1_usable) {
    signed char rs=get_reg(i_regs->regmap,CSREG);
    assert(rs>=0);
    emit_testimm(rs,CP0_STATUS_CU1);
    int jaddr=(int)out;
    emit_jeq(0);
    add_stub(FP_STUB,jaddr,(int)out,i,rs,(int)i_regs,is_delayslot,0);
    cop1_usable=1;
  }

#ifndef INTERPRET_FCONV
#ifdef __SSE__
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x0d) { // trunc_w_s
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f],temp);
    emit_movss_load(temp,0);
    emit_cvttps2dq(0,0); // float->int, truncate
    if(((source[i]>>11)&0x1f)!=((source[i]>>6)&0x1f))
      emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>6)&0x1f],temp);
    emit_movd_store(0,temp);
    return;
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x0d) { // trunc_w_d
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f],temp);
    emit_movsd_load(temp,0);
    emit_cvttpd2dq(0,0); // double->int, truncate
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>6)&0x1f],temp);
    emit_movd_store(0,temp);
    return;
  }
#endif

  if(opcode2[i]==0x14&&(source[i]&0x3f)==0x20) { // cvt_s_w
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f],temp);
    emit_fildl(temp);
    if(((source[i]>>11)&0x1f)!=((source[i]>>6)&0x1f))
      emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>6)&0x1f],temp);
    emit_fstps(temp);
    return;
  }
  if(opcode2[i]==0x14&&(source[i]&0x3f)==0x21) { // cvt_d_w
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f],temp);
    emit_fildl(temp);
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>6)&0x1f],temp);
    emit_fstpl(temp);
    return;
  }
  if(opcode2[i]==0x15&&(source[i]&0x3f)==0x20) { // cvt_s_l
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f],temp);
    emit_fildll(temp);
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>6)&0x1f],temp);
    emit_fstps(temp);
    return;
  }
  if(opcode2[i]==0x15&&(source[i]&0x3f)==0x21) { // cvt_d_l
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f],temp);
    emit_fildll(temp);
    if(((source[i]>>11)&0x1f)!=((source[i]>>6)&0x1f))
      emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>6)&0x1f],temp);
    emit_fstpl(temp);
    return;
  }

  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x21) { // cvt_d_s
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f],temp);
    emit_flds(temp);
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>6)&0x1f],temp);
    emit_fstpl(temp);
    return;
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x20) { // cvt_s_d
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f],temp);
    emit_fldl(temp);
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>6)&0x1f],temp);
    emit_fstps(temp);
    return;
  }

  if(opcode2[i]==0x10) { // cvt_*_s
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f],temp);
    emit_flds(temp);
  }
  if(opcode2[i]==0x11) { // cvt_*_d
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f],temp);
    emit_fldl(temp);
  }
  if((source[i]&0x3f)<0x10) {
    emit_fnstcw_stack();
    if((source[i]&3)==0) emit_fldcw((int)&g_dev.r4300.new_dynarec_hot_state.rounding_modes[0]); //DebugMessage(M64MSG_VERBOSE, "round");
    if((source[i]&3)==1) emit_fldcw((int)&g_dev.r4300.new_dynarec_hot_state.rounding_modes[1]); //DebugMessage(M64MSG_VERBOSE, "trunc");
    if((source[i]&3)==2) emit_fldcw((int)&g_dev.r4300.new_dynarec_hot_state.rounding_modes[2]); //DebugMessage(M64MSG_VERBOSE, "ceil");
    if((source[i]&3)==3) emit_fldcw((int)&g_dev.r4300.new_dynarec_hot_state.rounding_modes[3]); //DebugMessage(M64MSG_VERBOSE, "floor");
  }
  if((source[i]&0x3f)==0x24||(source[i]&0x3c)==0x0c) { // cvt_w_*
    if(opcode2[i]!=0x10||((source[i]>>11)&0x1f)!=((source[i]>>6)&0x1f))
      emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>6)&0x1f],temp);
    emit_fistpl(temp);
  }
  if((source[i]&0x3f)==0x25||(source[i]&0x3c)==0x08) { // cvt_l_*
    if(opcode2[i]!=0x11||((source[i]>>11)&0x1f)!=((source[i]>>6)&0x1f))
      emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>6)&0x1f],temp);
    emit_fistpll(temp);
  }
  if((source[i]&0x3f)<0x10) {
    emit_fldcw_stack();
  }
  return;
#endif

  // C emulation code for debugging

  signed char fs=get_reg(i_regs->regmap,FSREG);
  emit_pusha();

  if(opcode2[i]==0x14&&(source[i]&0x3f)==0x20) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    if(fs>=0) emit_pushreg(fs);
    else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    emit_call((int)cvt_s_w);
    emit_addimm(ESP,12,ESP);
  }
  if(opcode2[i]==0x14&&(source[i]&0x3f)==0x21) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)cvt_d_w);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x15&&(source[i]&0x3f)==0x20) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    if(fs>=0) emit_pushreg(fs);
    else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    emit_call((int)cvt_s_l);
    emit_addimm(ESP,12,ESP);
  }
  if(opcode2[i]==0x15&&(source[i]&0x3f)==0x21) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    if(fs>=0) emit_pushreg(fs);
    else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    emit_call((int)cvt_d_l);
    emit_addimm(ESP,12,ESP);
  }

  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x21) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)cvt_d_s);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x24) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    if(fs>=0) emit_pushreg(fs);
    else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    emit_call((int)cvt_w_s);
    emit_addimm(ESP,12,ESP);
  }
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x25) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    if(fs>=0) emit_pushreg(fs);
    else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    emit_call((int)cvt_l_s);
    emit_addimm(ESP,12,ESP);
  }

  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x20) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    if(fs>=0) emit_pushreg(fs);
    else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    emit_call((int)cvt_s_d);
    emit_addimm(ESP,12,ESP);
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x24) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    if(fs>=0) emit_pushreg(fs);
    else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    emit_call((int)cvt_w_d);
    emit_addimm(ESP,12,ESP);
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x25) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    if(fs>=0) emit_pushreg(fs);
    else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    emit_call((int)cvt_l_d);
    emit_addimm(ESP,12,ESP);
  }

  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x08) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)round_l_s);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x09) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)trunc_l_s);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x0a) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)ceil_l_s);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x0b) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)floor_l_s);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x0c) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)round_w_s);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x0d) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)trunc_w_s);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x0e) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)ceil_w_s);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x10&&(source[i]&0x3f)==0x0f) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_call((int)floor_w_s);
    emit_addimm(ESP,8,ESP);
  }

  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x08) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    emit_call((int)round_l_d);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x09) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    emit_call((int)trunc_l_d);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x0a) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    emit_call((int)ceil_l_d);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x0b) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    emit_call((int)floor_l_d);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x0c) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    emit_call((int)round_w_d);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x0d) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    emit_call((int)trunc_w_d);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x0e) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    emit_call((int)ceil_w_d);
    emit_addimm(ESP,8,ESP);
  }
  if(opcode2[i]==0x11&&(source[i]&0x3f)==0x0f) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    emit_call((int)floor_w_d);
    emit_addimm(ESP,8,ESP);
  }

  emit_popa();
  //emit_loadreg(CSREG,rs);
  return;
}
#define fconv_assemble fconv_assemble_x86

static void fcomp_assemble(int i,struct regstat *i_regs)
{
  signed char fs=get_reg(i_regs->regmap,FSREG);
  signed char temp=get_reg(i_regs->regmap,-1);
  assert(temp>=0);
  // Check cop1 unusable
  if(!cop1_usable) {
    signed char cs=get_reg(i_regs->regmap,CSREG);
    assert(cs>=0);
    emit_testimm(cs,CP0_STATUS_CU1);
    int jaddr=(int)out;
    emit_jeq(0);
    add_stub(FP_STUB,jaddr,(int)out,i,cs,(int)i_regs,is_delayslot,0);
    cop1_usable=1;
  }

#ifndef INTERPRET_FCOMP
  if((source[i]&0x3f)==0x30) {
    emit_andimm(fs,~0x800000,fs);
    return;
  }

  if((source[i]&0x3e)==0x38) {
    // sf/ngle - these should throw exceptions for NaNs
    emit_andimm(fs,~0x800000,fs);
    return;
  }

  if(opcode2[i]==0x10) {
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>16)&0x1f],temp);
    emit_flds(temp);
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f],temp);
    emit_flds(temp);
    emit_movimm(0x800000,temp);
    emit_or(fs,temp,fs);
    emit_xor(temp,fs,temp);
    emit_fucomip(1);
    emit_fpop();
    if((source[i]&0x3f)==0x31) emit_cmovnp_reg(temp,fs); // c_un_s
    if((source[i]&0x3f)==0x32) {emit_cmovne_reg(temp,fs);emit_cmovp_reg(temp,fs);} // c_eq_s
    if((source[i]&0x3f)==0x33) emit_cmovne_reg(temp,fs); // c_ueq_s
    if((source[i]&0x3f)==0x34) {emit_cmovnc_reg(temp,fs);emit_cmovp_reg(temp,fs);} // c_olt_s
    if((source[i]&0x3f)==0x35) emit_cmovnc_reg(temp,fs); // c_ult_s
    if((source[i]&0x3f)==0x36) {emit_cmova_reg(temp,fs);emit_cmovp_reg(temp,fs);} // c_ole_s
    if((source[i]&0x3f)==0x37) emit_cmova_reg(temp,fs); // c_ule_s
    if((source[i]&0x3f)==0x3a) emit_cmovne_reg(temp,fs); // c_seq_s
    if((source[i]&0x3f)==0x3b) emit_cmovne_reg(temp,fs); // c_ngl_s
    if((source[i]&0x3f)==0x3c) emit_cmovnc_reg(temp,fs); // c_lt_s
    if((source[i]&0x3f)==0x3d) emit_cmovnc_reg(temp,fs); // c_nge_s
    if((source[i]&0x3f)==0x3e) emit_cmova_reg(temp,fs); // c_le_s
    if((source[i]&0x3f)==0x3f) emit_cmova_reg(temp,fs); // c_ngt_s
    return;
  }
  if(opcode2[i]==0x11) {
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>16)&0x1f],temp);
    emit_fldl(temp);
    emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f],temp);
    emit_fldl(temp);
    emit_movimm(0x800000,temp);
    emit_or(fs,temp,fs);
    emit_xor(temp,fs,temp);
    emit_fucomip(1);
    emit_fpop();
    if((source[i]&0x3f)==0x31) emit_cmovnp_reg(temp,fs); // c_un_d
    if((source[i]&0x3f)==0x32) {emit_cmovne_reg(temp,fs);emit_cmovp_reg(temp,fs);} // c_eq_d
    if((source[i]&0x3f)==0x33) emit_cmovne_reg(temp,fs); // c_ueq_d
    if((source[i]&0x3f)==0x34) {emit_cmovnc_reg(temp,fs);emit_cmovp_reg(temp,fs);} // c_olt_d
    if((source[i]&0x3f)==0x35) emit_cmovnc_reg(temp,fs); // c_ult_d
    if((source[i]&0x3f)==0x36) {emit_cmova_reg(temp,fs);emit_cmovp_reg(temp,fs);} // c_ole_d
    if((source[i]&0x3f)==0x37) emit_cmova_reg(temp,fs); // c_ule_d
    if((source[i]&0x3f)==0x3a) emit_cmovne_reg(temp,fs); // c_seq_d
    if((source[i]&0x3f)==0x3b) emit_cmovne_reg(temp,fs); // c_ngl_d
    if((source[i]&0x3f)==0x3c) emit_cmovnc_reg(temp,fs); // c_lt_d
    if((source[i]&0x3f)==0x3d) emit_cmovnc_reg(temp,fs); // c_nge_d
    if((source[i]&0x3f)==0x3e) emit_cmova_reg(temp,fs); // c_le_d
    if((source[i]&0x3f)==0x3f) emit_cmova_reg(temp,fs); // c_ngt_d
    return;
  }
#endif

  emit_storereg(FSREG, fs);
  emit_pusha();
  if(opcode2[i]==0x10) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>16)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    emit_pushimm((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    if((source[i]&0x3f)==0x30) emit_call((int)c_f_s);
    if((source[i]&0x3f)==0x31) emit_call((int)c_un_s);
    if((source[i]&0x3f)==0x32) emit_call((int)c_eq_s);
    if((source[i]&0x3f)==0x33) emit_call((int)c_ueq_s);
    if((source[i]&0x3f)==0x34) emit_call((int)c_olt_s);
    if((source[i]&0x3f)==0x35) emit_call((int)c_ult_s);
    if((source[i]&0x3f)==0x36) emit_call((int)c_ole_s);
    if((source[i]&0x3f)==0x37) emit_call((int)c_ule_s);
    if((source[i]&0x3f)==0x38) emit_call((int)c_sf_s);
    if((source[i]&0x3f)==0x39) emit_call((int)c_ngle_s);
    if((source[i]&0x3f)==0x3a) emit_call((int)c_seq_s);
    if((source[i]&0x3f)==0x3b) emit_call((int)c_ngl_s);
    if((source[i]&0x3f)==0x3c) emit_call((int)c_lt_s);
    if((source[i]&0x3f)==0x3d) emit_call((int)c_nge_s);
    if((source[i]&0x3f)==0x3e) emit_call((int)c_le_s);
    if((source[i]&0x3f)==0x3f) emit_call((int)c_ngt_s);
  }
  if(opcode2[i]==0x11) {
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>16)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    emit_pushimm((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    if((source[i]&0x3f)==0x30) emit_call((int)c_f_d);
    if((source[i]&0x3f)==0x31) emit_call((int)c_un_d);
    if((source[i]&0x3f)==0x32) emit_call((int)c_eq_d);
    if((source[i]&0x3f)==0x33) emit_call((int)c_ueq_d);
    if((source[i]&0x3f)==0x34) emit_call((int)c_olt_d);
    if((source[i]&0x3f)==0x35) emit_call((int)c_ult_d);
    if((source[i]&0x3f)==0x36) emit_call((int)c_ole_d);
    if((source[i]&0x3f)==0x37) emit_call((int)c_ule_d);
    if((source[i]&0x3f)==0x38) emit_call((int)c_sf_d);
    if((source[i]&0x3f)==0x39) emit_call((int)c_ngle_d);
    if((source[i]&0x3f)==0x3a) emit_call((int)c_seq_d);
    if((source[i]&0x3f)==0x3b) emit_call((int)c_ngl_d);
    if((source[i]&0x3f)==0x3c) emit_call((int)c_lt_d);
    if((source[i]&0x3f)==0x3d) emit_call((int)c_nge_d);
    if((source[i]&0x3f)==0x3e) emit_call((int)c_le_d);
    if((source[i]&0x3f)==0x3f) emit_call((int)c_ngt_d);
  }
  emit_addimm(ESP,12,ESP);
  emit_popa();
  emit_loadreg(FSREG,fs);
  return;
}

static void float_assemble(int i,struct regstat *i_regs)
{
  signed char temp=get_reg(i_regs->regmap,-1);
  assert(temp>=0);
  // Check cop1 unusable
  if(!cop1_usable) {
    signed char cs=get_reg(i_regs->regmap,CSREG);
    assert(cs>=0);
    emit_testimm(cs,CP0_STATUS_CU1);
    int jaddr=(int)out;
    emit_jeq(0);
    add_stub(FP_STUB,jaddr,(int)out,i,cs,(int)i_regs,is_delayslot,0);
    cop1_usable=1;
  }

#ifndef INTERPRET_FLOAT
  if((source[i]&0x3f)==6) // mov
  {
    if(((source[i]>>11)&0x1f)!=((source[i]>>6)&0x1f)) {
      if(opcode2[i]==0x10) {
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f],temp);
        emit_flds(temp);
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>6)&0x1f],temp);
        emit_fstps(temp);
      }
      if(opcode2[i]==0x11) {
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f],temp);
        emit_fldl(temp);
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>6)&0x1f],temp);
        emit_fstpl(temp);
      }
    }
    return;
  }

  if((source[i]&0x3f)>3)
  {
    if(opcode2[i]==0x10) {
      emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f],temp);
      emit_flds(temp);
      if(((source[i]>>11)&0x1f)!=((source[i]>>6)&0x1f)) {
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>6)&0x1f],temp);
      }
    }
    if(opcode2[i]==0x11) {
      emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f],temp);
      emit_fldl(temp);
      if(((source[i]>>11)&0x1f)!=((source[i]>>6)&0x1f)) {
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>6)&0x1f],temp);
      }
    }
    if((source[i]&0x3f)==4) // sqrt
      emit_fsqrt();
    if((source[i]&0x3f)==5) // abs
      emit_fabs();
    if((source[i]&0x3f)==7) // neg
      emit_fchs();
    if(opcode2[i]==0x10) {
      emit_fstps(temp);
    }
    if(opcode2[i]==0x11) {
      emit_fstpl(temp);
    }
    return;
  }
  if((source[i]&0x3f)<4)
  {
    if(opcode2[i]==0x10) {
      emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f],temp);
      emit_flds(temp);
    }
    if(opcode2[i]==0x11) {
      emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f],temp);
      emit_fldl(temp);
    }
    if(((source[i]>>11)&0x1f)!=((source[i]>>16)&0x1f)) {
      if(opcode2[i]==0x10) {
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>16)&0x1f],temp);
        if((source[i]&0x3f)==0) emit_fadds(temp);
        if((source[i]&0x3f)==1) emit_fsubs(temp);
        if((source[i]&0x3f)==2) emit_fmuls(temp);
        if((source[i]&0x3f)==3) emit_fdivs(temp);
      }
      else if(opcode2[i]==0x11) {
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>16)&0x1f],temp);
        if((source[i]&0x3f)==0) emit_faddl(temp);
        if((source[i]&0x3f)==1) emit_fsubl(temp);
        if((source[i]&0x3f)==2) emit_fmull(temp);
        if((source[i]&0x3f)==3) emit_fdivl(temp);
      }
    }
    else {
      if((source[i]&0x3f)==0) emit_fadd(0);
      if((source[i]&0x3f)==1) emit_fsub(0);
      if((source[i]&0x3f)==2) emit_fmul(0);
      if((source[i]&0x3f)==3) emit_fdiv(0);
    }
    if(opcode2[i]==0x10) {
      if(((source[i]>>16)&0x1f)!=((source[i]>>6)&0x1f)) {
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>6)&0x1f],temp);
      }
      emit_fstps(temp);
    }
    if(opcode2[i]==0x11) {
      if(((source[i]>>16)&0x1f)!=((source[i]>>6)&0x1f)) {
        emit_readptr((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>6)&0x1f],temp);
      }
      emit_fstpl(temp);
    }
    return;
  }
#endif

  signed char fs=get_reg(i_regs->regmap,FSREG);
  if(opcode2[i]==0x10) { // Single precision
    emit_pusha();
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>> 6)&0x1f]);
    if((source[i]&0x3f)<4)
      emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>16)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_simple[(source[i]>>11)&0x1f]);
    if((source[i]&0x3f)<=4) {
      if(fs>=0) emit_pushreg(fs);
      else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    }
    switch(source[i]&0x3f)
    {
      case 0x00:
          emit_call((int)add_s);
          emit_addimm(ESP,16,ESP);
          break;
      case 0x01:
          emit_call((int)sub_s);
          emit_addimm(ESP,16,ESP);
          break;
      case 0x02:
          emit_call((int)mul_s);
          emit_addimm(ESP,16,ESP);
          break;
      case 0x03:
          emit_call((int)div_s);
          emit_addimm(ESP,16,ESP);
          break;
      case 0x04:
          emit_call((int)sqrt_s);
          emit_addimm(ESP,12,ESP);
          break;
      case 0x05:
          emit_call((int)abs_s);
          emit_addimm(ESP,8,ESP);
          break;
      case 0x06:
          emit_call((int)mov_s);
          emit_addimm(ESP,8,ESP);
          break;
      case 0x07:
          emit_call((int)neg_s);
          emit_addimm(ESP,8,ESP);
          break;
    }
    emit_popa();
  }
  if(opcode2[i]==0x11) { // Double precision
    emit_pusha();
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>> 6)&0x1f]);
    if((source[i]&0x3f)<4)
      emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>16)&0x1f]);
    emit_pushmem((intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_regs_double[(source[i]>>11)&0x1f]);
    if((source[i]&0x3f)<=4)
    {
      if(fs>=0) emit_pushreg(fs);
      else emit_pushmem((int)&g_dev.r4300.new_dynarec_hot_state.fcr31);
    }
    switch(source[i]&0x3f)
    {
      case 0x00:
          emit_call((int)add_d);
          emit_addimm(ESP,16,ESP);
          break;
      case 0x01:
          emit_call((int)sub_d);
          emit_addimm(ESP,16,ESP);
          break;
      case 0x02:
          emit_call((int)mul_d);
          emit_addimm(ESP,16,ESP);
          break;
      case 0x03:
          emit_call((int)div_d);
          emit_addimm(ESP,16,ESP);
          break;
      case 0x04:
          emit_call((int)sqrt_d);
          emit_addimm(ESP,12,ESP);
          break;
      case 0x05:
          emit_call((int)abs_d);
          emit_addimm(ESP,8,ESP);
          break;
      case 0x06:
          emit_call((int)mov_d);
          emit_addimm(ESP,8,ESP);
          break;
      case 0x07:
          emit_call((int)neg_d);
          emit_addimm(ESP,8,ESP);
          break;
    }
    emit_popa();
  }
}

static void multdiv_assemble_x86(int i,struct regstat *i_regs)
{
  //  case 0x18: MULT
  //  case 0x19: MULTU
  //  case 0x1A: DIV
  //  case 0x1B: DIVU
  //  case 0x1C: DMULT
  //  case 0x1D: DMULTU
  //  case 0x1E: DDIV
  //  case 0x1F: DDIVU
  if(rs1[i]&&rs2[i])
  {
    if((opcode2[i]&4)==0) // 32-bit
    {
#ifndef INTERPRET_MULT
      if((opcode2[i]==0x18) || (opcode2[i]==0x19))
      {
        signed char m1=get_reg(i_regs->regmap,rs1[i]);
        signed char m2=get_reg(i_regs->regmap,rs2[i]);
        assert(m1>=0);
        assert(m2>=0);
        emit_mov(m1,EAX);

        if(opcode2[i]==0x18) //MULT
          emit_imul(m2);
        else if(opcode2[i]==0x19) //MULTU
          emit_mul(m2);
      }
      else
#endif
#ifndef INTERPRET_DIV
      if((opcode2[i]==0x1A) || (opcode2[i]==0x1B))
      {
        signed char d1=get_reg(i_regs->regmap,rs1[i]);
        signed char d2=get_reg(i_regs->regmap,rs2[i]);
        assert(d1>=0);
        assert(d2>=0);
        emit_mov(d1,EAX);

        if(opcode2[i]==0x1A) //DIV
        {
          emit_cdq();
          emit_test(d2,d2);
          emit_jeq((int)out+8);
          emit_idiv(d2);
        }
        else if(opcode2[i]==0x1B) //DIVU
        {
          emit_zeroreg(EDX);
          emit_test(d2,d2);
          emit_jeq((int)out+8);
          emit_div(d2);
        }
      }
      else
#endif
      {
        u_int reglist=0;
        signed char r1=get_reg(i_regs->regmap,rs1[i]);
        signed char r2=get_reg(i_regs->regmap,rs2[i]);
        signed char hi=get_reg(i_regs->regmap,HIREG);
        signed char lo=get_reg(i_regs->regmap,LOREG);
        assert(r1>=0);
        assert(r2>=0);

        emit_writeword(r1,(intptr_t)&g_dev.r4300.new_dynarec_hot_state.rs);
        emit_writeword(r2,(intptr_t)&g_dev.r4300.new_dynarec_hot_state.rt);
        emit_pusha();

        if(opcode2[i]==0x18)
          emit_call((intptr_t)cached_interp_MULT);
        else if(opcode2[i]==0x19)
          emit_call((intptr_t)cached_interp_MULTU);
        else if(opcode2[i]==0x1A)
          emit_call((intptr_t)cached_interp_DIV);
        else if(opcode2[i]==0x1B)
          emit_call((intptr_t)cached_interp_DIVU);

        emit_popa();
        if(hi>=0) emit_loadreg(HIREG,hi);
        if(lo>=0) emit_loadreg(LOREG,lo);
      }
    }
    else // 64-bit
    {
#ifndef INTERPRET_MULT64
      if(opcode2[i]==0x1C||opcode2[i]==0x1D)
      {
        signed char m1h=get_reg(i_regs->regmap,rs1[i]|64);
        signed char m1l=get_reg(i_regs->regmap,rs1[i]);
        signed char m2h=get_reg(i_regs->regmap,rs2[i]|64);
        signed char m2l=get_reg(i_regs->regmap,rs2[i]);
        signed char temp=get_reg(i_regs->regmap,-1);

        assert((m1h>=0)&&(m1h!=EAX)&&(m1h!=EDX));
        assert((m2h>=0)&&(m2h!=EAX)&&(m2h!=EDX));
        assert((m1l>=0)&&(m1l!=EAX)&&(m1l!=EDX));
        assert((m2l>=0)&&(m2l!=EAX)&&(m2l!=EDX));
        assert((temp>=0)&&(temp!=EAX)&&(temp!=EDX));

        if(opcode2[i]==0x1C) // DMULT
        {
          // Multiply m2l*m1l
          emit_mov(m1l,EAX);
          emit_mul(m2l);
          emit_storereg(LOREG,EAX);
          emit_mov(EDX,temp);

          // Multiply m2l*m1h
          emit_mov(m1h,EAX);
          emit_mul(m2l);
          emit_add(EAX,temp,temp);
          emit_adcimm(0,EDX);
          emit_storereg(HIREG,EDX);

          // Multiply m2h*m1l
          emit_mov(m1l,EAX);
          emit_mul(m2h);
          emit_add(EAX,temp,temp);
          emit_adcimm(0,EDX);
          emit_storereg(LOREG|64,temp);
          emit_mov(EDX,temp);

          // Multiply m2h*m1h
          emit_mov(m1h,EAX);
          emit_mul(m2h);
          emit_add(EAX,temp,EAX);
          emit_adcimm(0,EDX);
          emit_loadreg(HIREG,temp);
          emit_add(EAX,temp,EAX);
          emit_adcimm(0,EDX);

          // If m1<0 subtract m2 from the high 64bit part
          emit_testimm(m1h,0x80000000);
          emit_jeq((int)out+10);
          emit_sub(EAX,m2l,EAX);
          emit_sbb(m2h,EDX);

          // If m2<0 subtract m1 from the high 64bit part
          emit_testimm(m2h,0x80000000);
          emit_jeq((int)out+10);
          emit_sub(EAX,m1l,EAX);
          emit_sbb(m1h,EDX);
        }
        else if(opcode2[i]==0x1D) // DMULTU
        {
          // Multiply m2l*m1l
          emit_mov(m1l,EAX);
          emit_mul(m2l);
          emit_storereg(LOREG,EAX);
          emit_mov(EDX,temp);

          // Multiply m2l*m1h
          emit_mov(m1h,EAX);
          emit_mul(m2l);
          emit_add(EAX,temp,temp);
          emit_adcimm(0,EDX);
          emit_storereg(HIREG,EDX);

          // Multiply m2h*m1l
          emit_mov(m1l, EAX);
          emit_mul(m2h);
          emit_add(EAX,temp,temp);
          emit_adcimm(0,EDX);
          emit_storereg(LOREG|64,temp);
          emit_mov(EDX,temp);

          // Multiply m2h*m1h
          emit_mov(m1h,EAX);
          emit_mul(m2h);
          emit_add(EAX,temp,EAX);
          emit_adcimm(0, EDX);
          emit_loadreg(HIREG,temp);
          emit_add(EAX,temp,EAX);
          emit_adcimm(0,EDX);
        }
      }
      else
#endif
      {
        signed char r1h=get_reg(i_regs->regmap,rs1[i]|64);
        signed char r1l=get_reg(i_regs->regmap,rs1[i]);
        signed char r2h=get_reg(i_regs->regmap,rs2[i]|64);
        signed char r2l=get_reg(i_regs->regmap,rs2[i]);
        signed char hih=get_reg(i_regs->regmap,HIREG|64);
        signed char hil=get_reg(i_regs->regmap,HIREG);
        signed char loh=get_reg(i_regs->regmap,LOREG|64);
        signed char lol=get_reg(i_regs->regmap,LOREG);
        assert(r1h>=0);
        assert(r2h>=0);
        assert(r1l>=0);
        assert(r2l>=0);

        emit_writeword(r1l,(int)&g_dev.r4300.new_dynarec_hot_state.rs);
        emit_writeword(r1h,((int)&g_dev.r4300.new_dynarec_hot_state.rs)+4);
        emit_writeword(r2l,(int)&g_dev.r4300.new_dynarec_hot_state.rt);
        emit_writeword(r2h,((int)&g_dev.r4300.new_dynarec_hot_state.rt)+4);
        emit_pusha();

        if(opcode2[i]==0x1C) // DMULT
          emit_call((int)cached_interp_DMULT);
        else if(opcode2[i]==0x1D) // DMULTU
          emit_call((int)cached_interp_DMULTU);
        else if(opcode2[i]==0x1E) // DDIV
          emit_call((int)cached_interp_DDIV);
        else if(opcode2[i]==0x1F) // DDIVU
          emit_call((int)cached_interp_DDIVU);

        emit_popa();
        if(hih>=0) emit_loadreg(HIREG|64,hih);
        if(hil>=0) emit_loadreg(HIREG,hil);
        if(loh>=0) emit_loadreg(LOREG|64,loh);
        if(lol>=0) emit_loadreg(LOREG,lol);
      }
    }
  }
  else
  {
    // Multiply by zero is zero.
    // MIPS does not have a divide by zero exception.
    // The result is undefined, we return zero.
    signed char hr=get_reg(i_regs->regmap,HIREG);
    signed char lr=get_reg(i_regs->regmap,LOREG);
    if(hr>=0) emit_zeroreg(hr);
    if(lr>=0) emit_zeroreg(lr);
  }
}
#define multdiv_assemble multdiv_assemble_x86

static void do_preload_rhash(int r) {
  emit_movimm(0xf8,r);
}

static void do_preload_rhtbl(int r) {
  // Don't need this for x86
}

static void do_rhash(int rs,int rh) {
  emit_and(rs,rh,rh);
}

static void do_miniht_load(int ht,int rh) {
  // Don't need this for x86.  The load and compare can be combined into
  // a single instruction (below)
}

static void do_miniht_jump(int rs,int rh,int ht) {
  emit_cmpmem_indexed((int)g_dev.r4300.new_dynarec_hot_state.mini_ht,rh,rs);
  emit_jne(jump_vaddr_reg[rs]);
  emit_jmpmem_indexed((int)g_dev.r4300.new_dynarec_hot_state.mini_ht+4,rh);
}

static void do_miniht_insert(int return_address,int rt,int temp) {
  emit_movimm(return_address,rt); // PC into link register
  //emit_writeword_imm(return_address,(int)&g_dev.r4300.new_dynarec_hot_state.mini_ht[(return_address&0xFF)>>8][0]);
  emit_writeword(rt,(int)&g_dev.r4300.new_dynarec_hot_state.mini_ht[(return_address&0xFF)>>3][0]);
  add_to_linker((int)out,return_address,1);
  emit_writeword_imm(0,(int)&g_dev.r4300.new_dynarec_hot_state.mini_ht[(return_address&0xFF)>>3][1]);
}

// We don't need this for x86
static void literal_pool(int n) {}
static void literal_pool_jumpover(int n) {}

// CPU-architecture-specific initialization, not needed for x86
static void arch_init(void)
{
  g_dev.r4300.new_dynarec_hot_state.rounding_modes[0]=0x33F; // round
  g_dev.r4300.new_dynarec_hot_state.rounding_modes[1]=0xF3F; // trunc
  g_dev.r4300.new_dynarec_hot_state.rounding_modes[2]=0xB3F; // ceil
  g_dev.r4300.new_dynarec_hot_state.rounding_modes[3]=0x73F; // floor
}
