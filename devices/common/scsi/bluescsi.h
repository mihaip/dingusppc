/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-25 divingkatae and maximum
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

/** @file BlueSCSI definitions. */

#ifndef BLUE_SCSI_H
#define BLUE_SCSI_H

#include <devices/common/scsi/scsi.h>

#include <cinttypes>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class BlueScsi : public ScsiDevice {
public:
    BlueScsi(std::string name, int my_id, std::string dir_path);
    ~BlueScsi() = default;

    virtual void process_command() override;
    virtual bool prepare_data() override;
    virtual bool get_more_data() override;

protected:
    int     test_unit_ready();
    void    read(uint32_t lba, uint16_t nblocks, uint8_t cmd_len);
    uint32_t inquiry(uint8_t *cmd_ptr, uint8_t *data_ptr);
    void    mode_select_6(uint8_t param_len);

    void    mode_sense_6();
    void    read_capacity_10();

    virtual int vendor_cmd_group_len(int group) override;

private:
    void    handle_toolbox_count_files();
    void    handle_toolbox_list_files();
    void    handle_toolbox_get_file();
    bool    refresh_toolbox_file_cache();
    bool    ensure_toolbox_file_cache();

    struct ToolboxFileMetadata {
        std::string name;
        std::string full_path;
        uint64_t size = 0;
        bool        is_directory = false;
    };

    int         bytes_out = 0;
    uint8_t     data_buf[4096] = {};
    std::string dir_path;
    std::vector<ToolboxFileMetadata> toolbox_file_cache;
    bool        toolbox_cache_valid = false;

    // Inquiry info
    char vendor_info[8] = {'Q', 'U', 'A', 'N', 'T', 'U', 'M', ' '}; // BlueSCSI Toolbox 1.0.2 expects a trailing space
    char prod_info[16]  = {'E', 'm', 'u', 'l', 'a', 't', 'e', 'd', ' ', 'D', 'i', 's', 'k', '\0'};
    char rev_info[4]    = {'d', 'i', '0', '1'};
};

#endif // BLUE_SCSI_H
