/**
 * ZuluIDE™ - Copyright (c) 2024 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version.
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * ZuluIDE Toolbox - Host-side control interface via vendor ATAPI commands
 *
 * This module provides ATAPI vendor commands (0xD0-0xD6) that allow the host
 * computer to:
 *   - List available disc images on the SD card
 *   - Get the current image name
 *   - Select a specific image by index or name
 *   - Navigate next/previous in the image list
 *
 * These commands mirror ZuluSCSI's Toolbox functionality for IDE/ATAPI devices.
 */

#ifndef IDE_TOOLBOX_H
#define IDE_TOOLBOX_H

#include <stdint.h>
#include <stddef.h>
#include "atapi_constants.h"

// Toolbox vendor command opcodes (0xD0-0xD6 range)
// These are defined in atapi_constants.h as part of the ATAPI_COMMAND_LIST X-macro:
//   ATAPI_CMD_TOOLBOX_COUNT_IMAGES   = 0xD0
//   ATAPI_CMD_TOOLBOX_LIST_IMAGES    = 0xD1
//   ATAPI_CMD_TOOLBOX_GET_CURRENT    = 0xD2
//   ATAPI_CMD_TOOLBOX_SELECT_IMAGE   = 0xD3
//   ATAPI_CMD_TOOLBOX_NEXT_IMAGE     = 0xD4
//   ATAPI_CMD_TOOLBOX_PREV_IMAGE     = 0xD5
//   ATAPI_CMD_TOOLBOX_GET_INFO       = 0xD6

// Toolbox response structure for image count (ATAPI_CMD_TOOLBOX_COUNT_IMAGES)
// Returns 4 bytes:
//   Byte 0-1: Image count (little-endian uint16_t)
//   Byte 2:   Current image index (0-based, 0xFF if none)
//   Byte 3:   Flags (bit 0 = toolbox enabled)
#define TOOLBOX_COUNT_RESPONSE_SIZE     4

// Toolbox response structure for image list (ATAPI_CMD_TOOLBOX_LIST_IMAGES)
// CDB format:
//   Byte 2-3: Starting index (little-endian uint16_t)
//   Byte 7-8: Allocation length (little-endian uint16_t)
// Response format:
//   Null-terminated filename strings, back-to-back

// Toolbox response for current image (ATAPI_CMD_TOOLBOX_GET_CURRENT)
// CDB format:
//   Byte 8: Allocation length
// Response:
//   Null-terminated filename string

// Toolbox select image (ATAPI_CMD_TOOLBOX_SELECT_IMAGE)
// CDB format:
//   Byte 1: Flags (bit 0 = select by name, otherwise by index)
//   Byte 2-3: Image index (little-endian uint16_t) if by index
//   Byte 7-8: Data length if by name
// If by name, followed by DATA OUT with null-terminated filename

// Toolbox response for device info (ATAPI_CMD_TOOLBOX_GET_INFO)
// Response structure (64 bytes):
//   Byte 0-3:  Magic "ZUTB" (ZuluIDE Toolbox)
//   Byte 4:    Toolbox protocol version (currently 1)
//   Byte 5:    Device type (0=CD-ROM, 1=Zip, 2=Removable)
//   Byte 6-7:  Firmware version (major.minor)
//   Byte 8-23: Firmware build string
//   Byte 24-63: Reserved (zeros)
#define TOOLBOX_INFO_RESPONSE_SIZE      64
#define TOOLBOX_MAGIC                   "ZUTB"
#define TOOLBOX_PROTOCOL_VERSION        1

// Maximum filename length in responses
#define TOOLBOX_MAX_FILENAME_LEN        255

// Toolbox select flags
#define TOOLBOX_SELECT_BY_NAME          0x01

// Function prototypes - implemented in ide_toolbox.cpp

/**
 * Check if a command is a Toolbox vendor command
 * @param opcode ATAPI command opcode
 * @return true if this is a Toolbox command
 */
bool toolbox_is_command(uint8_t opcode);

/**
 * Check if Toolbox is enabled in configuration
 * @return true if Toolbox is enabled
 */
bool toolbox_is_enabled(void);

#endif // IDE_TOOLBOX_H
