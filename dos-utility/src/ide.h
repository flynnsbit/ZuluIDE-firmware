/*
 * ZuluIDE DOS Utility - IDE/ATAPI Low-Level Driver
 * Copyright (c) 2024
 *
 * IDE port definitions and ATAPI command interface
 */

#ifndef IDE_H
#define IDE_H

#include <stdint.h>

/* IDE I/O Port Base Addresses */
#define IDE_PRIMARY_BASE    0x1F0
#define IDE_PRIMARY_CTRL    0x3F6
#define IDE_SECONDARY_BASE  0x170
#define IDE_SECONDARY_CTRL  0x376

/* IDE Register Offsets from Base */
#define IDE_REG_DATA        0x00    /* Data register (16-bit) */
#define IDE_REG_ERROR       0x01    /* Error register (read) */
#define IDE_REG_FEATURES    0x01    /* Features register (write) */
#define IDE_REG_SECTOR_CNT  0x02    /* Sector count / Interrupt reason */
#define IDE_REG_LBA_LOW     0x03    /* LBA low / Byte count low */
#define IDE_REG_LBA_MID     0x04    /* LBA mid / Byte count low */
#define IDE_REG_LBA_HIGH    0x05    /* LBA high / Byte count high */
#define IDE_REG_DEVICE      0x06    /* Device/Head register */
#define IDE_REG_STATUS      0x07    /* Status register (read) */
#define IDE_REG_COMMAND     0x07    /* Command register (write) */

/* Control Register Offset */
#define IDE_REG_ALT_STATUS  0x00    /* Alternate status (read) */
#define IDE_REG_DEV_CTRL    0x00    /* Device control (write) */

/* IDE Status Register Bits */
#define IDE_STATUS_BSY      0x80    /* Busy */
#define IDE_STATUS_DRDY     0x40    /* Device ready */
#define IDE_STATUS_DF       0x20    /* Device fault */
#define IDE_STATUS_DSC      0x10    /* Drive seek complete */
#define IDE_STATUS_DRQ      0x08    /* Data request */
#define IDE_STATUS_CORR     0x04    /* Corrected data */
#define IDE_STATUS_IDX      0x02    /* Index */
#define IDE_STATUS_ERR      0x01    /* Error */

/* IDE Commands */
#define IDE_CMD_IDENTIFY        0xEC    /* Identify Device */
#define IDE_CMD_IDENTIFY_PACKET 0xA1    /* Identify Packet Device */
#define IDE_CMD_PACKET          0xA0    /* ATAPI Packet Command */
#define IDE_CMD_SOFT_RESET      0x08    /* Device Reset */

/* ATAPI Packet Commands (12-byte CDB) */
#define ATAPI_CMD_TEST_UNIT_READY   0x00
#define ATAPI_CMD_REQUEST_SENSE     0x03
#define ATAPI_CMD_INQUIRY           0x12
#define ATAPI_CMD_START_STOP_UNIT   0x1B
#define ATAPI_CMD_PREVENT_REMOVAL   0x1E
#define ATAPI_CMD_READ_CAPACITY     0x25
#define ATAPI_CMD_READ_TOC          0x43
#define ATAPI_CMD_GET_CONFIG        0x46
#define ATAPI_CMD_GET_EVENT_STATUS  0x4A
#define ATAPI_CMD_MODE_SENSE        0x5A

/* ZuluIDE Toolbox Vendor Commands (to be added to firmware) */
#define ATAPI_CMD_ZULUIDE_COUNT     0xD0    /* Get image count */
#define ATAPI_CMD_ZULUIDE_LIST      0xD1    /* List images */
#define ATAPI_CMD_ZULUIDE_CURRENT   0xD2    /* Get current image */
#define ATAPI_CMD_ZULUIDE_SELECT    0xD3    /* Select image by index */
#define ATAPI_CMD_ZULUIDE_NEXT      0xD4    /* Load next image */
#define ATAPI_CMD_ZULUIDE_PREV      0xD5    /* Load previous image */
#define ATAPI_CMD_ZULUIDE_INFO      0xD6    /* Get ZuluIDE info */

