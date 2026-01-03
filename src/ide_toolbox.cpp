/**
 * ZuluIDE™ - Copyright (c) 2024 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version.
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * ZuluIDE Toolbox - Host-side control interface via vendor ATAPI commands
 *
 * This module implements ATAPI vendor commands that allow the host computer
 * to list, select, and manage disc images on the ZuluIDE device.
 */

#include "ide_toolbox.h"
#include "ide_atapi.h"
#include "atapi_constants.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE_config.h"
#include <zuluide/images/image_iterator.h>
#include <minIni.h>
#include <string.h>

// Global toolbox enabled flag (read from config)
static bool g_toolbox_enabled = false;
static bool g_toolbox_initialized = false;

/**
 * Initialize toolbox - read config settings
 */
static void toolbox_init(void)
{
    if (!g_toolbox_initialized)
    {
        g_toolbox_enabled = ini_getbool("IDE", "EnableToolbox", 0, CONFIGFILE);
        g_toolbox_initialized = true;
        
        if (g_toolbox_enabled)
        {
            logmsg("Toolbox: Enabled via configuration");
        }
    }
}

/**
 * Check if Toolbox is enabled
 */
bool toolbox_is_enabled(void)
{
    toolbox_init();
    return g_toolbox_enabled;
}

/**
 * Check if opcode is a Toolbox command
 */
bool toolbox_is_command(uint8_t opcode)
{
    return (opcode >= ATAPI_CMD_TOOLBOX_COUNT_IMAGES && 
            opcode <= ATAPI_CMD_TOOLBOX_GET_INFO);
}

/**
 * IDEATAPIDevice method implementations for Toolbox commands
 * These are added to the IDEATAPIDevice class
 */

// Forward declarations of helper functions
static uint16_t get_image_count(void);
static bool get_image_at_index(uint16_t index, char *filename, size_t maxlen);
static int16_t get_current_image_index(void);
static bool get_current_image_name(char *filename, size_t maxlen);
static bool select_image_by_index(uint16_t index);
static bool select_image_by_name(const char *filename);

/**
 * Handle TOOLBOX_COUNT_IMAGES command (0xD0)
 * Returns the number of available images
 */
bool IDEATAPIDevice::atapi_toolbox_count_images(const uint8_t *cmd)
{
    if (!toolbox_is_enabled())
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
    }

    uint8_t response[TOOLBOX_COUNT_RESPONSE_SIZE];
    uint16_t count = get_image_count();
    int16_t current_idx = get_current_image_index();
    
    response[0] = count & 0xFF;
    response[1] = (count >> 8) & 0xFF;
    response[2] = (current_idx >= 0) ? (uint8_t)current_idx : 0xFF;
    response[3] = 0x01;  // Flags: bit 0 = toolbox enabled
    
    dbgmsg("Toolbox: COUNT_IMAGES returning ", count, " images, current=", (int)current_idx);
    
    return atapi_send_data(response, TOOLBOX_COUNT_RESPONSE_SIZE) && atapi_cmd_ok();
}

/**
 * Handle TOOLBOX_LIST_IMAGES command (0xD1)
 * Returns a list of image filenames
 */
bool IDEATAPIDevice::atapi_toolbox_list_images(const uint8_t *cmd)
{
    if (!toolbox_is_enabled())
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
    }

    uint16_t start_idx = cmd[2] | ((uint16_t)cmd[3] << 8);
    uint16_t alloc_len = cmd[7] << 8 | cmd[8];
    
    if (alloc_len > sizeof(m_buffer.bytes))
    {
        alloc_len = sizeof(m_buffer.bytes);
    }
    
    memset(m_buffer.bytes, 0, alloc_len);
    
    zuluide::images::ImageIterator iterator;
    iterator.Reset();
    
    uint16_t current_idx = 0;
    uint16_t bytes_written = 0;
    char filename[TOOLBOX_MAX_FILENAME_LEN + 1];
    
    // Skip to start index
    while (current_idx < start_idx && iterator.MoveNext())
    {
        current_idx++;
    }
    
    // Fill buffer with filenames
    while (iterator.MoveNext() && bytes_written < alloc_len - 1)
    {
        const auto& image = iterator.Get();
        const std::string& name = image.GetFilename();
        size_t name_len = name.length();
        
        if (bytes_written + name_len + 1 > alloc_len)
        {
            break;  // No more room
        }
        
        memcpy(&m_buffer.bytes[bytes_written], name.c_str(), name_len);
        bytes_written += name_len;
        m_buffer.bytes[bytes_written++] = '\0';  // Null terminator
    }
    
    dbgmsg("Toolbox: LIST_IMAGES from ", start_idx, ", returning ", bytes_written, " bytes");
    
    return atapi_send_data(m_buffer.bytes, bytes_written) && atapi_cmd_ok();
}

