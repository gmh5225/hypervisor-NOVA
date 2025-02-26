/*
 * Central Processing Unit (CPU)
 *
 * Copyright (C) 2019-2022 Udo Steinberg, BedRock Systems, Inc.
 *
 * This file is part of the NOVA microhypervisor.
 *
 * NOVA is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOVA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 */

#include "acpi.hpp"
#include "cache.hpp"
#include "cpu.hpp"
#include "fpu.hpp"
#include "gicc.hpp"
#include "gicd.hpp"
#include "gich.hpp"
#include "gicr.hpp"
#include "ptab_npt.hpp"
#include "stdio.hpp"
#include "timer.hpp"
#include "vmcb.hpp"

unsigned Cpu::id, Cpu::hazard;
bool Cpu::bsp;
uint64 Cpu::res0_hcr;
uint64 Cpu::ptab, Cpu::midr, Cpu::mpidr, Cpu::cptr, Cpu::mdcr;
uint64 Cpu::feat_cpu64[2], Cpu::feat_dbg64[2], Cpu::feat_isa64[3], Cpu::feat_mem64[3], Cpu::feat_sme64[1], Cpu::feat_sve64[1];
uint32 Cpu::feat_cpu32[3], Cpu::feat_dbg32[2], Cpu::feat_isa32[7], Cpu::feat_mem32[6], Cpu::feat_mfp32[3];

