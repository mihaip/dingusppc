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

/** @file BlueSCSI emulation. */

#include <devices/common/scsi/scsi.h>
#include <devices/common/scsi/bluescsi.h>
#include <devices/deviceregistry.h>
#include <loguru.hpp>
#include <machines/machineproperties.h>
#include <memaccess.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
enum ToolboxCommand : uint8_t {
    TOOLBOX_LIST_FILES  = 0xD0,
    TOOLBOX_GET_FILE    = 0xD1,
    TOOLBOX_COUNT_FILES = 0xD2,
};
constexpr std::size_t kToolboxEntrySize = 40;
constexpr std::size_t kToolboxMaxEntries = 100;
constexpr std::size_t kToolboxBlockSize = 4096;
}

BlueScsi::BlueScsi(std::string name, int my_id, std::string dir_path) :
    ScsiDevice(name, my_id), dir_path(dir_path)
{

}

int BlueScsi::vendor_cmd_group_len(int group)
{
    switch (group) {
    case 6:
    case 7:
        // BlueSCSI Toolbox commands use 10-byte CDBs.
        return 10;
    default:
        return 1;
    }
}

void BlueScsi::process_command()
{
    uint32_t lba;

    this->pre_xfer_action  = nullptr;
    this->post_xfer_action = nullptr;
    this->status = ScsiStatus::GOOD;
    this->data_ptr = this->data_buf;

    uint8_t* cmd = this->cmd_buf;
    LOG_F(INFO, "%s: process_command 0x%X", this->name.c_str(), cmd[0]);

    switch (cmd[0]) {
    case ScsiCommand::TEST_UNIT_READY:
        this->test_unit_ready();
        break;
    case ScsiCommand::READ_6:
        lba = ((cmd[1] & 0x1F) << 16) + (cmd[2] << 8) + cmd[3];
        this->read(lba, cmd[4], 6);
        break;
    case ScsiCommand::INQUIRY:
        this->bytes_out = this->inquiry(cmd, this->data_buf);
        this->switch_phase(ScsiPhase::DATA_IN);
        break;
    case ScsiCommand::MODE_SELECT_6:
        this->mode_select_6(cmd[4]);
        break;
    case ScsiCommand::MODE_SENSE_6:
        this->mode_sense_6();
        break;
    case ScsiCommand::PREVENT_ALLOW_MEDIUM_REMOVAL:
        this->switch_phase(ScsiPhase::STATUS);
        break;
    case ScsiCommand::READ_CAPACITY_10:
        this->read_capacity_10();
        break;
    case ScsiCommand::READ_10:
        lba = READ_DWORD_BE_U(&cmd[2]);
        if (cmd[1] & 1) {
            ABORT_F("%s: RelAdr bit set in READ_10", this->name.c_str());
        }
        read(lba, READ_WORD_BE_U(&cmd[7]), 10);
        break;

    // BlueSCSI specific commands
    case TOOLBOX_COUNT_FILES:
        this->handle_toolbox_count_files();
        break;
    case TOOLBOX_LIST_FILES:
        this->handle_toolbox_list_files();
        break;
    case TOOLBOX_GET_FILE:
        this->handle_toolbox_get_file();
        break;
    default:
        LOG_F(ERROR, "%s: unsupported command 0x%X", this->name.c_str(), cmd[0]);
        this->illegal_command(cmd);
    }
}

bool BlueScsi::prepare_data()
{
    switch (this->cur_phase) {
    case ScsiPhase::DATA_IN:
        this->data_size = this->bytes_out;
        break;
    case ScsiPhase::DATA_OUT:
        this->data_size = 0;
        break;
    case ScsiPhase::STATUS:
        break;
    default:
        LOG_F(WARNING, "%s: unexpected phase in prepare_data", this->name.c_str());
        return false;
    }
    return true;
}

bool BlueScsi::get_more_data() {
    return false;
}

