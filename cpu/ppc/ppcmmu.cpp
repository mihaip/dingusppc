//DingusPPC
//Written by divingkatae and maximum
//(c)2018-20 (theweirdo)     spatium
//Please ask for permission
//if you want to distribute this.
//(divingkatae#1017 or powermax#2286 on Discord)

/** @file PowerPC Memory management unit emulation. */

/* TODO:
    - implement TLB
    - implement 601-style BATs
    - implement BAT access check
    - add proper error and exception handling
    - clarify what to do in the case of unaligned memory accesses
    - remove dependency on MPC106 (use generic memory controller interface instead)
 */

#include <iostream>
#include <cstdint>
#include <cinttypes>
#include <string>
#include <array>
#include "memreadwrite.h"
#include "ppcemu.h"
#include "ppcmmu.h"
#include "devices/memctrlbase.h"
#include "devices/mmiodevice.h"
#include "devices/mpc106.h"


 /** PowerPC-style MMU BAT arrays (NULL initialization isn't prescribed). */
PPC_BAT_entry ibat_array[4] = { {0} };
PPC_BAT_entry dbat_array[4] = { {0} };

/** remember recently used physical memory regions for quicker translation. */
AddressMapEntry last_read_area  = { 0 };
AddressMapEntry last_write_area = { 0 };
AddressMapEntry last_exec_area  = { 0 };
AddressMapEntry last_ptab_area  = { 0 };


/* macro for generating code reading from physical memory */
#define READ_PHYS_MEM(ENTRY, ADDR, OP, SIZE, UNVAL)                            \
{                                                                              \
    if ((ADDR) >= (ENTRY).start && (ADDR) <= (ENTRY).end) {                    \
        ret = OP((ENTRY).mem_ptr + ((ADDR) - (ENTRY).start));                  \
    } else {                                                                   \
        AddressMapEntry* entry = mem_ctrl_instance->find_range((ADDR));        \
        if (entry) {                                                           \
            if (entry->type & (RT_ROM | RT_RAM)) {                             \
                (ENTRY).start   = entry->start;                                \
                (ENTRY).end     = entry->end;                                  \
                (ENTRY).mem_ptr = entry->mem_ptr;                              \
                ret = OP((ENTRY).mem_ptr + ((ADDR) - (ENTRY).start));          \
            }                                                                  \
            else if (entry->type & RT_MMIO) {                                  \
                ret = entry->devobj->read((ADDR) - entry->start, (SIZE));      \
            }                                                                  \
            else {                                                             \
                printf("Please check your address map!\n");                    \
                ret = (UNVAL);                                                 \
            }                                                                  \
        }                                                                      \
        else {                                                                 \
            printf("WARNING: read from unmapped memory at 0x%08X!\n", (ADDR)); \
            ret = (UNVAL);                                                     \
        }                                                                      \
    }                                                                          \
}

/* macro for generating code writing to physical memory */
#define WRITE_PHYS_MEM(ENTRY, ADDR, OP, VAL, SIZE)                             \
{                                                                              \
    if ((ADDR) >= (ENTRY).start && (ADDR) <= (ENTRY).end) {                    \
        OP((ENTRY).mem_ptr + ((ADDR) - (ENTRY).start), (VAL));                 \
    } else {                                                                   \
        AddressMapEntry* entry = mem_ctrl_instance->find_range((ADDR));        \
        if (entry) {                                                           \
            if (entry->type & RT_RAM) {                                        \
                (ENTRY).start   = entry->start;                                \
                (ENTRY).end     = entry->end;                                  \
                (ENTRY).mem_ptr = entry->mem_ptr;                              \
                OP((ENTRY).mem_ptr + ((ADDR) - (ENTRY).start), (VAL));         \
            }                                                                  \
            else if (entry->type & RT_MMIO) {                                  \
                entry->devobj->write((ADDR) - entry->start, (VAL), (SIZE));    \
            }                                                                  \
            else {                                                             \
                printf("Please check your address map!\n");                    \
            }                                                                  \
        }                                                                      \
        else {                                                                 \
            printf("WARNING: write to unmapped memory at 0x%08X!\n", (ADDR));  \
        }                                                                      \
    }                                                                          \
}

