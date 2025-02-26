/*
 * UART Console (PIO)
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

#include "cmdline.hpp"
#include "console_uart.hpp"

class Console_uart_pio : protected Console_uart
{
    protected:
        uint16 port_base { 0 };

        inline explicit Console_uart_pio (unsigned c) : Console_uart (c) {}

        void pmap (uint16 p)
        {
            if (Cmdline::nouart || port_base || !p)
                return;

            port_base = p;

            init();

            enable();
        }
};
