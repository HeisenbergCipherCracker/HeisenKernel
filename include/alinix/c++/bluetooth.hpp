/**
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
#ifndef __ALINIX_KERNEL_C_PLUS_PLUS_BLUETOOTH_HPP
#define __ALINIX_KERNEL_C_PLUS_PLUS_BLUETOOTH_HPP


#include <alinix/types.h>
#include <alinix/list.h>
#include <alinix/gpio/desc.h>
#include <alinix/clk.h>

#define BCM_NULL_PKT 0x00
#define BCM_NULL_SIZE 0

#define BCM_LM_DIAG_PKT 0x07
#define BCM_LM_DIAG_SIZE 63

#define BCM_TYPE49_PKT 0x31
#define BCM_TYPE49_SIZE 0

#define BCM_TYPE52_PKT 0x34
#define BCM_TYPE52_SIZE 0

#define BCM_AUTOSUSPEND_DELAY	5000 /* default autosleep delay */

#define BCM_NUM_SUPPLIES 2





class Bluetooth{
    public:
        struct list_head    list;
        uint8_t status;
        uint16_t devices;
        uint16_t services;
        uint16_t characteristics;
        uint8_t* uuid;
        struct device *dev;
        const char* name;
        struct gpio_desc *device_wake_up;
        struct gpio_desc	*shutdown;
        int			(*set_device_wakeup)(struct bcm_device *, bool);
        int			(*set_shutdown)(struct bcm_device *, bool);
        bool res_enabled;
        struct clk		*txco_clk;
        struct clk		*lpo_clk;
        bool			res_enabled;

        u32			init_speed;
        u32			oper_speed;
        int			irq;
        bool			irq_active_low;


        Bluetooth();
        ~Bluetooth();
        void Setup();
        bool GetDeviceList();
        bool GetServiceList();
        bool GetCharacteristics();

};

#ifdef __cplusplus
extern "C" {
#endif
            struct list_head    list;
        uint8_t status;
        uint16_t devices;
        uint16_t services;
        uint16_t characteristics;
        uint8_t* uuid;
        struct device *dev;
        const char* name;
        struct gpio_desc *device_wake_up;
        struct gpio_desc	*shutdown;
        int			(*set_device_wakeup)(struct bcm_device *, bool);
        int			(*set_shutdown)(struct bcm_device *, bool);
        bool res_enabled;
        struct clk		*txco_clk;
        struct clk		*lpo_clk;
        bool			res_enabled;

        u32			init_speed;
        u32			oper_speed;
        int			irq;
        bool			irq_active_low;

        void Setup();
        bool GetDeviceList();
        bool GetServiceList();
        bool GetCharacteristics();
#ifdef __cplusplus
}
#endif




#endif // __ALINIX_KERNEL_C_PLUS_PLUS_BLUETOOTH_HPP