void ppc_set_cur_instruction(const uint8_t* ptr)
{
    ppc_cur_instruction = READ_DWORD_BE_A(ptr);
}

static inline void ppc_memstore_16bit(unsigned char* ptr, uint16_t value, uint32_t offset) {
    if (ppc_state.ppc_msr & 1) { /* little-endian byte ordering */
        ptr[offset] = value & 0xFF;
        ptr[offset + 1] = (value >> 8) & 0xFF;
    }
    else { /* big-endian byte ordering */
        ptr[offset] = (value >> 8) & 0xFF;
        ptr[offset + 1] = value & 0xFF;
    }
}

static inline void ppc_memstore_32bit(unsigned char* ptr, uint32_t value, uint32_t offset) {
    if (ppc_state.ppc_msr & 1) { /* little-endian byte ordering */
        ptr[offset] = value & 0xFF;
        ptr[offset + 1] = (value >> 8) & 0xFF;
        ptr[offset + 2] = (value >> 16) & 0xFF;
        ptr[offset + 3] = (value >> 24) & 0xFF;
    }
    else { /* big-endian byte ordering */
        ptr[offset] = (value >> 24) & 0xFF;
        ptr[offset + 1] = (value >> 16) & 0xFF;
        ptr[offset + 2] = (value >> 8) & 0xFF;
        ptr[offset + 3] = value & 0xFF;
    }
}

static inline void ppc_memstore_64bit(unsigned char* ptr, uint64_t value, uint32_t offset) {
    if (ppc_state.ppc_msr & 1) { /* little-endian byte ordering */
        ptr[offset] = value & 0xFF;
        ptr[offset + 1] = (value >> 8) & 0xFF;
        ptr[offset + 2] = (value >> 16) & 0xFF;
        ptr[offset + 3] = (value >> 24) & 0xFF;
        ptr[offset + 4] = (value >> 32) & 0xFF;
        ptr[offset + 5] = (value >> 40) & 0xFF;
        ptr[offset + 6] = (value >> 48) & 0xFF;
        ptr[offset + 7] = (value >> 56) & 0xFF;
    }
    else { /* big-endian byte ordering */
        ptr[offset] = (value >> 56) & 0xFF;
        ptr[offset + 1] = (value >> 48) & 0xFF;
        ptr[offset + 2] = (value >> 40) & 0xFF;
        ptr[offset + 3] = (value >> 32) & 0xFF;
        ptr[offset + 4] = (value >> 24) & 0xFF;
        ptr[offset + 5] = (value >> 16) & 0xFF;
        ptr[offset + 6] = (value >> 8) & 0xFF;
        ptr[offset + 7] = value & 0xFF;
    }
}


void ibat_update(uint32_t bat_reg)
{
    int upper_reg_num;
    uint32_t bl, lo_mask;
    PPC_BAT_entry* bat_entry;

    upper_reg_num = bat_reg & 0xFFFFFFFE;

    if (ppc_state.ppc_spr[upper_reg_num] & 3) { // is that BAT pair valid?
        bat_entry = &ibat_array[(bat_reg - 528) >> 1];
        bl = (ppc_state.ppc_spr[upper_reg_num] >> 2) & 0x7FF;
        lo_mask = (bl << 17) | 0x1FFFF;

        bat_entry->access = ppc_state.ppc_spr[upper_reg_num] & 3;
        bat_entry->prot = ppc_state.ppc_spr[upper_reg_num + 1] & 3;
        bat_entry->lo_mask = lo_mask;
        bat_entry->phys_hi = ppc_state.ppc_spr[upper_reg_num + 1] & ~lo_mask;
        bat_entry->bepi = ppc_state.ppc_spr[upper_reg_num] & ~lo_mask;
    }
}

