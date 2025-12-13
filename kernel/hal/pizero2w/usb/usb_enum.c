/*
 * USB Enumeration
 * Device discovery, descriptor parsing, hub support
 */

#include "usb_enum.h"
#include "usb_transfer.h"
#include "dwc2_core.h"
#include "../../../printf.h"
#include "../../../string.h"

// ============================================================================
// Standard Device Requests
// ============================================================================

int usb_get_device_descriptor(int addr, usb_device_descriptor_t *desc) {
    usb_setup_packet_t setup = {
        .bmRequestType = 0x80,  // Device to host, standard, device
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DESC_DEVICE << 8) | 0,
        .wIndex = 0,
        .wLength = sizeof(usb_device_descriptor_t)
    };

    return usb_control_transfer(addr, &setup, desc, sizeof(usb_device_descriptor_t), 1);
}

int usb_set_address(int addr) {
    usb_setup_packet_t setup = {
        .bmRequestType = 0x00,  // Host to device, standard, device
        .bRequest = USB_REQ_SET_ADDRESS,
        .wValue = addr,
        .wIndex = 0,
        .wLength = 0
    };

    return usb_control_transfer(0, &setup, NULL, 0, 0);
}

int usb_set_configuration(int addr, int config) {
    usb_setup_packet_t setup = {
        .bmRequestType = 0x00,
        .bRequest = USB_REQ_SET_CONFIGURATION,
        .wValue = config,
        .wIndex = 0,
        .wLength = 0
    };

    return usb_control_transfer(addr, &setup, NULL, 0, 0);
}

int usb_get_configuration_descriptor(int addr, uint8_t *buf, int len) {
    usb_setup_packet_t setup = {
        .bmRequestType = 0x80,
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DESC_CONFIGURATION << 8) | 0,
        .wIndex = 0,
        .wLength = len
    };

    return usb_control_transfer(addr, &setup, buf, len, 1);
}

// ============================================================================
// Hub-Specific Requests
// ============================================================================

int usb_get_hub_descriptor(int addr, usb_hub_descriptor_t *desc) {
    usb_setup_packet_t setup = {
        .bmRequestType = 0xA0,  // Device to host, class, device
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DESC_HUB << 8) | 0,
        .wIndex = 0,
        .wLength = sizeof(usb_hub_descriptor_t)
    };

    return usb_control_transfer(addr, &setup, desc, sizeof(usb_hub_descriptor_t), 1);
}

int usb_get_port_status(int hub_addr, int port, uint32_t *status) {
    usb_setup_packet_t setup = {
        .bmRequestType = 0xA3,  // Device to host, class, other (port)
        .bRequest = USB_REQ_GET_PORT_STATUS,
        .wValue = 0,
        .wIndex = port,
        .wLength = 4
    };

    return usb_control_transfer(hub_addr, &setup, status, 4, 1);
}

int usb_set_port_feature(int hub_addr, int port, int feature) {
    usb_setup_packet_t setup = {
        .bmRequestType = 0x23,  // Host to device, class, other (port)
        .bRequest = USB_REQ_SET_PORT_FEATURE,
        .wValue = feature,
        .wIndex = port,
        .wLength = 0
    };

    return usb_control_transfer(hub_addr, &setup, NULL, 0, 0);
}

int usb_clear_port_feature(int hub_addr, int port, int feature) {
    usb_setup_packet_t setup = {
        .bmRequestType = 0x23,  // Host to device, class, other (port)
        .bRequest = USB_REQ_CLEAR_PORT_FEATURE,
        .wValue = feature,
        .wIndex = port,
        .wLength = 0
    };

    return usb_control_transfer(hub_addr, &setup, NULL, 0, 0);
}

// ============================================================================
// Hub Enumeration
// ============================================================================

