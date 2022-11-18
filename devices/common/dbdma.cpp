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

/** @file Descriptor-based direct memory access emulation. */

#include <cpu/ppc/ppcmmu.h>
#include <devices/common/dbdma.h>
#include <devices/common/dmacore.h>
#include <endianswap.h>
#include <memaccess.h>

#include <cinttypes>
#include <cstring>
#include <loguru.hpp>

void DMAChannel::set_callbacks(DbdmaCallback start_cb, DbdmaCallback stop_cb)
{
    this->start_cb = start_cb;
    this->stop_cb  = stop_cb;
}

void DMAChannel::fetch_cmd(uint32_t cmd_addr, DMACmd* p_cmd) {
    /* load DMACmd from physical memory */
    memcpy((uint8_t*)p_cmd, mmu_get_dma_mem(cmd_addr, 16, nullptr), 16);
}

uint8_t DMAChannel::interpret_cmd() {
    DMACmd cmd_struct;
    bool   is_writable;
    int    cmd;

    if (this->cmd_in_progress) {
        // return current command if there is data to transfer
        if (this->queue_len)
            return cmd;

        // obtain real pointer to the descriptor of the completed command
        uint8_t *curr_cmd = mmu_get_dma_mem(this->cmd_ptr - 16, 16, &is_writable);

        // get command code
        cmd = curr_cmd[3] >> 4;

        // all commands except STOP update cmd.xferStatus
        if (cmd < 7 && is_writable) {
            WRITE_WORD_LE_A(&curr_cmd[14], this->ch_stat | CH_STAT_ACTIVE);
        }

        // all INPUT and OUTPUT commands update cmd.resCount
        if (cmd < 4 && is_writable) {
            WRITE_WORD_LE_A(&curr_cmd[12], this->queue_len & 0xFFFFUL);
        }

        this->cmd_in_progress = false;
    }

    fetch_cmd(this->cmd_ptr, &cmd_struct);

    cmd = cmd_struct.cmd_key >> 4;

    this->ch_stat &= ~CH_STAT_WAKE; /* clear wake bit (DMA spec, 5.5.3.4) */

    switch (cmd_struct.cmd_key >> 4) {
    case 0: // OUTPUT_MORE
    case 1: // OUTPUT_LAST
    case 2: // INPUT_MORE
    case 3: // INPUT_LAST
        if (cmd_struct.cmd_key & 7) {
            LOG_F(ERROR, "Key > 0 not implemented");
            break;
        }
        if (cmd_struct.cmd_bits & 0x3F) {
            LOG_F(ERROR, "non-zero i/b/w not implemented");
            break;
        }
        this->queue_data = mmu_get_dma_mem(cmd_struct.address, cmd_struct.req_count, &is_writable);
        this->queue_len  = cmd_struct.req_count;
        this->cmd_ptr += 16;
        this->cmd_in_progress = true;
        break;
    case 4:
        LOG_F(ERROR, "Unsupported DMA Command STORE_QUAD");
        break;
    case 5:
        LOG_F(ERROR, "Unsupported DMA Command LOAD_QUAD");
        break;
    case 6:
        LOG_F(ERROR, "Unsupported DMA Command NOP");
        break;
    case 7:
        this->ch_stat &= ~CH_STAT_ACTIVE;
        this->cmd_in_progress = false;
        break;
    default:
        LOG_F(ERROR, "Unsupported DMA command 0x%X", cmd_struct.cmd_key >> 4);
        this->ch_stat |= CH_STAT_DEAD;
        this->ch_stat &= ~CH_STAT_ACTIVE;
    }

    return (cmd_struct.cmd_key >> 4);
}


uint32_t DMAChannel::reg_read(uint32_t offset, int size) {
    uint32_t res = 0;

    if (size != 4) {
        LOG_F(WARNING, "Unsupported non-DWORD read from DMA channel");
        return 0;
    }

    switch (offset) {
    case DMAReg::CH_CTRL:
        res = 0; /* ChannelControl reads as 0 (DBDMA spec 5.5.1, table 74) */
        break;
    case DMAReg::CH_STAT:
        res = BYTESWAP_32(this->ch_stat);
        break;
    default:
        LOG_F(WARNING, "Unsupported DMA channel register 0x%X", offset);
    }

    return res;
}

