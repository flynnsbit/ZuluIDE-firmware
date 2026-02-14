/*
 * ZuluIDE DOS Utility - IDE/ATAPI Low-Level Driver Implementation
 * Copyright (c) 2024
 *
 * Low-level IDE port I/O and ATAPI packet command interface
 * Compatible with Watcom C, Turbo C, and Borland C
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <conio.h>
#include "ide.h"

/* Port I/O macros - adjust for different compilers */
#ifdef __WATCOMC__
    #include <conio.h>
    #define outportb(port, val) outp(port, val)
    #define inportb(port)       inp(port)
    #define outportw(port, val) outpw(port, val)
    #define inportw(port)       inpw(port)
#elif defined(__TURBOC__) || defined(__BORLANDC__)
    /* Turbo C / Borland C use outportb/inportb directly */
#else
    #error "Unsupported compiler"
#endif

/* Delay function - approximately 1ms */
static void delay_ms(uint32_t ms)
{
    /* Use BIOS timer tick (18.2 Hz) for rough delays */
    /* For short delays, use I/O port delay */
    volatile uint32_t i;
    while (ms--) {
        for (i = 0; i < 1000; i++) {
            inportb(0x80);  /* I/O delay ~1us */
        }
    }
}

/* Read 16-bit words from data port */
static void ide_read_data(ide_drive_t *drive, uint16_t *buffer, uint16_t count)
{
    uint16_t i;
    uint16_t port = drive->base_port + IDE_REG_DATA;
    
    for (i = 0; i < count; i++) {
        buffer[i] = inportw(port);
    }
}

/* Write 16-bit words to data port */
static void ide_write_data(ide_drive_t *drive, const uint16_t *buffer, uint16_t count)
{
    uint16_t i;
    uint16_t port = drive->base_port + IDE_REG_DATA;
    
    for (i = 0; i < count; i++) {
        outportw(port, buffer[i]);
    }
}

/*
 * Read status register
 */
uint8_t ide_read_status(ide_drive_t *drive)
{
    return inportb(drive->base_port + IDE_REG_STATUS);
}

/*
 * Read alternate status register (doesn't clear interrupt)
 */
uint8_t ide_read_alt_status(ide_drive_t *drive)
{
    return inportb(drive->ctrl_port + IDE_REG_ALT_STATUS);
}

/*
 * Wait for BSY flag to clear
 */
int ide_wait_not_busy(ide_drive_t *drive, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    uint8_t status;
    
    while (elapsed < timeout_ms) {
        status = ide_read_alt_status(drive);
        if (!(status & IDE_STATUS_BSY)) {
            return IDE_OK;
        }
        delay_ms(1);
        elapsed++;
    }
    return IDE_ERR_TIMEOUT;
}

/*
 * Wait for DRQ flag to set
 */
int ide_wait_drq(ide_drive_t *drive, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    uint8_t status;
    
    while (elapsed < timeout_ms) {
        status = ide_read_alt_status(drive);
        if (status & IDE_STATUS_ERR) {
            return IDE_ERR_COMMAND;
        }
        if (!(status & IDE_STATUS_BSY) && (status & IDE_STATUS_DRQ)) {
            return IDE_OK;
        }
        delay_ms(1);
        elapsed++;
    }
    return IDE_ERR_TIMEOUT;
}

/*
 * Select device (master or slave)
 */
int ide_select_device(ide_drive_t *drive)
{
    uint8_t dev_reg;
    int ret;
    
    /* Wait for not busy first */
    ret = ide_wait_not_busy(drive, IDE_TIMEOUT_READY);
    if (ret != IDE_OK) return ret;
    
    /* Select device */
    dev_reg = 0xA0 | (drive->device ? IDE_DEV_SLAVE : 0);
    outportb(drive->base_port + IDE_REG_DEVICE, dev_reg);
    
    /* Small delay after selection */
    delay_ms(1);
    
    /* Wait for device ready */
    return ide_wait_not_busy(drive, IDE_TIMEOUT_READY);
}

/*
 * Software reset the IDE channel
 */
void ide_soft_reset(ide_drive_t *drive)
{
    /* Set SRST bit */
    outportb(drive->ctrl_port + IDE_REG_DEV_CTRL, 0x04);
    delay_ms(5);
    
    /* Clear SRST bit */
    outportb(drive->ctrl_port + IDE_REG_DEV_CTRL, 0x00);
    delay_ms(5);
    
    /* Wait for reset to complete */
    ide_wait_not_busy(drive, IDE_TIMEOUT_BUSY);
}