int usb_enumerate_hub(int hub_addr, int num_ports) {
    usb_info("[USB] Enumerating hub at addr %d with %d ports\n", hub_addr, num_ports);

    for (int port = 1; port <= num_ports; port++) {
        usb_debug("[USB] Hub port %d: powering on...\n", port);

        // Power on port
        if (usb_set_port_feature(hub_addr, port, USB_PORT_FEAT_POWER) < 0) {
            usb_debug("[USB] Failed to power on port %d\n", port);
            continue;
        }

        // Wait for power good (hub descriptor says how long in 2ms units)
        msleep(100);

        // Get port status
        uint32_t status = 0;
        if (usb_get_port_status(hub_addr, port, &status) < 0) {
            usb_debug("[USB] Failed to get port %d status\n", port);
            continue;
        }

        usb_debug("[USB] Port %d status: %08x\n", port, status);

        // Check if device connected
        if (!(status & USB_PORT_STAT_CONNECTION)) {
            usb_debug("[USB] Port %d: no device\n", port);
            continue;
        }

        usb_info("[USB] Port %d: device connected!\n", port);

        // Reset port
        if (usb_set_port_feature(hub_addr, port, USB_PORT_FEAT_RESET) < 0) {
            usb_debug("[USB] Failed to reset port %d\n", port);
            continue;
        }

        // Wait for reset to complete
        msleep(50);

        // Get port status again
        if (usb_get_port_status(hub_addr, port, &status) < 0) {
            usb_debug("[USB] Failed to get port %d status after reset\n", port);
            continue;
        }

        usb_debug("[USB] Port %d after reset: %08x\n", port, status);

        // Clear reset change
        usb_clear_port_feature(hub_addr, port, USB_PORT_FEAT_C_RESET);

        // Check if port is enabled
        if (!(status & USB_PORT_STAT_ENABLE)) {
            usb_debug("[USB] Port %d: not enabled after reset\n", port);
            continue;
        }

        // Determine device speed
        int speed = 1;  // Default Full Speed
        if (status & USB_PORT_STAT_LOW_SPEED) {
            speed = 2;  // Low Speed
        } else if (status & USB_PORT_STAT_HIGH_SPEED) {
            speed = 0;  // High Speed
        }

        const char *speed_names[] = {"High", "Full", "Low"};
        usb_debug("[USB] Port %d: %s speed device\n", port, speed_names[speed]);

        // Enumerate the device on this port
        msleep(10);  // Recovery time
        usb_enumerate_device_at(hub_addr, port, speed);
    }

    return 0;
}

// ============================================================================
// Device Enumeration
// ============================================================================

