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
#include <core/memaccess.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
enum ToolboxCommand : uint8_t {
    TOOLBOX_LIST_FILES      = 0xD0,
    TOOLBOX_GET_FILE        = 0xD1,
    TOOLBOX_COUNT_FILES     = 0xD2,
    TOOLBOX_SEND_FILE_PREP  = 0xD3,
    TOOLBOX_SEND_FILE_10    = 0xD4,
    TOOLBOX_SEND_FILE_END   = 0xD5,
};
constexpr std::size_t kToolboxEntrySize = 40;
constexpr std::size_t kToolboxMaxEntries = 100;
constexpr std::size_t kToolboxBlockSize = 4096;
constexpr std::size_t kToolboxSendBlockSize = 512;
constexpr std::size_t kToolboxFilenameSize = 33;
}

BlueScsi::BlueScsi(std::string name, int my_id, std::string dir_path, std::string send_dir_path) :
    ScsiPhysDevice(name, my_id), ScsiCommonCmds(), dir_path(dir_path), send_dir_path(send_dir_path)
{
    this->set_phys_dev(this);
    this->set_cdb_ptr(this->cmd_buf);
    this->set_buf_ptr(this->data_buf);
    this->set_buffer(this->data_buf);
    this->set_read_more_data_cb(
        [](int* dsize, uint8_t** dptr) {
            return false;
        }
    );
    this->set_write_more_data_cb(
        [](int* dsize, uint8_t** dptr) {
            return false;
        }
    );

    this->dev_type      = ScsiDevType::DIRECT_ACCESS;
    this->is_removable  = false;
    this->std_versions  = 2;
    this->resp_fmt      = 1;
    this->cap_flags     = CAP_SYNC_XFER;
    this->set_vendor_id(this->vendor_info);
    this->set_product_id(this->prod_info);
    this->set_revision_id(this->rev_info);

    this->add_page_getter(this, 0x31, &BlueScsi::get_vendor_page);
}

int BlueScsi::vendor_cmd_group_len(int group)
{
    switch (group) {
    case 6:
    case 7:
        // BlueSCSI Toolbox commands use 10-byte CDBs.
        return 10;
    default:
        return ScsiPhysDevice::vendor_cmd_group_len(group);
    }
}

void BlueScsi::process_command()
{
    uint32_t lba;

    this->pre_xfer_action  = nullptr;
    this->post_xfer_action = nullptr;
    phy_impl->set_status(ScsiStatus::GOOD);
    phy_impl->set_buffer(this->data_buf);
    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;

    if (this->verify_cdb() < 0) {
        this->switch_phase(ScsiPhase::STATUS);
        return;
    }

    uint8_t* cmd = this->cmd_buf;
    LOG_F(INFO, "%s: process_command 0x%X", this->name.c_str(), cmd[0]);

    switch (cmd[0]) {
    case ScsiCommand::TEST_UNIT_READY:
    case ScsiCommand::INQUIRY:
    case ScsiCommand::REQ_SENSE:
    case ScsiCommand::MODE_SENSE_6:
    case ScsiCommand::MODE_SENSE_10:
        ScsiCommonCmds::process_command();
        break;
    case ScsiCommand::READ_6:
        lba = ((cmd[1] & 0x1F) << 16) + (cmd[2] << 8) + cmd[3];
        this->read(lba, cmd[4], 6);
        break;
    case ScsiCommand::MODE_SELECT_6:
        this->mode_select_6(cmd[4]);
        break;
    case ScsiCommand::PREVENT_ALLOW_MEDIUM_REMOVAL:
        this->switch_phase(ScsiPhase::STATUS);
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
    case TOOLBOX_SEND_FILE_PREP:
        this->handle_toolbox_send_file_prep();
        break;
    case TOOLBOX_SEND_FILE_10:
        this->handle_toolbox_send_file_10();
        break;
    case TOOLBOX_SEND_FILE_END:
        this->handle_toolbox_send_file_end();
        break;
    default:
        LOG_F(ERROR, "%s: unsupported command 0x%X", this->name.c_str(), cmd[0]);
        this->invalid_command();
        this->switch_phase(ScsiPhase::STATUS);
    }
}

bool BlueScsi::check_lun()
{
    if (this->get_lun() == 0) {
        return true;
    }

    this->invalid_lun();
    this->switch_phase(ScsiPhase::STATUS);
    return false;
}

void BlueScsi::complete_data_in(int xfer_len)
{
    phy_impl->set_buffer(this->data_buf);
    phy_impl->set_xfer_len(xfer_len);
    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
    this->switch_phase(ScsiPhase::DATA_IN);
}