void dbat_update(uint32_t bat_reg)
{
    int upper_reg_num;
    uint32_t bl, lo_mask;
    PPC_BAT_entry* bat_entry;

    upper_reg_num = bat_reg & 0xFFFFFFFE;

    if (ppc_state.ppc_spr[upper_reg_num] & 3) { // is that BAT pair valid?
        bat_entry = &dbat_array[(bat_reg - 536) >> 1];
        bl = (ppc_state.ppc_spr[upper_reg_num] >> 2) & 0x7FF;
        lo_mask = (bl << 17) | 0x1FFFF;

        bat_entry->access = ppc_state.ppc_spr[upper_reg_num] & 3;
        bat_entry->prot = ppc_state.ppc_spr[upper_reg_num + 1] & 3;
        bat_entry->lo_mask = lo_mask;
        bat_entry->phys_hi = ppc_state.ppc_spr[upper_reg_num + 1] & ~lo_mask;
        bat_entry->bepi = ppc_state.ppc_spr[upper_reg_num] & ~lo_mask;
    }
}

static inline uint8_t* calc_pteg_addr(uint32_t hash)
{
    uint32_t sdr1_val, pteg_addr;

    sdr1_val = ppc_state.ppc_spr[25];

    pteg_addr = sdr1_val & 0xFE000000;
    pteg_addr |= (sdr1_val & 0x01FF0000) |
        (((sdr1_val & 0x1FF) << 16) & ((hash & 0x7FC00) << 6));
    pteg_addr |= (hash & 0x3FF) << 6;

    if (pteg_addr >= last_ptab_area.start && pteg_addr <= last_ptab_area.end) {
        return last_ptab_area.mem_ptr + (pteg_addr - last_ptab_area.start);
    }
    else {
        AddressMapEntry* entry = mem_ctrl_instance->find_range(pteg_addr);
        if (entry && entry->type & (RT_ROM | RT_RAM)) {
            last_ptab_area.start   = entry->start;
            last_ptab_area.end     = entry->end;
            last_ptab_area.mem_ptr = entry->mem_ptr;
            return last_ptab_area.mem_ptr + (pteg_addr - last_ptab_area.start);
        }
        else {
            printf("SOS: no page table region was found at %08X!\n", pteg_addr);
            exit(-1); // FIXME: ugly error handling, must be the proper exception!
        }
    }
}

static bool search_pteg(uint8_t* pteg_addr, uint8_t** ret_pte_addr,
    uint32_t vsid, uint16_t page_index, uint8_t pteg_num)
{
    /* construct PTE matching word */
    uint32_t pte_check = 0x80000000 | (vsid << 7) | (pteg_num << 6) |
        (page_index >> 10);

#ifdef MMU_INTEGRITY_CHECKS
    /* PTEG integrity check that ensures that all matching PTEs have
       identical RPN, WIMG and PP bits (PPC PEM 32-bit 7.6.2, rule 5). */
    uint32_t pte_word2_check;
    bool match_found = false;

    for (int i = 0; i < 8; i++, pteg_addr += 8) {
        if (pte_check == READ_DWORD_BE_A(pteg_addr)) {
            if (match_found) {
                if ((READ_DWORD_BE_A(pteg_addr) & 0xFFFFF07B) != pte_word2_check) {
                    printf("Multiple PTEs with different RPN/WIMG/PP found!\n");
                    exit(-1);
                }
            }
            else {
                /* isolate RPN, WIMG and PP fields */
                pte_word2_check = READ_DWORD_BE_A(pteg_addr) & 0xFFFFF07B;
                *ret_pte_addr = pteg_addr;
            }
        }
    }
#else
    for (int i = 0; i < 8; i++, pteg_addr += 8) {
        if (pte_check == READ_DWORD_BE_A(pteg_addr)) {
            *ret_pte_addr = pteg_addr;
            return true;
        }
    }
#endif

    return false;
}

