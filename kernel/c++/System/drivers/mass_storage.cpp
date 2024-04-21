/**
 * @author Ali Mirmohammad
 * @file mass_storage.cpp
 * *************************************IMPORTANT ALINIX LICENSE TERM********************************************
 ** This file is part of AliNix.

**AliNix is free software: you can redistribute it and/or modify
**it under the terms of the GNU Affero General Public License as published by
**the Free Software Foundation, either version 3 of the License, or
**(at your option) any later version.

**AliNix is distributed in the hope that it will be useful,
**but WITHOUT ANY WARRANTY; without even the implied warranty of
**MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
**GNU Affero General Public License for more details.

**You should have received a copy of the GNU Affero General Public License
**along with AliNix. If not, see <https://www.gnu.org/licenses/>.
*/
#include <alinix/mass_storage.hpp>
#include <alinix/log.h>
#include <system/drivers/usb/usbdefs.hpp>
#include <alinix/enums.h>
#include <alinix/disk.hpp>
#include <alinix/types.h>


USBMassStorageDriver::USBMassStorageDriver(USBDevice* dev)
    : USBDriver(dev, "USB Mass Storage"), DiskUtil(dev->GetControllerIndex(), dev->GetController(), USBDisk, dev->GetSize(), dev->GetNumBlocks(), dev->GetBlockSize(), /* Pass a valid USBDriver pointer here */)
{ }
