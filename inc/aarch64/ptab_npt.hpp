/*
 * Nested Page Table (NPT)
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

#pragma once

#include "ptab.hpp"
#include "vmid.hpp"

class Npt final : public Pagetable<Npt, uint64, uint64, 3, 3, true>::Entry
{
    private:
        enum
        {
            ATTR_P      = BIT64  (0),   // Present
            ATTR_nL     = BIT64  (1),   // Not Large
            ATTR_R      = BIT64  (6),   // Readable
            ATTR_W      = BIT64  (7),   // Writable
            ATTR_A      = BIT64 (10),   // Accessed
            ATTR_nX0    = BIT64 (53),   // Not Executable
            ATTR_nX1    = BIT64 (54),   // Not Executable
            ATTR_K      = BIT64 (55),   // Kernel Memory
        };

    public:
        static inline bool xnx { true };

        static constexpr OAddr ADDR_MASK { BIT64_RANGE (47, 12) };

        bool is_large (unsigned l) const { return              l && (val & (ATTR_nL | ATTR_P)) == (ATTR_P); }
        bool is_table (unsigned l) const { return l == lev || (l && (val & (ATTR_nL | ATTR_P)) == (ATTR_nL | ATTR_P)); }

        // Attributes for PTEs referring to page tables
        static OAddr ptab_attr()
        {
            return ATTR_nL | ATTR_P;
        }

        // Attributes for PTEs referring to leaf pages
        static OAddr page_attr (unsigned l, Paging::Permissions pm, Memattr::Cacheability ca, Memattr::Shareability sh)
        {
            bool nxs = !(pm & Paging::XS);
            bool nxu = !(pm & Paging::XU);

            return !(pm & Paging::API) ? 0 :
                     ATTR_K   * !!(pm & Paging::K)          |
                     ATTR_nX1 * ((nxs & nxu) | (xnx & nxu)) |
                     ATTR_nX0 * ((nxs ^ nxu) &  xnx)        |
                     ATTR_W   * !!(pm & Paging::W)          |
                     ATTR_R   * !!(pm & Paging::R)          |
                     std::to_underlying (sh) << 8 | Memattr::s2_attr (ca) << 2 | !l * ATTR_nL | ATTR_A | ATTR_P;
        }

        Paging::Permissions page_pm() const
        {
            return Paging::Permissions (!val ? 0 :
                                      !!(val & ATTR_K)                        * Paging::K  |
                                     !(!(val & ATTR_nX1) ^ !(val & ATTR_nX0)) * Paging::XS |
                                       !(val & ATTR_nX1)                      * Paging::XU |
                                      !!(val & ATTR_W)                        * Paging::W  |
                                      !!(val & ATTR_R)                        * Paging::R);
        }

        Memattr::Cacheability page_ca (unsigned) const { return Memattr::Cacheability (!!(val & BIT_RANGE (5, 4)) * 4 + (val >> 2 & BIT_RANGE (1, 0))); }

        Memattr::Shareability page_sh() const { return Memattr::Shareability (val >> 8 & BIT_RANGE (1, 0)); }

        // Needed by gcc version < 10
        ALWAYS_INLINE inline Npt() : Entry() {}
        ALWAYS_INLINE inline Npt (Entry x) : Entry (x) {}
};

class Nptp final : public Pagetable<Npt, uint64, uint64, 3, 3, true>
{
    private:
        static uint64 current CPULOCAL;

    public:
        // Constructor
        ALWAYS_INLINE
        inline explicit Nptp (OAddr v = 0) : Pagetable (Npt (v)) {}

        ALWAYS_INLINE
        inline void make_current (Vmid vmid) const
        {
            uint64 vttbr = static_cast<uint64>(vmid) << 48 | root_addr();

            if (current != vttbr)
                asm volatile ("msr vttbr_el2, %x0; isb" : : "rZ" (current = vttbr) : "memory");
        }

        ALWAYS_INLINE
        inline void invalidate (Vmid vmid) const
        {
            make_current (vmid);

            asm volatile ("dsb  ishst           ;"  // Ensure PTE writes have completed
                          "tlbi vmalls12e1is    ;"  // Invalidate TLB
                          "dsb  ish             ;"  // Ensure TLB invalidation completed
                          "isb                  ;"  // Ensure subsequent instructions use new PTEs
                          : : : "memory");
        }

        ALWAYS_INLINE
        static inline void init() { current = 0; }  // Reset at resume time
};
