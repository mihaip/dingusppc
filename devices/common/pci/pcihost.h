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

#ifndef PCI_HOST_H
#define PCI_HOST_H

#include <devices/deviceregistry.h>

#include <cinttypes>
#include <string>
#include <unordered_map>
#include <vector>

enum {
    PCI_CONFIG_DIRECTION    = 1,
    PCI_CONFIG_READ         = 0,
    PCI_CONFIG_WRITE        = 1,

    PCI_CONFIG_TYPE         = 4,
    PCI_CONFIG_TYPE_0       = 0,
    PCI_CONFIG_TYPE_1       = 4,
};

/** PCI config space access details */
typedef struct AccessDetails {
    uint8_t size;
    uint8_t offset;
    uint8_t flags;
} AccessDetails;

class PCIDevice;    // forward declaration to prevent errors

class PCIHost {
public:
    PCIHost() {
        this->dev_map.clear();
        io_space_devs.clear();
    };
    ~PCIHost() = default;

    virtual bool pci_register_device(int dev_num, PCIDevice* dev_instance);

    virtual bool pci_register_mmio_region(uint32_t start_addr, uint32_t size, PCIDevice* obj);
    virtual bool pci_unregister_mmio_region(uint32_t start_addr, uint32_t size, PCIDevice* obj);

    virtual void attach_pci_device(std::string& dev_name, int slot_id);

    virtual PCIDevice *pci_find_device(uint8_t bus_num, uint8_t dev_num, uint8_t fun_num);

protected:
    std::unordered_map<int, PCIDevice*> dev_map;
    std::vector<PCIDevice*>             io_space_devs;
};

#endif /* PCI_HOST_H */
