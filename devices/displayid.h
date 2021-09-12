/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-20 divingkatae and maximum
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

/** @file DisplayID class definitions.

    DisplayID is a special purpose class for handling display
    identification (aka Monitor Plug-n-Play) as required by
    video cards.

    DisplayID provides two methods for display identification:
    - Apple monitor sense codes as described in the Technical Note HW30
    - Display Data Channel (DDC) standardized by VESA
 */

#ifndef DISPLAY_ID_H
#define DISPLAY_ID_H

#include "SDL.h"
#include <cinttypes>

/** I2C bus states. */
enum I2CState : uint8_t {
    STOP     = 0, /* transaction started           */
    START    = 1, /* transaction ended (idle)      */
    DEV_ADDR = 2, /* receiving device address      */
    REG_ADDR = 3, /* receiving register address    */
    DATA     = 4, /* sending/receiving data        */
    ACK      = 5, /* sending/receiving acknowledge */
    NACK     = 6  /* no acknowledge (error)        */
};


class DisplayID {
public:
    DisplayID();
    ~DisplayID();

    uint16_t read_monitor_sense(uint16_t data, uint16_t dirs);
    void get_disp_texture(void **pix_buf, int *pitch);
    void update_screen(void);

protected:
    uint16_t set_result(uint8_t sda, uint8_t scl);
    uint16_t update_ddc_i2c(uint8_t sda, uint8_t scl);

private:
    SDL_Window      *display_wnd;
    SDL_Renderer    *renderer;
    SDL_Texture     *disp_texture;

    bool i2c_on;

    uint8_t std_sense_code;
    uint8_t ext_sense_code;

    /* DDC I2C variables. */
    uint8_t next_state;
    uint8_t prev_state;
    uint8_t last_sda;
    uint8_t last_scl;
    uint16_t data_out;
    int bit_count;     /* number of bits processed so far */
    uint8_t byte;      /* byte value being currently transferred */
    uint8_t dev_addr;  /* current device address */
    uint8_t reg_addr;  /* current register address */
    uint8_t* data_ptr; /* ptr to data byte to be transferred next */

    uint8_t edid[128] = {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x06, 0x10, 0x02, 0x9d, 0x01, 0x01, 0x01,
        0x01, 0x08, 0x09, 0x01, 0x01, 0x68, 0x20, 0x18, 0x28, 0xe8, 0x04, 0x89, 0xa0, 0x57, 0x4a,
        0x9b, 0x26, 0x12, 0x48, 0x4c, 0x31, 0x2b, 0x80, 0x31, 0x59, 0x45, 0x59, 0x61, 0x59, 0xa9,
        0x40, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x60, 0x16, 0x40, 0x40, 0x31, 0x70,
        0x2b, 0x20, 0x20, 0x40, 0x23, 0x00, 0x38, 0xea, 0x10, 0x00, 0x00, 0x18, 0x48, 0x3f, 0x40,
        0x32, 0x62, 0xb0, 0x32, 0x40, 0x40, 0xc2, 0x13, 0x00, 0x38, 0xea, 0x10, 0x00, 0x00, 0x18,
        0x00, 0x00, 0x00, 0xfd, 0x00, 0x30, 0xa0, 0x1e, 0x55, 0x10, 0x00, 0x0a, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x53, 0x74, 0x75, 0x64, 0x69, 0x6f, 0x44,
        0x73, 0x70, 0x6c, 0x79, 0x31, 0x37, 0x00, 0x19};

    /*  More EDID:
        00ff ffff ffff ff00 5a63 5151 0341 0000
        240a 0102 1f28 1eb3 e850 69a7 5148 9b24
        0e48 4cff ff80 3159 4559 6159 714f 8140
        8199 a940 a94f 0000 00ff 0053 5a30 3336
        3136 3634 330a 2020 0000 00fd 0032 b41e
        61ff 000a 2020 2020 2020 0000 00fc 0056
        6965 7753 6f6e 6963 2047 3831 0000 00fc
        0030 2d34 4d0a 2020 2020 2020 2020 00f7
      */
};

#endif /* DISPLAY_ID_H */