void BlueScsi::read(uint32_t lba, uint16_t nblocks, uint8_t cmd_len)
{
    if (!check_lun())
        return;

    if (cmd_len == 6 && nblocks == 0)
        nblocks = 256;

//    NOCOMMIT: BlockStorageDevice dependency
//    this->set_fpos(lba);
//    this->data_ptr   = (uint8_t *)this->data_cache.get();
//    LOG_F(INFO, "%s: set data_ptr to data_cache %p", this->name.c_str(), this->data_ptr);
//    this->bytes_out  = this->read_begin(nblocks, UINT32_MAX);

    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
    this->switch_phase(ScsiPhase::DATA_IN);
}


uint32_t BlueScsi::inquiry(uint8_t *cmd_ptr, uint8_t *data_ptr) {
    int page_num  = cmd_ptr[2];
    int alloc_len = cmd_ptr[4];

    if (page_num) {
        ABORT_F("%s: invalid page number in INQUIRY", this->name.c_str());
    }

    if (alloc_len > 36) {
        LOG_F(WARNING, "%s: more than 36 bytes requested in INQUIRY", this->name.c_str());
    }

    int lun;
    if (this->last_selection_has_atention) {
        LOG_F(INFO, "%s: INQUIRY (%d bytes) with ATN LUN = %02x & 7",
            this->name.c_str(), alloc_len, this->last_selection_message);
        lun = this->last_selection_message & 7;
    }
    else {
        LOG_F(INFO, "%s: INQUIRY (%d bytes) with NO ATN LUN = %02x >> 5", this->name.c_str(),
            alloc_len, cmd_ptr[1]);
        lun = cmd_ptr[1] >> 5;
    }

    data_ptr[0] = (lun == this->lun) ? 0 : 0x7f; // device type: Direct-access block device (hard drive)
    data_ptr[1] =    0; // non-removable media; 0x80 = removable media
    data_ptr[2] =    2; // ANSI version: SCSI-2
    data_ptr[3] =    1; // response data format
    data_ptr[4] = 0x1F; // additional length
    data_ptr[5] =    0;
    data_ptr[6] =    0;
    data_ptr[7] = 0x18; // supports synchronous xfers and linked commands
    std::memcpy(&data_ptr[8], vendor_info, 8);
    std::memcpy(&data_ptr[16], prod_info, 16);
    std::memcpy(&data_ptr[32], rev_info, 4);
    if (alloc_len < 36) {
        LOG_F(ERROR, "%s: allocation length too small: %d", this->name.c_str(),
              alloc_len);
    }
    else {
        memset(&data_ptr[36], 0, alloc_len - 36);
    }

    return alloc_len;
}


void BlueScsi::handle_toolbox_count_files()
{
    // Always rescan so repeated COUNT reflects filesystem changes.
    if (!this->refresh_toolbox_file_cache()) {
        LOG_F(WARNING, "%s: unable to build toolbox cache for COUNT_FILES", this->name.c_str());
        this->data_buf[0] = 0;
    } else {
        const uint8_t entry_count =
            static_cast<uint8_t>(std::min<std::size_t>(this->toolbox_file_cache.size(), kToolboxMaxEntries));
        this->data_buf[0] = entry_count;
        LOG_F(INFO, "%s: counted %d entries", this->name.c_str(), entry_count);
    }

    this->bytes_out = 1;
    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
    this->switch_phase(ScsiPhase::DATA_IN);
}

void BlueScsi::handle_toolbox_list_files()
{
    LOG_F(INFO, "%s: BLUESCSI_TOOLBOX_LIST_FILES", this->name.c_str());
    if (!this->ensure_toolbox_file_cache()) {
        LOG_F(WARNING, "%s: unable to build toolbox cache for LIST_FILES", this->name.c_str());
        this->bytes_out = 0;
        this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
        this->switch_phase(ScsiPhase::DATA_IN);
        return;
    }

    const std::size_t entry_count = std::min<std::size_t>(this->toolbox_file_cache.size(), kToolboxMaxEntries);
    const std::size_t total_bytes = entry_count * kToolboxEntrySize;

    std::memset(this->data_buf, 0, total_bytes);

    for (std::size_t i = 0; i < entry_count; ++i) {
        uint8_t *entry_buf = &this->data_buf[kToolboxEntrySize * i];
        entry_buf[0] = static_cast<uint8_t>(i);
        entry_buf[1] = this->toolbox_file_cache[i].is_directory ? 0 : 1;

        const auto &metadata = this->toolbox_file_cache[i];
        const std::size_t name_len = std::min<std::size_t>(metadata.name.size(), 32);
        std::memcpy(&entry_buf[2], metadata.name.data(), name_len);

        const uint64_t size = metadata.size;
        entry_buf[35] = (size >> 32) & 0xff;
        entry_buf[36] = (size >> 24) & 0xff;
        entry_buf[37] = (size >> 16) & 0xff;
        entry_buf[38] = (size >> 8) & 0xff;
        entry_buf[39] = size & 0xff;
    }

    this->bytes_out = static_cast<int>(total_bytes);

    LOG_F(INFO, "%s: listed %zu entries for a total of %zu bytes", this->name.c_str(), entry_count, total_bytes);
    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
    this->switch_phase(ScsiPhase::DATA_IN);
}