void DMAChannel::reg_write(uint32_t offset, uint32_t value, int size) {
    uint16_t mask, old_stat, new_stat;

    if (size != 4) {
        LOG_F(WARNING, "Unsupported non-DWORD write to DMA channel");
        return;
    }

    value    = BYTESWAP_32(value);
    old_stat = this->ch_stat;

    switch (offset) {
    case DMAReg::CH_CTRL:
        mask     = value >> 16;
        new_stat = (value & mask & 0xF0FFU) | (old_stat & ~mask);
        LOG_F(9, "New ChannelStatus value = 0x%X", new_stat);

        if ((new_stat & CH_STAT_RUN) != (old_stat & CH_STAT_RUN)) {
            if (new_stat & CH_STAT_RUN) {
                new_stat |= CH_STAT_ACTIVE;
                this->ch_stat = new_stat;
                this->start();
            } else {
                new_stat &= ~CH_STAT_ACTIVE;
                new_stat &= ~CH_STAT_DEAD;
                this->ch_stat = new_stat;
                this->abort();
            }
        } else if ((new_stat & CH_STAT_WAKE) != (old_stat & CH_STAT_WAKE)) {
            new_stat |= CH_STAT_ACTIVE;
            this->ch_stat = new_stat;
            this->resume();
        } else if ((new_stat & CH_STAT_PAUSE) != (old_stat & CH_STAT_PAUSE)) {
            if (new_stat & CH_STAT_PAUSE) {
                new_stat &= ~CH_STAT_ACTIVE;
                this->ch_stat = new_stat;
                this->pause();
            }
        }
        if (new_stat & CH_STAT_FLUSH) {
            LOG_F(WARNING, "DMA flush not implemented!");
            new_stat &= ~CH_STAT_FLUSH;
            this->ch_stat = new_stat;
        }
        break;
    case DMAReg::CH_STAT:
        break; /* ingore writes to ChannelStatus */
    case DMAReg::CMD_PTR_LO:
        if (!(this->ch_stat & CH_STAT_RUN) && !(this->ch_stat & CH_STAT_ACTIVE)) {
            this->cmd_ptr = value;
            LOG_F(9, "CommandPtrLo set to 0x%X", this->cmd_ptr);
        }
        break;
    default:
        LOG_F(WARNING, "Unsupported DMA channel register 0x%X", offset);
    }
}

DmaPullResult DMAChannel::pull_data(uint32_t req_len, uint32_t *avail_len, uint8_t **p_data)
{
    *avail_len = 0;

    if (this->ch_stat & CH_STAT_DEAD || !(this->ch_stat & CH_STAT_ACTIVE)) {
        /* dead or idle channel? -> no more data */
        LOG_F(WARNING, "Dead/idle channel -> no more data");
        return DmaPullResult::NoMoreData;
    }

    /* interpret DBDMA program until we get data or become idle  */
    while ((this->ch_stat & CH_STAT_ACTIVE) && !this->queue_len) {
        this->interpret_cmd();
    }

    /* dequeue data if any */
    if (this->queue_len) {
        if (this->queue_len >= req_len) {
            LOG_F(9, "Return req_len = %d data", req_len);
            *p_data    = this->queue_data;
            *avail_len = req_len;
            this->queue_len -= req_len;
            this->queue_data += req_len;
        } else { /* return less data than req_len */
            LOG_F(9, "Return queue_len = %d data", this->queue_len);
            *p_data         = this->queue_data;
            *avail_len      = this->queue_len;
            this->queue_len = 0;
        }
        return DmaPullResult::MoreData; /* tell the caller there is more data */
    }

    return DmaPullResult::NoMoreData; /* tell the caller there is no more data */
}

int DMAChannel::push_data(const char* src_ptr, int len)
{
    if (this->ch_stat & CH_STAT_DEAD || !(this->ch_stat & CH_STAT_ACTIVE)) {
        LOG_F(WARNING, "DBDMA: attempt to push data to dead/idle channel");
        return -1;
    }

    // interpret DBDMA program until we get buffer to fill in or become idle
    while ((this->ch_stat & CH_STAT_ACTIVE) && !this->queue_len) {
        this->interpret_cmd();
    }

    if (this->queue_len) {
        len = std::min((int)this->queue_len, len);
        std::memcpy(this->queue_data, src_ptr, len);
        this->queue_data += len;
        this->queue_len  -= len;
    }

    // proceed with the DBDMA program if the buffer became exhausted
    if (!this->queue_len) {
        this->interpret_cmd();
    }

    return 0;
}

bool DMAChannel::is_active()
{
    if (this->ch_stat & CH_STAT_DEAD || !(this->ch_stat & CH_STAT_ACTIVE)) {
        return false;
    }
    else {
        return true;
    }
}

void DMAChannel::start()
{
    if (this->ch_stat & CH_STAT_PAUSE) {
        LOG_F(WARNING, "Cannot start DMA channel, PAUSE bit is set");
        return;
    }

    this->queue_len = 0;

    if (this->start_cb)
        this->start_cb();
}

void DMAChannel::resume() {
    if (this->ch_stat & CH_STAT_PAUSE) {
        LOG_F(WARNING, "Cannot resume DMA channel, PAUSE bit is set");
        return;
    }

    LOG_F(INFO, "Resuming DMA channel");
}

void DMAChannel::abort() {
    LOG_F(9, "Aborting DMA channel");
}

void DMAChannel::pause() {
    LOG_F(INFO, "Pausing DMA channel");
    if (this->stop_cb)
        this->stop_cb();
}