static uint32_t page_address_translate(uint32_t la, bool is_instr_fetch,
    unsigned msr_pr, int is_write)
{
    uint32_t sr_val, page_index, pteg_hash1, vsid, pte_word2;
    unsigned key, pp;
    uint8_t* pte_addr;

    sr_val = ppc_state.ppc_sr[(la >> 28) & 0x0F];
    if (sr_val & 0x80000000) {
        printf("Direct-store segments not supported, LA=%0xX\n", la);
        exit(-1); // FIXME: ugly error handling, must be the proper exception!
    }

    /* instruction fetch from a no-execute segment will cause ISI exception */
    if ((sr_val & 0x10000000) && is_instr_fetch) {
        ppc_exception_handler(Except_Type::EXC_ISI, 0x10000000);
    }

    page_index = (la >> 12) & 0xFFFF;
    pteg_hash1 = (sr_val & 0x7FFFF) ^ page_index;
    vsid = sr_val & 0x0FFFFFF;

    if (!search_pteg(calc_pteg_addr(pteg_hash1), &pte_addr, vsid, page_index, 0)) {
        if (!search_pteg(calc_pteg_addr(~pteg_hash1), &pte_addr, vsid, page_index, 1)) {
            if (is_instr_fetch) {
                ppc_exception_handler(Except_Type::EXC_ISI, 0x40000000);
            }
            else {
                ppc_state.ppc_spr[18] = 0x40000000 | (is_write << 25);
                ppc_state.ppc_spr[19] = la;
                ppc_exception_handler(Except_Type::EXC_DSI, 0);
            }
        }
    }

    pte_word2 = READ_DWORD_BE_A(pte_addr + 4);

    key = (((sr_val >> 29) & 1)& msr_pr) | (((sr_val >> 30) & 1)& (msr_pr ^ 1));

    /* check page access */
    pp = pte_word2 & 3;

    // the following scenarios cause DSI/ISI exception:
    // any access with key = 1 and PP = %00
    // write access with key = 1 and PP = %01
    // write access with PP = %11
    if ((key && (!pp || (pp == 1 && is_write))) || (pp == 3 && is_write)) {
        if (is_instr_fetch) {
            ppc_exception_handler(Except_Type::EXC_ISI, 0x08000000);
        }
        else {
            ppc_state.ppc_spr[18] = 0x08000000 | (is_write << 25);
            ppc_state.ppc_spr[19] = la;
            ppc_exception_handler(Except_Type::EXC_DSI, 0);
        }
    }

    /* update R and C bits */
    /* For simplicity, R is set on each access, C is set only for writes */
    pte_addr[6] |= 0x01;
    if (is_write) {
        pte_addr[7] |= 0x80;
    }

    /* return physical address */
    return ((pte_word2 & 0xFFFFF000) | (la & 0x00000FFF));
}

/** PowerPC-style MMU instruction address translation. */
static uint32_t ppc_mmu_instr_translate(uint32_t la)
{
    uint32_t pa; /* translated physical address */

    bool bat_hit = false;
    unsigned msr_pr = !!(ppc_state.ppc_msr & 0x4000);

    // Format: %XY
    // X - supervisor access bit, Y - problem/user access bit
    // Those bits are mutually exclusive
    unsigned access_bits = (~msr_pr << 1) | msr_pr;

    for (int bat_index = 0; bat_index < 4; bat_index++) {
        PPC_BAT_entry* bat_entry = &ibat_array[bat_index];

        if ((bat_entry->access & access_bits) &&
            ((la & ~bat_entry->lo_mask) == bat_entry->bepi)) {
            bat_hit = true;
            // TODO: check access

            // logical to physical translation
            pa = bat_entry->phys_hi | (la & bat_entry->lo_mask);
            break;
        }
    }

    /* page address translation */
    if (!bat_hit) {
        pa = page_address_translate(la, true, msr_pr, 0);
    }

    return pa;
}

/** PowerPC-style MMU data address translation. */
static uint32_t ppc_mmu_addr_translate(uint32_t la, int is_write)
{
#if PROFILER
    mmu_translations_num++;
#endif

    uint32_t pa; /* translated physical address */

    bool bat_hit = false;
    unsigned msr_pr = !!(ppc_state.ppc_msr & 0x4000);

    // Format: %XY
    // X - supervisor access bit, Y - problem/user access bit
    // Those bits are mutually exclusive
    unsigned access_bits = (~msr_pr << 1) | msr_pr;

    for (int bat_index = 0; bat_index < 4; bat_index++) {
        PPC_BAT_entry* bat_entry = &dbat_array[bat_index];

        if ((bat_entry->access & access_bits) &&
            ((la & ~bat_entry->lo_mask) == bat_entry->bepi)) {
            bat_hit = true;
            // TODO: check access

            // logical to physical translation
            pa = bat_entry->phys_hi | (la & bat_entry->lo_mask);
            break;
        }
    }

    /* page address translation */
    if (!bat_hit) {
        pa = page_address_translate(la, false, msr_pr, is_write);
    }

    return pa;
}