void BlueScsi::handle_toolbox_get_file()
{
    LOG_F(INFO, "%s: BLUESCSI_TOOLBOX_GET_FILE", this->name.c_str());
    const uint8_t requested_index = this->cmd_buf[1];
    const uint32_t block_offset = READ_DWORD_BE_U(&this->cmd_buf[2]);
    LOG_F(INFO, "%s: index=%d offset=%d", this->name.c_str(), requested_index, block_offset);

    if (!this->ensure_toolbox_file_cache()) {
        LOG_F(WARNING, "%s: no cached toolbox entries available", this->name.c_str());
        this->report_error(ScsiSense::NOT_READY, ScsiError::DEV_NOT_READY);
        return;
    }

    if (requested_index >= this->toolbox_file_cache.size()) {
        LOG_F(WARNING, "%s: invalid file index %u (available=%zu)",
              this->name.c_str(), requested_index, this->toolbox_file_cache.size());
        this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
        return;
    }

    const auto &metadata = this->toolbox_file_cache[requested_index];

    if (metadata.is_directory) {
        LOG_F(WARNING, "%s: GET_FILE requested on directory index %u (%s)",
              this->name.c_str(), requested_index, metadata.name.c_str());
        this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
        return;
    }

    if (metadata.size == 0 && block_offset == 0) {
        this->bytes_out = 0;
        this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
        this->switch_phase(ScsiPhase::DATA_IN);
        return;
    }

    const uint64_t byte_offset = static_cast<uint64_t>(block_offset) * kToolboxBlockSize;
    if (byte_offset >= metadata.size) {
        LOG_F(WARNING, "%s: block offset %u beyond end of file \"%s\" (size=%" PRIu64 ")",
              this->name.c_str(), block_offset, metadata.name.c_str(), metadata.size);
        this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
        return;
    }

    const uint64_t remaining = metadata.size - byte_offset;
    const std::size_t bytes_to_read =
        static_cast<std::size_t>(std::min<uint64_t>(remaining, kToolboxBlockSize));

    std::ifstream file(metadata.full_path, std::ios::binary);
    if (!file) {
        LOG_F(WARNING, "%s: failed to open \"%s\" for reading",
              this->name.c_str(), metadata.full_path.c_str());
        this->report_error(ScsiSense::NOT_READY, ScsiError::DEV_NOT_READY);
        return;
    }

    file.seekg(static_cast<std::streamoff>(byte_offset), std::ios::beg);
    if (!file) {
        LOG_F(WARNING, "%s: seek failed in \"%s\" offset=%" PRIu64,
              this->name.c_str(), metadata.full_path.c_str(), byte_offset);
        this->report_error(ScsiSense::MEDIUM_ERR, ScsiError::NO_SECTOR);
        return;
    }

    std::size_t actual_read = 0;
    if (bytes_to_read > 0) {
        file.read(reinterpret_cast<char*>(this->data_buf), static_cast<std::streamsize>(bytes_to_read));
        if (file.bad()) {
            LOG_F(WARNING, "%s: read error in \"%s\"",
                  this->name.c_str(), metadata.full_path.c_str());
            this->report_error(ScsiSense::MEDIUM_ERR, ScsiError::NO_SECTOR);
            return;
        }
        actual_read = static_cast<std::size_t>(file.gcount());
    }

    this->bytes_out = static_cast<int>(actual_read);
    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
    this->switch_phase(ScsiPhase::DATA_IN);
}