int usb_enumerate_device_at(int parent_addr, int port, int speed) {
    usb_debug("[USB] Enumerating device (parent=%d, port=%d, speed=%d)...\n",
              parent_addr, port, speed);

    if (usb_state.num_devices >= MAX_USB_DEVICES) {
        usb_debug("[USB] Too many devices!\n");
        return -1;
    }

    // Get device descriptor at address 0
    usb_device_descriptor_t desc;
    memset(&desc, 0, sizeof(desc));

    // Temporarily set speed for address 0 transfers
    int old_speed = usb_state.device_speed;
    usb_state.device_speed = speed;

    int ret = usb_get_device_descriptor(0, &desc);
    if (ret < 8) {
        usb_debug("[USB] Failed to get device descriptor (got %d bytes)\n", ret);
        usb_state.device_speed = old_speed;
        return -1;
    }

    usb_debug("[USB] Device descriptor: VID=%04x PID=%04x MaxPacket=%d\n",
              desc.idVendor, desc.idProduct, desc.bMaxPacketSize0);

    // Assign address
    int new_addr = ++usb_state.next_address;
    msleep(10);

    ret = usb_set_address(new_addr);
    if (ret < 0) {
        usb_debug("[USB] Failed to set address %d\n", new_addr);
        usb_state.device_speed = old_speed;
        return -1;
    }
    msleep(10);

    // Create device entry
    usb_device_t *dev = &usb_state.devices[usb_state.num_devices++];
    dev->address = new_addr;
    dev->speed = speed;
    dev->max_packet_size = desc.bMaxPacketSize0;
    dev->parent_hub = parent_addr;
    dev->parent_port = port;
    dev->is_hub = 0;
    dev->hub_ports = 0;

    // Get full device descriptor at new address
    ret = usb_get_device_descriptor(new_addr, &desc);
    if (ret < (int)sizeof(desc)) {
        usb_debug("[USB] Failed to get full device descriptor\n");
        return -1;
    }

    usb_debug("[USB] Device %d: USB%x.%x Class=%d VID=%04x PID=%04x\n",
              new_addr, desc.bcdUSB >> 8, (desc.bcdUSB >> 4) & 0xF,
              desc.bDeviceClass, desc.idVendor, desc.idProduct);

    // Get configuration descriptor
    uint8_t config_buf[256];
    ret = usb_get_configuration_descriptor(new_addr, config_buf, sizeof(config_buf));
    if (ret < 9) {
        usb_debug("[USB] Failed to get config descriptor\n");
        return -1;
    }

    usb_config_descriptor_t *config = (usb_config_descriptor_t *)config_buf;
    usb_debug("[USB] Config: %d interfaces, total length %d\n",
              config->bNumInterfaces, config->wTotalLength);

    // Check if this is a hub (device class or interface class)
    int is_hub = (desc.bDeviceClass == USB_CLASS_HUB);
    int found_keyboard = 0;
    int keyboard_ep = 0;
    int keyboard_mps = 8;
    int keyboard_interval = 10;
    int keyboard_interface = 0;

    // Parse interfaces
    int offset = config->bLength;
    while (offset < config->wTotalLength && offset < (int)sizeof(config_buf)) {
        uint8_t len = config_buf[offset];
        if (len == 0) break;

        uint8_t type = config_buf[offset + 1];

        if (type == USB_DESC_INTERFACE) {
            usb_interface_descriptor_t *iface = (usb_interface_descriptor_t *)&config_buf[offset];
            usb_debug("[USB] Interface %d: Class=%d SubClass=%d Protocol=%d\n",
                      iface->bInterfaceNumber, iface->bInterfaceClass,
                      iface->bInterfaceSubClass, iface->bInterfaceProtocol);

            if (iface->bInterfaceClass == USB_CLASS_HUB) {
                is_hub = 1;
            } else if (iface->bInterfaceClass == USB_CLASS_HID) {
                if (iface->bInterfaceProtocol == USB_HID_PROTOCOL_KEYBOARD) {
                    usb_info("[USB] Found HID boot keyboard!\n");
                    found_keyboard = 1;
                    keyboard_interface = iface->bInterfaceNumber;
                } else if (iface->bInterfaceProtocol == USB_HID_PROTOCOL_MOUSE) {
                    usb_debug("[USB] Found HID boot mouse\n");
                }
            }
        } else if (type == USB_DESC_ENDPOINT && found_keyboard && keyboard_ep == 0) {
            usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t *)&config_buf[offset];
            if ((ep->bmAttributes & 0x03) == 3 && (ep->bEndpointAddress & 0x80)) {
                // Interrupt IN endpoint
                keyboard_ep = ep->bEndpointAddress & 0x0F;
                keyboard_mps = ep->wMaxPacketSize;
                keyboard_interval = ep->bInterval;
                usb_debug("[USB] Keyboard interrupt EP: %d, MPS=%d, interval=%d\n",
                          keyboard_ep, keyboard_mps, keyboard_interval);
            }
        }

        offset += len;
    }

    // Set configuration
    ret = usb_set_configuration(new_addr, config->bConfigurationValue);
    if (ret < 0) {
        usb_debug("[USB] Failed to set configuration\n");
        return -1;
    }

    usb_debug("[USB] Device %d configured!\n", new_addr);

    // Handle hub
    if (is_hub) {
        dev->is_hub = 1;

        // Get hub descriptor
        usb_hub_descriptor_t hub_desc;
        ret = usb_get_hub_descriptor(new_addr, &hub_desc);
        if (ret >= 7) {
            dev->hub_ports = hub_desc.bNbrPorts;
            usb_info("[USB] Hub has %d ports\n", hub_desc.bNbrPorts);

            // Enumerate downstream devices
            usb_enumerate_hub(new_addr, hub_desc.bNbrPorts);
        } else {
            usb_debug("[USB] Failed to get hub descriptor\n");
        }
    }

    // Save keyboard info and configure HID protocol
    if (found_keyboard && keyboard_ep > 0) {
        usb_state.keyboard_addr = new_addr;
        usb_state.keyboard_ep = keyboard_ep;
        usb_state.keyboard_mps = keyboard_mps;
        usb_state.keyboard_interval = keyboard_interval;

        // SET_PROTOCOL: Switch to Boot Protocol (0) for simple 8-byte reports
        // This is CRITICAL - without it, keyboard stays in Report Protocol mode
        usb_setup_packet_t set_protocol = {
            .bmRequestType = 0x21,  // Host to device, Class, Interface
            .bRequest = USB_HID_SET_PROTOCOL,
            .wValue = USB_HID_PROTOCOL_BOOT,  // 0 = Boot Protocol
            .wIndex = keyboard_interface,
            .wLength = 0
        };
        ret = usb_control_transfer(new_addr, &set_protocol, NULL, 0, 0);
        if (ret < 0) {
            usb_info("[USB] SET_PROTOCOL failed (may be OK for boot keyboards)\n");
        } else {
            usb_info("[USB] SET_PROTOCOL to Boot Protocol OK\n");
        }

        // SET_IDLE: Set idle rate to 0 (only report on change)
        // This reduces USB traffic - keyboard only sends data when key state changes
        usb_setup_packet_t set_idle = {
            .bmRequestType = 0x21,  // Host to device, Class, Interface
            .bRequest = USB_HID_SET_IDLE,
            .wValue = 0,  // Idle rate = 0 (indefinite)
            .wIndex = keyboard_interface,
            .wLength = 0
        };
        ret = usb_control_transfer(new_addr, &set_idle, NULL, 0, 0);
        if (ret < 0) {
            usb_debug("[USB] SET_IDLE failed (OK, not all keyboards support it)\n");
        } else {
            usb_debug("[USB] SET_IDLE OK\n");
        }

        usb_info("[USB] Keyboard ready at addr %d EP %d\n", new_addr, keyboard_ep);
    }

    return 0;
}

// Main enumeration entry point (for root device)
int usb_enumerate_device(void) {
    usb_state.next_address = 0;
    usb_state.num_devices = 0;
    usb_state.keyboard_addr = 0;

    return usb_enumerate_device_at(0, 0, usb_state.device_speed);
}