void address_insert8bit_translate(uint8_t value, uint32_t addr)
{
    /* data address translation if enabled */
    if (ppc_state.ppc_msr & 0x10) {
        addr = ppc_mmu_addr_translate(addr, 1);
    }

    #define WRITE_BYTE(addr, val) (*(addr) = val)

    WRITE_PHYS_MEM(last_write_area, addr, WRITE_BYTE, value, 1);
}

void address_insert16bit_translate(uint16_t value, uint32_t addr)
{
    /* data address translation if enabled */
    if (ppc_state.ppc_msr & 0x10) {
        addr = ppc_mmu_addr_translate(addr, 1);
    }

    WRITE_PHYS_MEM(last_write_area, addr, WRITE_WORD_BE_A, value, 2);
}

void address_insert32bit_translate(uint32_t value, uint32_t addr)
{
    /* data address translation if enabled */
    if (ppc_state.ppc_msr & 0x10) {
        addr = ppc_mmu_addr_translate(addr, 1);
    }

    WRITE_PHYS_MEM(last_write_area, addr, WRITE_DWORD_BE_A, value, 4);
}

void address_insert64bit_translate(uint64_t value, uint32_t addr)
{
    /* data address translation if enabled */
    if (ppc_state.ppc_msr & 0x10) {
        addr = ppc_mmu_addr_translate(addr, 1);
    }

    WRITE_PHYS_MEM(last_write_area, addr, WRITE_QWORD_BE_A, value, 8);
}

/** Grab a value from memory into a register */
void address_grab8bit_translate(uint32_t addr)
{
    uint8_t ret;

    /* data address translation if enabled */
    if (ppc_state.ppc_msr & 0x10) {
        addr = ppc_mmu_addr_translate(addr, 0);
    }

    READ_PHYS_MEM(last_read_area, addr, *, 1, 0xFFU);
    return_value = ret;
}

void address_grab16bit_translate(uint32_t addr)
{
    uint16_t ret;

    /* data address translation if enabled */
    if (ppc_state.ppc_msr & 0x10) {
        addr = ppc_mmu_addr_translate(addr, 0);
    }

    READ_PHYS_MEM(last_read_area, addr, READ_WORD_BE_A, 2, 0xFFFFU);
    return_value = ret;
}

void address_grab32bit_translate(uint32_t addr)
{
    uint32_t ret;

    /* data address translation if enabled */
    if (ppc_state.ppc_msr & 0x10) {
        addr = ppc_mmu_addr_translate(addr, 0);
    }

    READ_PHYS_MEM(last_read_area, addr, READ_DWORD_BE_A, 4, 0xFFFFFFFFUL);
    return_value = ret;
}

void address_grab64bit_translate(uint32_t addr)
{
    uint64_t ret;

    /* data address translation if enabled */
    if (ppc_state.ppc_msr & 0x10) {
        addr = ppc_mmu_addr_translate(addr, 0);
    }

    READ_PHYS_MEM(last_read_area, addr, READ_QWORD_BE_A, 8, 0xFFFFFFFFFFFFFFFFULL);
    return_value = ret;
}

uint8_t* quickinstruction_translate(uint32_t addr)
{
    uint8_t* real_addr;

    /* perform instruction address translation if enabled */
    if (ppc_state.ppc_msr & 0x20) {
        addr = ppc_mmu_instr_translate(addr);
    }

    if (addr >= last_exec_area.start && addr <= last_exec_area.end) {
        real_addr = last_exec_area.mem_ptr + (addr - last_exec_area.start);
        ppc_set_cur_instruction(real_addr);
    }
    else {
        AddressMapEntry* entry = mem_ctrl_instance->find_range(addr);
        if (entry && entry->type & (RT_ROM | RT_RAM)) {
            last_exec_area.start   = entry->start;
            last_exec_area.end     = entry->end;
            last_exec_area.mem_ptr = entry->mem_ptr;
            real_addr = last_exec_area.mem_ptr + (addr - last_exec_area.start);
            ppc_set_cur_instruction(real_addr);
        }
        else {
            printf("WARNING: attempt to execute code at %08X!\n", addr);
            exit(-1); // FIXME: ugly error handling, must be the proper exception!
        }
    }

    return real_addr;
}