void Cpu::enumerate_features()
{
    uint64 ctr;

    asm volatile ("mrs %x0, ctr_el0"            : "=r" (ctr));
    asm volatile ("mrs %x0, midr_el1"           : "=r" (midr));
    asm volatile ("mrs %x0, mpidr_el1"          : "=r" (mpidr));

    Cache::init (4 << (ctr >> 16 & BIT_RANGE (3, 0)), 4 << (ctr & BIT_RANGE (3, 0)));

    asm volatile ("mrs %x0, id_aa64pfr0_el1"    : "=r" (feat_cpu64[0]));
    asm volatile ("mrs %x0, id_aa64pfr1_el1"    : "=r" (feat_cpu64[1]));
    asm volatile ("mrs %x0, id_aa64dfr0_el1"    : "=r" (feat_dbg64[0]));
    asm volatile ("mrs %x0, id_aa64dfr1_el1"    : "=r" (feat_dbg64[1]));
    asm volatile ("mrs %x0, id_aa64isar0_el1"   : "=r" (feat_isa64[0]));
    asm volatile ("mrs %x0, id_aa64isar1_el1"   : "=r" (feat_isa64[1]));
    asm volatile ("mrs %x0, id_aa64isar2_el1"   : "=r" (feat_isa64[2]));
    asm volatile ("mrs %x0, id_aa64mmfr0_el1"   : "=r" (feat_mem64[0]));
    asm volatile ("mrs %x0, id_aa64mmfr1_el1"   : "=r" (feat_mem64[1]));
    asm volatile ("mrs %x0, id_aa64mmfr2_el1"   : "=r" (feat_mem64[2]));
//  asm volatile ("mrs %x0, id_aa64smfr0_el1"   : "=r" (feat_sme64[0]));    // SME
//  asm volatile ("mrs %x0, id_aa64zfr0_el1"    : "=r" (feat_sve64[0]));    // SVE

    if (feature (Cpu_feature::EL1) == 2) {
        asm volatile ("mrs %x0, id_pfr0_el1"    : "=r" (feat_cpu32[0]));
        asm volatile ("mrs %x0, id_pfr1_el1"    : "=r" (feat_cpu32[1]));
//      asm volatile ("mrs %x0, id_pfr2_el1"    : "=r" (feat_cpu32[2]));
        asm volatile ("mrs %x0, id_dfr0_el1"    : "=r" (feat_dbg32[0]));
        asm volatile ("mrs %x0, id_dfr1_el1"    : "=r" (feat_dbg32[1]));
        asm volatile ("mrs %x0, id_isar0_el1"   : "=r" (feat_isa32[0]));
        asm volatile ("mrs %x0, id_isar1_el1"   : "=r" (feat_isa32[1]));
        asm volatile ("mrs %x0, id_isar2_el1"   : "=r" (feat_isa32[2]));
        asm volatile ("mrs %x0, id_isar3_el1"   : "=r" (feat_isa32[3]));
        asm volatile ("mrs %x0, id_isar4_el1"   : "=r" (feat_isa32[4]));
        asm volatile ("mrs %x0, id_isar5_el1"   : "=r" (feat_isa32[5]));
        asm volatile ("mrs %x0, id_isar6_el1"   : "=r" (feat_isa32[6]));
        asm volatile ("mrs %x0, id_mmfr0_el1"   : "=r" (feat_mem32[0]));
        asm volatile ("mrs %x0, id_mmfr1_el1"   : "=r" (feat_mem32[1]));
        asm volatile ("mrs %x0, id_mmfr2_el1"   : "=r" (feat_mem32[2]));
        asm volatile ("mrs %x0, id_mmfr3_el1"   : "=r" (feat_mem32[3]));
        asm volatile ("mrs %x0, id_mmfr4_el1"   : "=r" (feat_mem32[4]));
        asm volatile ("mrs %x0, id_mmfr5_el1"   : "=r" (feat_mem32[5]));
    }

    asm volatile ("mrs %x0, mvfr0_el1"          : "=r" (feat_mfp32[0]));
    asm volatile ("mrs %x0, mvfr1_el1"          : "=r" (feat_mfp32[1]));
    asm volatile ("mrs %x0, mvfr2_el1"          : "=r" (feat_mfp32[2]));

    if (!feature (Mem_feature::XNX))
        Npt::xnx = false;

    uint64 res1_cptr =  (feature (Cpu_feature::SME)     >=  1 ? 0 : CPTR_TSM)   |
                        (feature (Cpu_feature::FP)      != 15 ||
                         feature (Cpu_feature::ADVSIMD) != 15 ? 0 : CPTR_TFP)   |
                        (feature (Cpu_feature::SVE)     >=  1 ? 0 : CPTR_TZ)    |
                        CPTR_RES1;

    uint64 res0_cptr =  (feature (Cpu_feature::AMU)     >=  1 ? 0 : CPTR_TAM)   |
                        CPTR_RES0;

    uint64 res1_mdcr =  MDCR_RES1;

    uint64 res0_mdcr =  (feature (Dbg_feature::PMSVER)      >= 3 ? 0 : MDCR_HPMFZS)                                     |
                        (feature (Dbg_feature::PMSVER)      >= 2 ? 0 : MDCR_TPMS | MDCR_E2PB)                           |
                        (feature (Dbg_feature::PMUVER)      >= 7 ? 0 : MDCR_HPMFZO)                                     |
                        (feature (Dbg_feature::PMUVER)      >= 6 ? 0 : MDCR_HLP | MDCR_HCCD)                            |
                        (feature (Dbg_feature::PMUVER)      >= 4 ? 0 : MDCR_HPMD)                                       |
                        (feature (Dbg_feature::PMUVER)      >= 1 ? 0 : MDCR_HPME | MDCR_TPM | MDCR_TPMCR | MDCR_HPMN)   |
                        (feature (Dbg_feature::MTPMU)       >= 1 ? 0 : MDCR_MTPME)                                      |
                        (feature (Mem_feature::FGT)         >= 1 ? 0 : MDCR_TDCC)                                       |
                        (feature (Dbg_feature::TRACEBUFFER) >= 1 ? 0 : MDCR_E2TB)                                       |
                        (feature (Dbg_feature::TRACEFILT)   >= 1 ? 0 : MDCR_TTRF)                                       |
                        MDCR_RES0;

    cptr = ((hyp1_cptr & ~hyp0_cptr) | res1_cptr) & ~res0_cptr;
    mdcr = ((hyp1_mdcr & ~hyp0_mdcr) | res1_mdcr) & ~res0_mdcr;

    res0_hcr  = (feature (Mem_feature::TWED) >= 1 ? 0 : HCR_TWEDEL | HCR_TWEDEn)            |
                (feature (Cpu_feature::MTE)  >= 2 ? 0 : HCR_TID5 | HCR_DCT | HCR_ATA)       |
                (feature (Mem_feature::EVT)  >= 2 ? 0 : HCR_TTLBOS | HCR_TTLBIS)            |
                (feature (Mem_feature::EVT)  >= 1 ? 0 : HCR_TOCU | HCR_TICAB | HCR_TID4)    |
                (feature (Cpu_feature::CSV2) >= 2 ? 0 : HCR_ENSCXT)                         |
                (feature (Cpu_feature::AMU)  >= 2 ? 0 : HCR_AMVOFFEN)                       |
                (feature (Cpu_feature::RME)  >= 1 ? 0 : HCR_GPF)                            |
                (feature (Cpu_feature::RAS)  >= 2 ? 0 : HCR_FIEN)                           |
                (feature (Cpu_feature::RAS)  >= 1 ? 0 : HCR_TEA | HCR_TERR)                 |
                (feature (Mem_feature::FWB)  >= 1 ? 0 : HCR_FWB)                            |
                (feature (Mem_feature::NV)   >= 2 ? 0 : HCR_NV2)                            |
                (feature (Mem_feature::NV)   >= 1 ? 0 : HCR_AT | HCR_NV1 | HCR_NV)          |
                (feature (Isa_feature::APA)  >= 1 ||
                 feature (Isa_feature::API)  >= 1 ||
                 feature (Isa_feature::GPA)  >= 1 ||
                 feature (Isa_feature::GPI)  >= 1 ? 0 : HCR_API | HCR_APK)                  |
                (feature (Mem_feature::LO)   >= 1 ? 0 : HCR_TLOR)                           |
                (feature (Mem_feature::VH)   >= 1 ? 0 : HCR_E2H)                            |
                (feature (Isa_feature::TLB)  >= 1 ? 0 : HCR_TTLB)                           |
                (feature (Isa_feature::DPB)  >= 1 ? 0 : HCR_TPCP);
}