/**
 * Handle TOOLBOX_GET_CURRENT command (0xD2)
 * Returns the current image filename
 */
bool IDEATAPIDevice::atapi_toolbox_get_current(const uint8_t *cmd)
{
    if (!toolbox_is_enabled())
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
    }

    uint8_t alloc_len = cmd[8];
    if (alloc_len == 0) alloc_len = 255;
    
    char filename[TOOLBOX_MAX_FILENAME_LEN + 1];
    memset(filename, 0, sizeof(filename));
    
    if (!get_current_image_name(filename, sizeof(filename) - 1))
    {
        filename[0] = '\0';
    }
    
    size_t len = strlen(filename) + 1;  // Include null terminator
    if (len > alloc_len) len = alloc_len;
    
    dbgmsg("Toolbox: GET_CURRENT returning '", filename, "'");
    
    return atapi_send_data((uint8_t *)filename, len) && atapi_cmd_ok();
}

/**
 * Handle TOOLBOX_SELECT_IMAGE command (0xD3)
 * Selects an image by index or name
 */
bool IDEATAPIDevice::atapi_toolbox_select_image(const uint8_t *cmd)
{
    if (!toolbox_is_enabled())
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
    }

    uint8_t flags = cmd[1];
    bool success = false;
    
    if (flags & TOOLBOX_SELECT_BY_NAME)
    {
        // Select by name - read filename from data phase
        uint16_t data_len = (cmd[7] << 8) | cmd[8];
        if (data_len > TOOLBOX_MAX_FILENAME_LEN)
        {
            data_len = TOOLBOX_MAX_FILENAME_LEN;
        }
        
        char filename[TOOLBOX_MAX_FILENAME_LEN + 1];
        memset(filename, 0, sizeof(filename));
        
        if (atapi_recv_data((uint8_t *)filename, data_len))
        {
            filename[data_len] = '\0';
            dbgmsg("Toolbox: SELECT_IMAGE by name '", filename, "'");
            success = select_image_by_name(filename);
        }
    }
    else
    {
        // Select by index
        uint16_t index = cmd[2] | ((uint16_t)cmd[3] << 8);
        dbgmsg("Toolbox: SELECT_IMAGE by index ", index);
        success = select_image_by_index(index);
    }
    
    if (success)
    {
        return atapi_cmd_ok();
    }
    else
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_FIELD);
    }
}

/**
 * Handle TOOLBOX_NEXT_IMAGE command (0xD4)
 * Loads the next image in alphabetical order
 */
bool IDEATAPIDevice::atapi_toolbox_next_image(const uint8_t *cmd)
{
    if (!toolbox_is_enabled())
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
    }

    dbgmsg("Toolbox: NEXT_IMAGE");
    
    // Trigger eject which will load next image
    if (m_removable.ejected)
    {
        // Already ejected, insert next
        insert_next_media(m_image);
    }
    else
    {
        // Eject first, then firmware auto-loads next
        eject_media();
    }
    
    return atapi_cmd_ok();
}

/**
 * Handle TOOLBOX_PREV_IMAGE command (0xD5)
 * Loads the previous image in alphabetical order
 */
bool IDEATAPIDevice::atapi_toolbox_prev_image(const uint8_t *cmd)
{
    if (!toolbox_is_enabled())
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
    }

    dbgmsg("Toolbox: PREV_IMAGE");
    
    // Get current index and select previous
    int16_t current = get_current_image_index();
    if (current > 0)
    {
        select_image_by_index(current - 1);
    }
    else
    {
        // Wrap to last image
        uint16_t count = get_image_count();
        if (count > 0)
        {
            select_image_by_index(count - 1);
        }
    }
    
    return atapi_cmd_ok();
}

/**
 * Handle TOOLBOX_GET_INFO command (0xD6)
 * Returns device and toolbox information
 */
