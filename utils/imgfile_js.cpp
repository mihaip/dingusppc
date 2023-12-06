/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-23 divingkatae and maximum
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

#include <utils/imgfile.h>
#include <loguru.hpp>

#include <emscripten.h>

class ImgFile::Impl {
public:
    int disk_id = -1;
};

ImgFile::ImgFile(): impl(std::make_unique<Impl>())
{

}

ImgFile::~ImgFile() = default;

bool ImgFile::open(const std::string &img_path)
{
    int disk_id = EM_ASM_INT({
        return workerApi.disks.open(UTF8ToString($0));
    }, img_path.c_str());
    if (disk_id == -1) {
        return false;
    }

    impl->disk_id = disk_id;
    return true;
}

void ImgFile::close()
{
    EM_ASM_({ workerApi.disks.close($0); }, impl->disk_id);
}

size_t ImgFile::size() const
{
    return EM_ASM_INT({ return workerApi.disks.size($0); }, impl->disk_id);
}

size_t ImgFile::read(void* buf, off_t offset, size_t length) const
{
    if (impl->disk_id == -1) {
        LOG_F(WARNING, "ImgFile::read before disk was opened, ignoring.");
        return 0;
    }
    int read_size = EM_ASM_INT({
        return workerApi.disks.read($0, $1, $2, $3);
    }, impl->disk_id, buf, int(offset), int(length));
    return read_size;
}

size_t ImgFile::write(const void* buf, off_t offset, size_t length)
{
    if (impl->disk_id == -1) {
        LOG_F(WARNING, "ImgFile::write before disk was opened, ignoring.");
        return 0;
    }
    int write_size = EM_ASM_INT({
        return workerApi.disks.write($0, $1, $2, $3);
    }, impl->disk_id, buf, int(offset), int(length));
    return write_size;
}