/*
 * Detect if a drive is present on the given port/device
 */
int ide_detect_drive(ide_drive_t *drive, uint16_t base, uint16_t ctrl, uint8_t dev)
{
    uint8_t status;
    uint8_t cl, ch;
    
    /* Initialize drive structure */
    memset(drive, 0, sizeof(ide_drive_t));
    drive->base_port = base;
    drive->ctrl_port = ctrl;
    drive->device = dev;
    
    /* Check if anything is there by reading status */
    status = inportb(base + IDE_REG_STATUS);
    if (status == 0xFF) {
        return IDE_ERR_NO_DEVICE;  /* No device - floating bus */
    }
    
    /* Select the device */
    outportb(base + IDE_REG_DEVICE, 0xA0 | (dev ? IDE_DEV_SLAVE : 0));
    delay_ms(1);
    
    /* Check signature - ATAPI devices have 0x14, 0xEB in LBA mid/high */
    cl = inportb(base + IDE_REG_LBA_MID);
    ch = inportb(base + IDE_REG_LBA_HIGH);
    
    if (cl == 0x14 && ch == 0xEB) {
        drive->is_atapi = 1;
        return ide_identify_atapi(drive);
    }
    else if (cl == 0x00 && ch == 0x00) {
        /* Could be ATA device - not ATAPI */
        drive->is_atapi = 0;
        return IDE_ERR_NOT_ATAPI;
    }
    else if (cl == 0xFF && ch == 0xFF) {
        return IDE_ERR_NO_DEVICE;
    }
    
    return IDE_ERR_NO_DEVICE;
}

/*
 * Identify ATAPI device
 */
int ide_identify_atapi(ide_drive_t *drive)
{
    uint16_t buffer[256];
    int ret;
    int i;
    char *p;
    
    ret = ide_select_device(drive);
    if (ret != IDE_OK) return ret;
    
    /* Send IDENTIFY PACKET DEVICE command */
    outportb(drive->base_port + IDE_REG_COMMAND, IDE_CMD_IDENTIFY_PACKET);
    
    /* Wait for DRQ */
    ret = ide_wait_drq(drive, IDE_TIMEOUT_DRQ);
    if (ret != IDE_OK) return ret;
    
    /* Read 256 words of identification data */
    ide_read_data(drive, buffer, 256);
    
    /* Extract model string (words 27-46, 40 chars) */
    p = drive->model;
    for (i = 27; i <= 46; i++) {
        *p++ = (buffer[i] >> 8) & 0xFF;
        *p++ = buffer[i] & 0xFF;
    }
    drive->model[40] = '\0';
    
    /* Trim trailing spaces */
    for (i = 39; i >= 0 && drive->model[i] == ' '; i--) {
        drive->model[i] = '\0';
    }
    
    return IDE_OK;
}

/*
 * Send ATAPI packet command
 * direction: 0 = no data, 1 = read from device, 2 = write to device
 */