bool IDEATAPIDevice::atapi_toolbox_get_info(const uint8_t *cmd)
{
    if (!toolbox_is_enabled())
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
    }

    uint16_t alloc_len = (cmd[7] << 8) | cmd[8];
    if (alloc_len > TOOLBOX_INFO_RESPONSE_SIZE)
    {
        alloc_len = TOOLBOX_INFO_RESPONSE_SIZE;
    }
    
    uint8_t response[TOOLBOX_INFO_RESPONSE_SIZE];
    memset(response, 0, sizeof(response));
    
    // Magic signature
    memcpy(&response[0], TOOLBOX_MAGIC, 4);
    
    // Protocol version
    response[4] = TOOLBOX_PROTOCOL_VERSION;
    
    // Device type
    response[5] = m_devinfo.devtype;
    
    // Firmware version (placeholder - should come from build system)
    response[6] = 1;  // Major
    response[7] = 0;  // Minor
    
    // Build string
    const char *build = "ZuluIDE-Toolbox";
    strncpy((char *)&response[8], build, 16);
    
    dbgmsg("Toolbox: GET_INFO");
    
    return atapi_send_data(response, alloc_len) && atapi_cmd_ok();
}

// ============================================================================
// Helper function implementations
// ============================================================================

/**
 * Get the count of available images
 */
static uint16_t get_image_count(void)
{
    zuluide::images::ImageIterator iterator;
    iterator.Reset();
    
    uint16_t count = 0;
    while (iterator.MoveNext())
    {
        count++;
    }
    
    return count;
}

/**
 * Get image filename at specified index
 */
static bool get_image_at_index(uint16_t index, char *filename, size_t maxlen)
{
    zuluide::images::ImageIterator iterator;
    iterator.Reset();
    
    uint16_t current = 0;
    while (iterator.MoveNext())
    {
        if (current == index)
        {
            const auto& image = iterator.Get();
            strncpy(filename, image.GetFilename().c_str(), maxlen);
            filename[maxlen - 1] = '\0';
            return true;
        }
        current++;
    }
    
    return false;
}

/**
 * Get current image index (-1 if none)
 */
static int16_t get_current_image_index(void)
{
    // Get current image name from the global image file
    extern IDEImageFile g_ide_imagefile;
    
    char current_name[256];
    if (!g_ide_imagefile.get_image_name(current_name, sizeof(current_name)))
    {
        return -1;
    }
    
    zuluide::images::ImageIterator iterator;
    iterator.Reset();
    
    int16_t index = 0;
    while (iterator.MoveNext())
    {
        const auto& image = iterator.Get();
        if (strcmp(image.GetFilename().c_str(), current_name) == 0)
        {
            return index;
        }
        index++;
    }
    
    return -1;
}

/**
 * Get current image filename
 */
static bool get_current_image_name(char *filename, size_t maxlen)
{
    extern IDEImageFile g_ide_imagefile;
    return g_ide_imagefile.get_image_name(filename, maxlen);
}

/**
 * Select image by index
 */
static bool select_image_by_index(uint16_t index)
{
    char filename[256];
    
    if (!get_image_at_index(index, filename, sizeof(filename)))
    {
        return false;
    }
    
    return select_image_by_name(filename);
}

/**
 * Select image by name
 */
static bool select_image_by_name(const char *filename)
{
    extern IDEImageFile g_ide_imagefile;
    extern void load_image(const zuluide::images::Image& img, bool reset_status);
    
    // Find image in iterator
    zuluide::images::ImageIterator iterator;
    iterator.Reset();
    
    while (iterator.MoveNext())
    {
        const auto& image = iterator.Get();
        if (strcmp(image.GetFilename().c_str(), filename) == 0)
        {
            load_image(image, false);
            return true;
        }
    }
    
    // Try partial match (just the base filename)
    iterator.Reset();
    while (iterator.MoveNext())
    {
        const auto& image = iterator.Get();
        const std::string& fullname = image.GetFilename();
        
        // Check if filename matches the end of the full path
        if (fullname.length() >= strlen(filename))
        {
            size_t offset = fullname.length() - strlen(filename);
            if (strcasecmp(fullname.c_str() + offset, filename) == 0)
            {
                load_image(image, false);
                return true;
            }
        }
    }
    
    logmsg("Toolbox: Image not found: ", filename);
    return false;
}