bool BlueScsi::refresh_toolbox_file_cache()
{
    namespace fs = std::filesystem;
    this->toolbox_file_cache.clear();
    this->toolbox_cache_valid = false;

    if (this->dir_path.empty()) {
        LOG_F(WARNING, "%s: toolbox directory path is empty", this->name.c_str());
        return false;
    }

    std::error_code ec;
    const fs::path root_path(this->dir_path);

    if (!fs::exists(root_path, ec) || !fs::is_directory(root_path, ec)) {
        LOG_F(WARNING, "%s: toolbox directory \"%s\" missing or invalid",
              this->name.c_str(), this->dir_path.c_str());
        return false;
    }

    fs::directory_iterator iter(root_path, ec);
    if (ec) {
        LOG_F(WARNING, "%s: failed to enumerate \"%s\": %s",
              this->name.c_str(), this->dir_path.c_str(), ec.message().c_str());
        return false;
    }
    const fs::directory_iterator end_iter;

    const std::size_t max_files = (kToolboxMaxEntries > 0) ? (kToolboxMaxEntries - 1) : 0;
    std::vector<ToolboxFileMetadata> entries;
    entries.reserve(max_files ? max_files : kToolboxMaxEntries);

    for (; iter != end_iter && entries.size() < max_files; iter.increment(ec)) {
        if (ec) {
            LOG_F(WARNING, "%s: error iterating \"%s\": %s",
                  this->name.c_str(), this->dir_path.c_str(), ec.message().c_str());
            break;
        }

        const fs::directory_entry &entry = *iter;
        std::error_code status_ec;
        if (!entry.is_regular_file(status_ec)) {
            if (status_ec) {
                LOG_F(WARNING, "%s: unable to stat \"%s\": %s",
                      this->name.c_str(), entry.path().string().c_str(), status_ec.message().c_str());
            }
            continue;
        }

        ToolboxFileMetadata metadata;
        metadata.name = entry.path().filename().string();
        metadata.full_path = entry.path().string();
        metadata.size = entry.file_size(status_ec);
        if (status_ec) {
            LOG_F(WARNING, "%s: failed to get size for \"%s\": %s",
                  this->name.c_str(), metadata.full_path.c_str(), status_ec.message().c_str());
            continue;
        }

        entries.emplace_back(std::move(metadata));
    }

    std::sort(entries.begin(), entries.end(),
              [](const ToolboxFileMetadata &lhs, const ToolboxFileMetadata &rhs) {
                  return lhs.name < rhs.name;
              });

    std::string share_name = root_path.filename().string();
    if (share_name.empty()) {
        share_name = root_path.string();
    }
    if (share_name.empty()) {
        share_name = "shared";
    }

    ToolboxFileMetadata directory_entry;
    directory_entry.name = share_name;
    directory_entry.full_path = this->dir_path;
    directory_entry.size = 0;
    directory_entry.is_directory = true;

    std::vector<ToolboxFileMetadata> cache_entries;
    cache_entries.reserve(std::min<std::size_t>(entries.size() + 1, kToolboxMaxEntries));
    cache_entries.emplace_back(std::move(directory_entry));
    for (auto &entry : entries) {
        if (cache_entries.size() >= kToolboxMaxEntries) {
            break;
        }
        cache_entries.emplace_back(std::move(entry));
    }

    this->toolbox_file_cache = std::move(cache_entries);
    this->toolbox_cache_valid = true;
    return true;
}

bool BlueScsi::ensure_toolbox_file_cache()
{
    if (this->toolbox_cache_valid) {
        return true;
    }
    return this->refresh_toolbox_file_cache();
}

int BlueScsi::test_unit_ready()
{
    LOG_F(WARNING, "%s: test_unit_ready called", this->name.c_str());
    this->switch_phase(ScsiPhase::STATUS);
    return ScsiError::NO_ERROR;
}