int atapi_packet_cmd(ide_drive_t *drive, const uint8_t *cdb,
                     uint8_t *buffer, uint16_t buflen, int direction)
{
    int ret;
    uint8_t status;
    uint8_t ir;
    uint16_t byte_count;
    uint16_t transfer_len;
    uint16_t total_read = 0;
    
    if (!drive->is_atapi) {
        return IDE_ERR_NOT_ATAPI;
    }
    
    /* Select device */
    ret = ide_select_device(drive);
    if (ret != IDE_OK) return ret;
    
    /* Set byte count limit */
    outportb(drive->base_port + IDE_REG_FEATURES, 0);  /* No DMA */
    outportb(drive->base_port + IDE_REG_LBA_MID, buflen & 0xFF);
    outportb(drive->base_port + IDE_REG_LBA_HIGH, (buflen >> 8) & 0xFF);
    
    /* Send PACKET command */
    outportb(drive->base_port + IDE_REG_COMMAND, IDE_CMD_PACKET);
    
    /* Wait for DRQ (device ready to receive CDB) */
    ret = ide_wait_drq(drive, IDE_TIMEOUT_DRQ);
    if (ret != IDE_OK) return ret;
    
    /* Send the 12-byte Command Descriptor Block */
    ide_write_data(drive, (const uint16_t *)cdb, 6);
    
    /* Handle data transfer if expected */
    if (direction == 1 && buffer != NULL && buflen > 0) {
        /* Read data from device */
        while (1) {
            /* Wait for not busy */
            ret = ide_wait_not_busy(drive, IDE_TIMEOUT_BUSY);
            if (ret != IDE_OK) return ret;
            
            status = ide_read_status(drive);
            
            /* Check for errors */
            if (status & IDE_STATUS_ERR) {
                return IDE_ERR_COMMAND;
            }
            
            /* Check if command complete */
            if (!(status & IDE_STATUS_DRQ)) {
                break;
            }
            
            /* Get transfer length from byte count registers */
            byte_count = inportb(drive->base_port + IDE_REG_LBA_MID);
            byte_count |= ((uint16_t)inportb(drive->base_port + IDE_REG_LBA_HIGH)) << 8;
            
            /* Limit to buffer size */
            transfer_len = (byte_count + 1) / 2;  /* Convert to words */
            if (total_read + byte_count > buflen) {
                transfer_len = (buflen - total_read) / 2;  /* Round down to avoid overrun */
            }
            
            /* Read data */
            if (transfer_len > 0) {
                ide_read_data(drive, (uint16_t *)(buffer + total_read), transfer_len);
                total_read += transfer_len * 2;
            }
        }
    }
    else if (direction == 2 && buffer != NULL && buflen > 0) {
        /* Write data to device */
        ret = ide_wait_drq(drive, IDE_TIMEOUT_DRQ);
        if (ret != IDE_OK) return ret;
        
        ide_write_data(drive, (const uint16_t *)buffer, (buflen + 1) / 2);
        
        /* Wait for completion */
        ret = ide_wait_not_busy(drive, IDE_TIMEOUT_BUSY);
        if (ret != IDE_OK) return ret;
        
        status = ide_read_status(drive);
        if (status & IDE_STATUS_ERR) {
            return IDE_ERR_COMMAND;
        }
    }
    else {
        /* No data - just wait for completion */
        ret = ide_wait_not_busy(drive, IDE_TIMEOUT_BUSY);
        if (ret != IDE_OK) return ret;
        
        status = ide_read_status(drive);
        if (status & IDE_STATUS_ERR) {
            return IDE_ERR_COMMAND;
        }
    }
    
    return IDE_OK;
}

/*
 * Send ATAPI packet command with UNIT ATTENTION retry
 * Wraps atapi_packet_cmd() to automatically clear and retry on
 * UNIT ATTENTION (sense key 0x06), which is commonly reported
 * after media changes on ATAPI devices.
 */
#define ATAPI_MAX_RETRIES 2

static int atapi_packet_cmd_retry(ide_drive_t *drive, const uint8_t *cdb,
                                  uint8_t *buffer, uint16_t buflen, int direction)
{
    int ret;
    int retries;
    uint8_t sense_cdb[12] = {0};
    uint8_t sense[18];
    
    for (retries = 0; retries <= ATAPI_MAX_RETRIES; retries++) {
        ret = atapi_packet_cmd(drive, cdb, buffer, buflen, direction);
        if (ret != IDE_ERR_COMMAND) {
            return ret;  /* Success or non-command error */
        }
        
        /* Command failed - issue REQUEST SENSE to check cause */
        sense_cdb[0] = ATAPI_CMD_REQUEST_SENSE;
        sense_cdb[4] = 18;
        memset(sense, 0, sizeof(sense));
        
        if (atapi_packet_cmd(drive, sense_cdb, sense, 18, 1) != IDE_OK) {
            return ret;  /* Can't even get sense data, give up */
        }
        
        /* Check for UNIT ATTENTION (sense key 0x06) */
        if ((sense[2] & 0x0F) != 0x06) {
            return ret;  /* Not UNIT ATTENTION, return original error */
        }
        
        /* UNIT ATTENTION cleared by REQUEST SENSE, retry the command */
    }
    
    return ret;  /* Exhausted retries */
}

/*
 * ATAPI TEST UNIT READY
 */
int atapi_test_unit_ready(ide_drive_t *drive)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_TEST_UNIT_READY;
    
    return atapi_packet_cmd(drive, cdb, NULL, 0, 0);
}

/*
 * ATAPI INQUIRY
 */
int atapi_inquiry(ide_drive_t *drive, uint8_t *buffer, uint8_t len)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_INQUIRY;
    cdb[4] = len;
    
    return atapi_packet_cmd_retry(drive, cdb, buffer, len, 1);
}

/*
 * ATAPI START/STOP UNIT
 * start: 1 = start/load, 0 = stop/eject
 * loej: 1 = load/eject operation, 0 = just start/stop
 */