void BlueScsi::report_error(uint8_t sense_key, uint8_t asc, uint8_t ascq, bool is_cdb)
{
    this->sense_key       = sense_key;
    this->asc             = asc;
    this->ascq            = ascq;
    this->is_cdb_err      = is_cdb;
    this->field_ptr_valid = false;
    this->bit_ptr_valid   = false;
    phy_impl->set_status(ScsiStatus::CHECK_CONDITION);
    this->switch_phase(ScsiPhase::STATUS);
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

    phy_impl->set_xfer_len(0);
    this->switch_phase(ScsiPhase::DATA_IN);
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

    this->complete_data_in(1);
}

void BlueScsi::handle_toolbox_list_files()
{
    LOG_F(INFO, "%s: BLUESCSI_TOOLBOX_LIST_FILES CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
          this->name.c_str(),
          this->cmd_buf[0], this->cmd_buf[1], this->cmd_buf[2], this->cmd_buf[3], this->cmd_buf[4],
          this->cmd_buf[5], this->cmd_buf[6], this->cmd_buf[7], this->cmd_buf[8], this->cmd_buf[9]);
    if (!this->ensure_toolbox_file_cache()) {
        LOG_F(WARNING, "%s: unable to build toolbox cache for LIST_FILES", this->name.c_str());
        this->complete_data_in(0);
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

        LOG_F(INFO, "%s: entry[%zu]: %s %s, size=%llu",
              this->name.c_str(), i,
              metadata.is_directory ? "DIR" : "FILE",
              metadata.name.c_str(), size);
    }

    LOG_F(INFO, "%s: listed %zu entries for a total of %zu bytes", this->name.c_str(), entry_count, total_bytes);
    this->complete_data_in(static_cast<int>(total_bytes));
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
        this->complete_data_in(0);
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

    this->complete_data_in(static_cast<int>(actual_read));
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

    std::vector<ToolboxFileMetadata> cache_entries;
    cache_entries.reserve(std::min<std::size_t>(entries.size(), kToolboxMaxEntries));
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

void BlueScsi::get_medium_type(uint8_t& medium_type, uint8_t& dev_flags)
{
    medium_type = 0;
    dev_flags   = 0x80;
}

int BlueScsi::format_block_descriptors(uint8_t* out_ptr)
{
    std::memset(out_ptr, 0, 8);
    return 8;
}

int BlueScsi::get_vendor_page(uint8_t ctrl, uint8_t subpage, uint8_t* out_ptr, int avail_len)
{
    if (subpage && subpage != 0xFFU)
        return FORMAT_ERR_BAD_SUBPAGE;

    if (ctrl == 3)
        return FORMAT_ERR_BAD_CONTROL;

    if ((int)BlueScsi_Vendor_Page_Length > avail_len)
        return FORMAT_ERR_DATA_TOO_BIG;

    std::memcpy(out_ptr, BlueScsi_Vendor_Page, BlueScsi_Vendor_Page_Length);
    return static_cast<int>(BlueScsi_Vendor_Page_Length);
}

void BlueScsi::mode_select_6(uint8_t param_len)
{
    if (!param_len) {
        this->switch_phase(ScsiPhase::STATUS);
        return;
    }

    phy_impl->set_xfer_len(param_len);
    std::memset(&this->data_buf[0], 0, sizeof(this->data_buf));

    this->post_xfer_action = [this]() {
        // TODO: parse the received mode parameter list here
        LOG_F(INFO, "Mode Select: received mode parameter list");
    };

    this->switch_phase(ScsiPhase::DATA_OUT);
}

void BlueScsi::handle_toolbox_send_file_prep()
{
    namespace fs = std::filesystem;

    LOG_F(INFO, "%s: BLUESCSI_TOOLBOX_SEND_FILE_PREP", this->name.c_str());

    // Close any previously open send file
    if (this->send_file_open) {
        LOG_F(WARNING, "%s: closing previously open send file", this->name.c_str());
        this->send_file_stream.close();
        this->send_file_open = false;
    }

    // Prepare to receive 33-byte filename
    phy_impl->set_xfer_len(kToolboxFilenameSize);
    std::memset(this->data_buf, 0, kToolboxFilenameSize);

    this->post_xfer_action = [this]() {
        namespace fs = std::filesystem;

        // Extract null-terminated filename (max 32 chars + null)
        char filename[kToolboxFilenameSize];
        std::memcpy(filename, this->data_buf, kToolboxFilenameSize);
        filename[kToolboxFilenameSize - 1] = '\0';  // Ensure null termination

        std::string name(filename);
        LOG_F(INFO, "%s: preparing to receive file: '%s'", this->name.c_str(), name.c_str());

        // Validate filename is not empty
        if (name.empty()) {
            LOG_F(WARNING, "%s: empty filename in SEND_FILE_PREP", this->name.c_str());
            this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
            return;
        }

        // Check if send directory is configured
        if (this->send_dir_path.empty()) {
            LOG_F(WARNING, "%s: send directory path is empty", this->name.c_str());
            this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
            return;
        }

        // Construct full path
        const fs::path send_dir(this->send_dir_path);
        const fs::path file_path = send_dir / name;

        // Ensure send directory exists
        std::error_code ec;
        if (!fs::exists(send_dir, ec)) {
            fs::create_directories(send_dir, ec);
            if (ec) {
                LOG_F(WARNING, "%s: failed to create send directory '%s': %s",
                      this->name.c_str(), this->send_dir_path.c_str(), ec.message().c_str());
                this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
                return;
            }
        }

        // Open file for binary writing (truncate if exists)
        this->send_file_stream.open(file_path.string(), std::ios::binary | std::ios::trunc);
        if (!this->send_file_stream) {
            LOG_F(WARNING, "%s: failed to create file '%s'", this->name.c_str(), file_path.string().c_str());
            this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
            return;
        }

        this->send_file_name = file_path.string();
        this->send_file_open = true;
        LOG_F(INFO, "%s: file '%s' opened for writing", this->name.c_str(), this->send_file_name.c_str());

        this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
        this->switch_phase(ScsiPhase::STATUS);
    };

    this->switch_phase(ScsiPhase::DATA_OUT);
}

void BlueScsi::handle_toolbox_send_file_10()
{
    LOG_F(INFO, "%s: BLUESCSI_TOOLBOX_SEND_FILE_10", this->name.c_str());

    // Check if a file is open
    if (!this->send_file_open || !this->send_file_stream) {
        LOG_F(WARNING, "%s: no file open for SEND_FILE_10", this->name.c_str());
        this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
        return;
    }

    // Extract byte count from CDB[1..2] (big endian)
    const uint16_t byte_count = READ_WORD_BE_U(&this->cmd_buf[1]);

    // Extract block number from CDB[3..5] (big endian, 24-bit)
    const uint32_t block_number = (static_cast<uint32_t>(this->cmd_buf[3]) << 16) |
                                   (static_cast<uint32_t>(this->cmd_buf[4]) << 8) |
                                   static_cast<uint32_t>(this->cmd_buf[5]);

    LOG_F(INFO, "%s: writing %u bytes at block %u", this->name.c_str(), byte_count, block_number);

    // Validate byte count
    if (byte_count == 0 || byte_count > kToolboxSendBlockSize) {
        LOG_F(WARNING, "%s: invalid byte count %u (must be 1-%zu)",
              this->name.c_str(), byte_count, kToolboxSendBlockSize);
        this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
        return;
    }

    // Prepare to receive data
    phy_impl->set_xfer_len(byte_count);
    std::memset(this->data_buf, 0, kToolboxSendBlockSize);

    this->post_xfer_action = [this, block_number, byte_count]() {
        // Calculate byte offset in file
        const uint64_t byte_offset = static_cast<uint64_t>(block_number) * kToolboxSendBlockSize;

        // Seek to the correct position
        this->send_file_stream.seekp(static_cast<std::streamoff>(byte_offset), std::ios::beg);
        if (!this->send_file_stream) {
            LOG_F(WARNING, "%s: seek failed to offset %" PRIu64 " in '%s'",
                  this->name.c_str(), byte_offset, this->send_file_name.c_str());
            this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
            return;
        }

        // Write the data
        this->send_file_stream.write(reinterpret_cast<const char*>(this->data_buf), byte_count);
        if (!this->send_file_stream) {
            LOG_F(WARNING, "%s: write failed for '%s'", this->name.c_str(), this->send_file_name.c_str());
            this->report_error(ScsiSense::ILLEGAL_REQ, ScsiError::INVALID_CDB);
            return;
        }

        // Flush to ensure data is written
        this->send_file_stream.flush();

        LOG_F(INFO, "%s: wrote %u bytes at offset %" PRIu64, this->name.c_str(), byte_count, byte_offset);

        this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
        this->switch_phase(ScsiPhase::STATUS);
    };

    this->switch_phase(ScsiPhase::DATA_OUT);
}

void BlueScsi::handle_toolbox_send_file_end()
{
    LOG_F(INFO, "%s: BLUESCSI_TOOLBOX_SEND_FILE_END", this->name.c_str());

    if (this->send_file_open) {
        this->send_file_stream.close();
        LOG_F(INFO, "%s: closed file '%s'", this->name.c_str(), this->send_file_name.c_str());
        this->send_file_open = false;
        this->send_file_name.clear();
    } else {
        LOG_F(WARNING, "%s: no file open for SEND_FILE_END", this->name.c_str());
    }

    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
    this->switch_phase(ScsiPhase::STATUS);
}