// Based on the value of BlueSCSIVendorPage from
// https://github.com/BlueSCSI/BlueSCSI-v2/blob/main/lib/SCSI2SD/src/firmware/mode.c
static const uint8_t BlueScsi_Vendor_Page[] =
{
    0x31, // Page code
    42,   // Page length
    'B','l','u','e','S','C','S','I',' ','i','s',' ','t','h','e',' ','B','E','S','T',' ',
    'S','T','O','L','E','N',' ','F','R','O','M',' ','B','L','U','E','S','C','S','I',0x00
};
static size_t BlueScsi_Vendor_Page_Length = sizeof(BlueScsi_Vendor_Page);

void BlueScsi::mode_sense_6()
{
    uint8_t page_code = this->cmd_buf[2] & 0x3F;
    uint8_t alloc_len = this->cmd_buf[4];

    std::memset(&this->data_buf[1], 0, alloc_len);

    this->data_buf[0] =     3; // initial data size
    this->data_buf[1] =     0; // medium type
    this->data_buf[2] =  0x80; // write protected

    switch (page_code) {
    case 0x31: // magic BlueSCSI page; official clients require this to recognize a toolbox-capable device
        this->data_buf[0] += BlueScsi_Vendor_Page_Length;
        // scuzEMU first asks just just for the header, and then does a second
        // sense command for the actual value.
        if (alloc_len >= BlueScsi_Vendor_Page_Length) {
            LOG_F(INFO, "%s: sending vendor page", this->name.c_str());
            std::memcpy(&this->data_buf[3], BlueScsi_Vendor_Page, BlueScsi_Vendor_Page_Length);
        } else {
            LOG_F(INFO, "%s: skipping vendor page, alloc_len=%d", this->name.c_str(), alloc_len);
        }
        break;
    default:
        LOG_F(WARNING, "%s: unsupported page 0x%02x in MODE_SENSE_6", this->name.c_str(), page_code);
        this->status = ScsiStatus::CHECK_CONDITION;
        this->sense  = ScsiSense::ILLEGAL_REQ;
        this->asc    = ScsiError::INVALID_CDB;
        this->ascq   = 0;
        this->sksv   = 0xc0; // sksv=1, C/D=Command, BPV=0, BP=0
        this->field  = 2;
        this->switch_phase(ScsiPhase::STATUS);
        return;
    }

    this->bytes_out = std::min((int)alloc_len, (int)this->data_buf[0] + 1);
    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
    this->switch_phase(ScsiPhase::DATA_IN);
}

void BlueScsi::mode_select_6(uint8_t param_len)
{
    LOG_F(WARNING, "%s: mode_select_6 called", this->name.c_str());
    if (!param_len)
        return;

    this->incoming_size = param_len;

    std::memset(&this->data_buf[0], 0, 512);

    this->post_xfer_action = [this]() {
        // TODO: parse the received mode parameter list here
        LOG_F(INFO, "Mode Select: received mode parameter list");
    };

    this->switch_phase(ScsiPhase::DATA_OUT);
}

void BlueScsi::read_capacity_10()
{
    LOG_F(WARNING, "%s: read_capacity_10 called", this->name.c_str());
    uint32_t lba = READ_DWORD_BE_U(&this->cmd_buf[2]);

    if (this->cmd_buf[1] & 1) {
        ABORT_F("%s: RelAdr bit set in READ_CAPACITY_10", this->name.c_str());
    }

    if (!(this->cmd_buf[8] & 1) && lba) {
        LOG_F(ERROR, "%s: non-zero LBA for PMI=0", this->name.c_str());
        this->status = ScsiStatus::CHECK_CONDITION;
        this->sense  = ScsiSense::ILLEGAL_REQ;
        this->asc    = ScsiError::INVALID_CDB;
        this->ascq   = 0;
        this->sksv   = 0xc0; // sksv=1, C/D=Command, BPV=0, BP=0
        this->field  = 8;
        this->switch_phase(ScsiPhase::STATUS);
        return;
    }

    if (!check_lun())
        return;

//    int last_lba = (int)this->size_blocks - 1;
//
//    WRITE_DWORD_BE_A(&this->data_buf[0], last_lba);
//    WRITE_DWORD_BE_A(&this->data_buf[4], this->block_size);

    this->bytes_out  = 8;
    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;

    this->switch_phase(ScsiPhase::DATA_IN);
}
