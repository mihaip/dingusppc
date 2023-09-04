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

#include <devices/sound/soundserver.h>
#include <loguru.hpp>

class SoundServer::Impl {
public:
};


SoundServer::SoundServer(): impl(std::make_unique<Impl>())
{
}

SoundServer::~SoundServer()
{
    this->shutdown();
}

int SoundServer::start()
{
    LOG_F(INFO, "SoundServer::start()");
    return 0;
}

void SoundServer::shutdown()
{
    LOG_F(INFO, "SoundServer::shutdown()");
}

int SoundServer::open_out_stream(uint32_t sample_rate, void *user_data)
{
    LOG_F(INFO, "SoundServer::open_out_stream()");
    return 0;
}

int SoundServer::start_out_stream()
{
    LOG_F(INFO, "SoundServer::start_out_stream()");
    return 0;
}

void SoundServer::close_out_stream()
{
    LOG_F(INFO, "SoundServer::close_out_stream()");
}