void Cpu::init (unsigned cpu, unsigned e)
{
    if (Acpi::resume)
        hazard = 0;

    else {
        for (void (**func)() = &CTORS_L; func != &CTORS_C; (*func++)()) ;

        hazard = Hazard::BOOT_GST | Hazard::BOOT_HST;

        id   = cpu;
        bsp  = cpu == boot_cpu;
        ptab = Hptp::current().root_addr();

        enumerate_features();
    }

    char const *impl = "Unknown", *part = impl;

    switch (midr >> 24 & BIT_RANGE (7, 0)) {

        case 0x41:
            impl = "ARM Limited";
            switch (midr >> 4 & BIT_RANGE (11, 0)) {
                case 0xd02: part = "Cortex-A34 (Metis)";            break;
                case 0xd03: part = "Cortex-A53 (Apollo)";           break;
                case 0xd04: part = "Cortex-A35 (Mercury)";          break;
                case 0xd05: part = "Cortex-A55 (Ananke)";           break;
                case 0xd06: part = "Cortex-A65 (Helios)";           break;
                case 0xd07: part = "Cortex-A57 (Atlas)";            break;
                case 0xd08: part = "Cortex-A72 (Maia)";             break;
                case 0xd09: part = "Cortex-A73 (Artemis)";          break;
                case 0xd0a: part = "Cortex-A75 (Prometheus)";       break;
                case 0xd0b: part = "Cortex-A76 (Enyo)";             break;
                case 0xd0c: part = "Neoverse N1 (Ares)";            break;
                case 0xd0d: part = "Cortex-A77 (Deimos)";           break;
                case 0xd0e: part = "Cortex-A76AE (Enyo-AE)";        break;
                case 0xd40: part = "Neoverse V1 (Zeus)";            break;
                case 0xd41: part = "Cortex-A78 (Hercules)";         break;
                case 0xd42: part = "Cortex-A78AE (Hercules-AE)";    break;
                case 0xd43: part = "Cortex-A65AE (Helios-AE)";      break;
                case 0xd44: part = "Cortex-X1 (Hera)";              break;
                case 0xd46: part = "Cortex-A510 (Klein)";           break;
                case 0xd47: part = "Cortex-A710 (Matterhorn)";      break;
                case 0xd48: part = "Cortex-X2 (Matterhorn ELP)";    break;
                case 0xd49: part = "Neoverse N2 (Perseus)";         break;
                case 0xd4a: part = "Neoverse E1 (Helios)";          break;
                case 0xd4b: part = "Cortex-A78C (Hercules-C)";      break;
                case 0xd4c: part = "Cortex-X1C (Hera-C)";           break;
                case 0xd4d: part = "Cortex-A715 (Makalu)";          break;
                case 0xd4e: part = "Cortex-X3 (Makalu ELP)";        break;
                case 0xd4f: part = "Neoverse (Demeter)";            break;
                case 0xd80: part = "Cortex (Hayes)";                break;
                case 0xd81: part = "Cortex (Hunter)";               break;
                case 0xd83: part = "Neoverse (Poseidon)";           break;
            }
            break;

        case 0x4e:
            impl = "NVIDIA Corporation";
            switch (midr >> 4 & BIT_RANGE (11, 0)) {
                case 0x003: part = "Denver";                        break;
                case 0x004: part = "Carmel";                        break;
            }
            break;

        case 0x51:
            impl = "Qualcomm Inc.";
            switch (midr >> 4 & BIT_RANGE (11, 0)) {
                case 0x800: part = "Kryo 2xx Gold";                 break;
                case 0x801: part = "Kryo 2xx Silver";               break;
                case 0x802: part = "Kryo 3xx Gold";                 break;
                case 0x803: part = "Kryo 3xx Silver";               break;
                case 0x804: part = "Kryo 4xx Gold";                 break;
                case 0x805: part = "Kryo 4xx Silver";               break;
                case 0xc00: part = "Falkor";                        break;
                case 0xc01: part = "Saphira";                       break;
            }
            break;
    }

    trace (TRACE_CPU, "CORE: %x:%x:%x %s %s r%llup%llu PA:%u XNX:%u GIC:%u (EL%u)",
           affinity() >> 16 & BIT_RANGE (7, 0), affinity() >> 8 & BIT_RANGE (7, 0), affinity() & BIT_RANGE (7, 0),
           impl, part, midr >> 20 & BIT_RANGE (3, 0), midr & BIT_RANGE (3, 0),
           feature (Mem_feature::PARANGE), feature (Mem_feature::XNX), feature (Cpu_feature::GIC), e);

    Gicd::init();
    Gicr::init();
    Gicc::init();
    Gich::init();

    Timer::init();

    Nptp::init();
    Vmcb::init();

    boot_lock.unlock();
}

