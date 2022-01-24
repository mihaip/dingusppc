/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-22 divingkatae and maximum
                      (theweirdo)     spatium

(Contact divingkatae#1017 or powermax#2286 on Discord for more info)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/** @file NCR53C94/Am53CF94 SCSI controller definitions. */

/* NOTE: Power Macintosh computers don't have a real NCR 53C94 chip.
   The corresponding functionality is provided by the 53CF94 compatible
   cell in the custom Curio IC (Am79C950).
*/

#ifndef SC_53C94_H
#define SC_53C94_H

#include <cinttypes>

/** 53C94 read registers */
namespace Read {
    enum Reg53C94 : uint8_t {
        Xfer_Cnt_LSB = 0,
        Xfer_Cnt_MSB = 1,
        FIFO         = 2,
        Command      = 3,
        Status       = 4,
        Int_Status   = 5,
        Seq_Step     = 6,
        FIFO_Flags   = 7,
        Config_1     = 8,
        Config_2     = 0xB,
        Config_3     = 0xC,
        Config_4     = 0xD, // Am53CF94 extension
        Xfer_Cnt_Hi  = 0xE, // Am53CF94 extension
    };
};

/** 53C94 write registers */
namespace Write {
    enum Reg53C94 : uint8_t {
        Xfer_Cnt_LSB = 0,
        Xfer_Cnt_MSB = 1,
        FIFO         = 2,
        Command      = 3,
        Dest_Bus_ID  = 4,
        Sel_Timeout  = 5,
        Synch_Period = 6,
        Synch_Offset = 7,
        Config_1     = 8,
        Clock_Factor = 9,
        Test_Mode    = 0xA,
        Config_2     = 0xB,
        Config_3     = 0xC,
        Config_4     = 0xD, // Am53CF94 extension
        Xfer_Cnt_Hi  = 0xE, // Am53CF94 extension
        Data_Align   = 0xF
    };
};

/** NCR53C94/Am53CF94 commands. */
enum {
    CMD_NOP          = 0,
    CMD_CLEAR_FIFO   = 1,
    CMD_RESET_DEVICE = 2,
    CMD_RESET_BUS    = 3,
    CMD_DMA_STOP     = 4,
};

enum {
    CFG2_ENF    = 0x40, // Am53CF94: enable features (ENF) bit
};

class Sc53C94 {
public:
    Sc53C94(uint8_t chip_id=12);
    ~Sc53C94() = default;

    // 53C94 registers access
    uint8_t read(uint8_t reg_offset);
    void   write(uint8_t reg_offset, uint8_t value);

protected:
    void reset_device();
    void update_command_reg(uint8_t cmd);
    void exec_command();
    void exec_next_command();

private:
    uint8_t     chip_id;
    uint8_t     cmd_fifo[2];
    uint8_t     data_fifo[16];
    int         cmd_fifo_pos;
    int         data_fifo_pos;
    bool        on_reset = false;
    uint32_t    xfer_count;
    uint32_t    set_xfer_count;
    uint8_t     status;
    uint8_t     int_status;
    uint8_t     sel_timeout;
    uint8_t     clk_factor;
    uint8_t     config1;
    uint8_t     config2;
    uint8_t     config3;
};

#endif // SC_53C94_H