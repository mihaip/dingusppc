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
#include <devices/common/scsi/scsicommoncmds.h>

#include <cinttypes>
#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

class BlueScsi : public ScsiPhysDevice, public ScsiCommonCmds {
public:
    BlueScsi(std::string name, int my_id, std::string dir_path, std::string send_dir_path);
    ~BlueScsi() = default;

    void process_command() override;

protected:
    void    read(uint32_t lba, uint16_t nblocks, uint8_t cmd_len);
    void    mode_select_6(uint8_t param_len);
    int     vendor_cmd_group_len(int group) override;
    void    get_medium_type(uint8_t& medium_type, uint8_t& dev_flags) override;
    int     format_block_descriptors(uint8_t* out_ptr) override;
    int     get_vendor_page(uint8_t ctrl, uint8_t subpage, uint8_t *out_ptr, int avail_len);

private:
    bool    check_lun();
    void    complete_data_in(int xfer_len);
    void    report_error(uint8_t sense_key, uint8_t asc, uint8_t ascq = 0, bool is_cdb = true);

    void    handle_toolbox_count_files();
    void    handle_toolbox_list_files();
    void    handle_toolbox_get_file();
    void    handle_toolbox_send_file_prep();
    void    handle_toolbox_send_file_10();
    void    handle_toolbox_send_file_end();
    bool    refresh_toolbox_file_cache();
    bool    ensure_toolbox_file_cache();

    struct ToolboxFileMetadata {
        std::string name;
        std::string full_path;
        uint64_t    size = 0;
        bool        is_directory = false;
    };

    uint8_t     data_buf[4096] = {};
    std::string dir_path;
    std::string send_dir_path;
    std::vector<ToolboxFileMetadata> toolbox_file_cache;
    bool        toolbox_cache_valid = false;

    // File sending state
    std::ofstream send_file_stream;
    std::string   send_file_name;
    bool          send_file_open = false;

    char vendor_info[8] = {'Q', 'U', 'A', 'N', 'T', 'U', 'M', ' '}; // BlueSCSI Toolbox 1.0.2 expects a trailing space
    char prod_info[16]  = {'E', 'm', 'u', 'l', 'a', 't', 'e', 'd', ' ', 'D', 'i', 's', 'k', ' ', ' ', ' '};
    char rev_info[4]    = {'d', 'i', '0', '1'};
};

#endif // BLUE_SCSI_H