/* ATAPI Interrupt Reason (Sector Count register) */
#define ATAPI_IR_COD        0x01    /* 0=Data, 1=Command */
#define ATAPI_IR_IO         0x02    /* 0=To device, 1=To host */
#define ATAPI_IR_REL        0x04    /* Release */

/* START/STOP UNIT command bits */
#define SSU_LOEJ            0x02    /* Load/Eject */
#define SSU_START           0x01    /* Start/Stop */

/* Device selection */
#define IDE_DEV_MASTER      0x00
#define IDE_DEV_SLAVE       0x10

/* Timeout values (in milliseconds) */
#define IDE_TIMEOUT_READY   5000
#define IDE_TIMEOUT_DRQ     5000
#define IDE_TIMEOUT_BUSY    30000

/* Error codes */
#define IDE_OK              0
#define IDE_ERR_TIMEOUT     1
#define IDE_ERR_NO_DEVICE   2
#define IDE_ERR_NOT_ATAPI   3
#define IDE_ERR_COMMAND     4
#define IDE_ERR_DRQ         5
#define IDE_ERR_ABORTED     6

/* Drive info structure */
typedef struct {
    uint16_t base_port;         /* I/O base port */
    uint16_t ctrl_port;         /* Control port */
    uint8_t  device;            /* 0=master, 1=slave */
    uint8_t  is_atapi;          /* 1 if ATAPI device */
    char     model[41];         /* Model string */
    char     vendor[9];         /* Vendor string (ATAPI) */
    char     product[17];       /* Product string (ATAPI) */
    char     revision[5];       /* Revision string */
} ide_drive_t;

/* Function prototypes */

/* Low-level IDE functions */
uint8_t ide_read_status(ide_drive_t *drive);
uint8_t ide_read_alt_status(ide_drive_t *drive);
int ide_wait_not_busy(ide_drive_t *drive, uint32_t timeout_ms);
int ide_wait_drq(ide_drive_t *drive, uint32_t timeout_ms);
int ide_select_device(ide_drive_t *drive);
void ide_soft_reset(ide_drive_t *drive);

/* Device detection */
int ide_detect_drive(ide_drive_t *drive, uint16_t base, uint16_t ctrl, uint8_t dev);
int ide_identify_atapi(ide_drive_t *drive);

/* ATAPI packet interface */
int atapi_packet_cmd(ide_drive_t *drive, const uint8_t *cdb, 
                     uint8_t *buffer, uint16_t buflen, int direction);

/* High-level ATAPI commands */
int atapi_test_unit_ready(ide_drive_t *drive);
int atapi_inquiry(ide_drive_t *drive, uint8_t *buffer, uint8_t len);
int atapi_start_stop_unit(ide_drive_t *drive, uint8_t start, uint8_t loej);
int atapi_request_sense(ide_drive_t *drive, uint8_t *buffer, uint8_t len);
int atapi_get_event_status(ide_drive_t *drive, uint8_t *buffer, uint8_t len);

/* ZuluIDE Toolbox commands (require firmware support) */
int zuluide_get_image_count(ide_drive_t *drive, uint16_t *count);
int zuluide_list_images(ide_drive_t *drive, uint8_t *buffer, uint16_t buflen, uint16_t start_idx);
int zuluide_get_current_image(ide_drive_t *drive, char *filename, uint8_t maxlen);
int zuluide_select_image(ide_drive_t *drive, uint16_t index);
int zuluide_select_image_by_name(ide_drive_t *drive, const char *filename);
int zuluide_next_image(ide_drive_t *drive);
int zuluide_prev_image(ide_drive_t *drive);
int zuluide_get_info(ide_drive_t *drive, uint8_t *buffer, uint16_t buflen);

#endif /* IDE_H */