void Cpu::fini()
{
    auto s = Acpi::get_transition();

    if (s.state() > 1)
        Fpu::fini();

    Acpi::fini (s);
}

void Cpu::set_vmm_regs (uintptr_t (&x)[31], uint64 &hcr, uint64 &vpidr, uint64 &vmpidr, uint32 &elrsr)
{
    x[0]  = feat_cpu64[0]; x[1] = feat_cpu64[1];
    x[2]  = feat_dbg64[0]; x[3] = feat_dbg64[1];
    x[4]  = feat_isa64[0]; x[5] = feat_isa64[1]; x[6] = feat_isa64[2];
    x[7]  = feat_mem64[0]; x[8] = feat_mem64[1]; x[9] = feat_mem64[2];
    x[10] = feat_sme64[0]; x[11] = feat_sve64[0];

    x[16] = static_cast<uint64>(feat_cpu32[1]) << 32 | feat_cpu32[0];
    x[17] = static_cast<uint64>(feat_dbg32[0]) << 32 | feat_cpu32[2];
    x[18] = static_cast<uint64>(feat_isa32[0]) << 32 | feat_dbg32[1];
    x[19] = static_cast<uint64>(feat_isa32[2]) << 32 | feat_isa32[1];
    x[20] = static_cast<uint64>(feat_isa32[4]) << 32 | feat_isa32[3];
    x[21] = static_cast<uint64>(feat_isa32[6]) << 32 | feat_isa32[5];
    x[22] = static_cast<uint64>(feat_mem32[1]) << 32 | feat_mem32[0];
    x[23] = static_cast<uint64>(feat_mem32[3]) << 32 | feat_mem32[2];
    x[24] = static_cast<uint64>(feat_mem32[5]) << 32 | feat_mem32[4];

    x[29] = static_cast<uint64>(feat_mfp32[1]) << 32 | feat_mfp32[0];
    x[30] =                                            feat_mfp32[2];

    hcr    = constrain_hcr (0);
    vpidr  = midr;
    vmpidr = mpidr;
    elrsr  = BIT (Gich::num_lr) - 1;
}