int atapi_start_stop_unit(ide_drive_t *drive, uint8_t start, uint8_t loej)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_START_STOP_UNIT;
    cdb[4] = (loej ? SSU_LOEJ : 0) | (start ? SSU_START : 0);
    
    return atapi_packet_cmd_retry(drive, cdb, NULL, 0, 0);
}

/*
 * ATAPI REQUEST SENSE
 */
int atapi_request_sense(ide_drive_t *drive, uint8_t *buffer, uint8_t len)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_REQUEST_SENSE;
    cdb[4] = len;
    
    return atapi_packet_cmd(drive, cdb, buffer, len, 1);
}

/*
 * ATAPI GET EVENT STATUS NOTIFICATION
 */
int atapi_get_event_status(ide_drive_t *drive, uint8_t *buffer, uint8_t len)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_GET_EVENT_STATUS;
    cdb[1] = 0x01;  /* Polled */
    cdb[4] = 0x10;  /* Media event class */
    cdb[7] = 0;
    cdb[8] = len;
    
    return atapi_packet_cmd_retry(drive, cdb, buffer, len, 1);
}

/* ===================================================================
 * ZuluIDE Toolbox Commands
 * These require firmware support - will return IDE_ERR_COMMAND if
 * the firmware doesn't support them yet.
 * =================================================================== */

/*
 * Get count of available images
 */
int zuluide_get_image_count(ide_drive_t *drive, uint16_t *count)
{
    uint8_t cdb[12] = {0};
    uint8_t buffer[4];
    int ret;
    
    cdb[0] = ATAPI_CMD_ZULUIDE_COUNT;
    
    ret = atapi_packet_cmd_retry(drive, cdb, buffer, sizeof(buffer), 1);
    if (ret == IDE_OK) {
        *count = buffer[0] | ((uint16_t)buffer[1] << 8);
    }
    return ret;
}

/*
 * List images starting at index
 */
int zuluide_list_images(ide_drive_t *drive, uint8_t *buffer, uint16_t buflen, uint16_t start_idx)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_ZULUIDE_LIST;
    cdb[2] = start_idx & 0xFF;
    cdb[3] = (start_idx >> 8) & 0xFF;
    cdb[7] = (buflen >> 8) & 0xFF;
    cdb[8] = buflen & 0xFF;
    
    return atapi_packet_cmd_retry(drive, cdb, buffer, buflen, 1);
}

/*
 * Get current image filename
 */
int zuluide_get_current_image(ide_drive_t *drive, char *filename, uint16_t maxlen)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_ZULUIDE_CURRENT;
    cdb[7] = (maxlen >> 8) & 0xFF;
    cdb[8] = maxlen & 0xFF;
    
    return atapi_packet_cmd_retry(drive, cdb, (uint8_t *)filename, maxlen, 1);
}

/*
 * Select image by index
 */
int zuluide_select_image(ide_drive_t *drive, uint16_t index)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_ZULUIDE_SELECT;
    cdb[2] = index & 0xFF;
    cdb[3] = (index >> 8) & 0xFF;
    
    return atapi_packet_cmd_retry(drive, cdb, NULL, 0, 0);
}

/*
 * Select image by filename
 */
int zuluide_select_image_by_name(ide_drive_t *drive, const char *filename)
{
    uint8_t cdb[12] = {0};
    uint16_t len = strlen(filename) + 1;
    
    cdb[0] = ATAPI_CMD_ZULUIDE_SELECT;
    cdb[1] = 0x01;  /* By name flag */
    cdb[7] = (len >> 8) & 0xFF;
    cdb[8] = len & 0xFF;
    
    return atapi_packet_cmd_retry(drive, cdb, (uint8_t *)filename, len, 2);
}

/*
 * Load next image
 */
int zuluide_next_image(ide_drive_t *drive)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_ZULUIDE_NEXT;
    
    return atapi_packet_cmd_retry(drive, cdb, NULL, 0, 0);
}

/*
 * Load previous image
 */
int zuluide_prev_image(ide_drive_t *drive)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_ZULUIDE_PREV;
    
    return atapi_packet_cmd_retry(drive, cdb, NULL, 0, 0);
}

/*
 * Get ZuluIDE device info
 */
int zuluide_get_info(ide_drive_t *drive, uint8_t *buffer, uint16_t buflen)
{
    uint8_t cdb[12] = {0};
    
    cdb[0] = ATAPI_CMD_ZULUIDE_INFO;
    cdb[7] = (buflen >> 8) & 0xFF;
    cdb[8] = buflen & 0xFF;
    
    return atapi_packet_cmd_retry(drive, cdb, buffer, buflen, 1);
}
