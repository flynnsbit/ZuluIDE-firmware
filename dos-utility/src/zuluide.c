/*
 * ZuluIDE DOS Utility
 * Copyright (c) 2024
 *
 * Command-line utility for controlling ZuluIDE devices from MS-DOS
 * 
 * Usage:
 *   ZULUIDE [options] <command> [arguments]
 *
 * Commands:
 *   scan              - Scan for ZuluIDE devices
 *   info              - Show device information
 *   status            - Show current status
 *   eject             - Eject current media (loads next image)
 *   next              - Load next image
 *   prev              - Load previous image
 *   list              - List available images (requires firmware support)
 *   select <name>     - Select image by name (requires firmware support)
 *
 * Options:
 *   /P                - Use primary IDE channel (default)
 *   /S                - Use secondary IDE channel
 *   /M                - Use master device (default)
 *   /L                - Use slave device
 *   /D:n              - Use specific drive (0-3)
 *   /Q                - Quiet mode
 *   /V                - Verbose mode
 *   /?                - Show help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ide.h"

#define VERSION "1.1.0"

/* Configuration */
typedef struct {
    uint8_t channel;        /* 0=primary, 1=secondary */
    uint8_t device;         /* 0=master, 1=slave */
    uint8_t quiet;          /* Suppress output */
    uint8_t verbose;        /* Extra output */
    uint8_t drive_num;      /* Explicit drive number (0-3) */
    uint8_t use_drive_num;  /* Use explicit drive number */
} config_t;

static config_t g_config = {0, 0, 0, 0, 0, 0};
static ide_drive_t g_drive;

/* Function prototypes */
static void print_help(void);
static int parse_args(int argc, char *argv[], char **cmd, char **arg);
static int cmd_scan(void);
static int cmd_info(void);
static int cmd_status(void);
static int cmd_eject(void);
static int cmd_next(void);
static int cmd_prev(void);
static int cmd_list(void);
static int cmd_select(const char *name);
static int find_zuluide(void);
static const char *get_error_string(int err);
static void print_inquiry(const uint8_t *data);

/*
 * Main entry point
 */
int main(int argc, char *argv[])
{
    char *cmd = NULL;
    char *arg = NULL;
    int ret;
    
    if (!g_config.quiet) {
        printf("ZuluIDE DOS Utility v%s\n", VERSION);
        printf("Copyright (c) 2024\n\n");
    }
    
    /* Parse command line */
    ret = parse_args(argc, argv, &cmd, &arg);
    if (ret != 0) {
        return ret;
    }
    
    if (cmd == NULL) {
        print_help();
        return 0;
    }
    
    /* Convert command to uppercase for comparison */
    strupr(cmd);
    
    /* Execute command */
    if (strcmp(cmd, "SCAN") == 0) {
        return cmd_scan();
    }
    else if (strcmp(cmd, "INFO") == 0) {
        return cmd_info();
    }
    else if (strcmp(cmd, "STATUS") == 0) {
        return cmd_status();
    }
    else if (strcmp(cmd, "EJECT") == 0) {
        return cmd_eject();
    }
    else if (strcmp(cmd, "NEXT") == 0) {
        return cmd_next();
    }
    else if (strcmp(cmd, "PREV") == 0) {
        return cmd_prev();
    }
    else if (strcmp(cmd, "LIST") == 0) {
        return cmd_list();
    }
    else if (strcmp(cmd, "SELECT") == 0) {
        if (arg == NULL) {
            printf("Error: SELECT requires an image name\n");
            return 1;
        }
        return cmd_select(arg);
    }
    else {
        printf("Error: Unknown command '%s'\n", cmd);
        print_help();
        return 1;
    }
}

/*
 * Print help message
 */
static void print_help(void)
{
    printf("Usage: ZULUIDE [options] <command> [arguments]\n\n");
    printf("Commands:\n");
    printf("  scan              Scan for ZuluIDE/ATAPI devices\n");
    printf("  info              Show device information\n");
    printf("  status            Show current media status\n");
    printf("  eject             Eject current media (loads next image)\n");
    printf("  next              Load next image*\n");
    printf("  prev              Load previous image*\n");
    printf("  list              List available images*\n");
    printf("  select <name>     Select image by filename*\n");
    printf("\n");
    printf("Options:\n");
    printf("  /P                Use primary IDE channel (default)\n");
    printf("  /S                Use secondary IDE channel\n");
    printf("  /M                Use master device (default)\n");
    printf("  /L                Use slave device\n");
    printf("  /D:n              Use specific drive number (0-3)\n");
    printf("  /Q                Quiet mode\n");
    printf("  /V                Verbose mode\n");
    printf("  /?                Show this help\n");
    printf("\n");
    printf("* Requires ZuluIDE firmware with Toolbox support\n");
    printf("\n");
    printf("Examples:\n");
    printf("  ZULUIDE scan              Scan all IDE channels\n");
    printf("  ZULUIDE /S /L info        Info from secondary slave\n");
    printf("  ZULUIDE eject             Eject and load next image\n");
    printf("  ZULUIDE list              List available images\n");
    printf("  ZULUIDE select game.iso   Load specific image\n");
}

/*
 * Parse command line arguments
 */
static int parse_args(int argc, char *argv[], char **cmd, char **arg)
{
    int i;
    char *p;
    
    *cmd = NULL;
    *arg = NULL;
    
    for (i = 1; i < argc; i++) {
        p = argv[i];
        
        if (*p == '/' || *p == '-') {
            p++;
            switch (toupper(*p)) {
                case 'P':
                    g_config.channel = 0;
                    break;
                case 'S':
                    g_config.channel = 1;
                    break;
                case 'M':
                    g_config.device = 0;
                    break;
                case 'L':
                    g_config.device = 1;
                    break;
                case 'D':
                    if (p[1] == ':' && isdigit(p[2])) {
                        g_config.drive_num = p[2] - '0';
                        g_config.use_drive_num = 1;
                        if (g_config.drive_num > 3) {
                            printf("Error: Drive number must be 0-3\n");
                            return 1;
                        }
                    }
                    break;
                case 'Q':
                    g_config.quiet = 1;
                    break;
                case 'V':
                    g_config.verbose = 1;
                    break;
                case '?':
                case 'H':
                    print_help();
                    return -1;
                default:
                    printf("Error: Unknown option '%s'\n", argv[i]);
                    return 1;
            }
        }
        else {
            /* First non-option is command */
            if (*cmd == NULL) {
                *cmd = argv[i];
            }
            /* Second non-option is argument */
            else if (*arg == NULL) {
                *arg = argv[i];
            }
            else {
                printf("Error: Too many arguments\n");
                return 1;
            }
        }
    }
    
    return 0;
}

/*
 * Find ZuluIDE device
 */
static int find_zuluide(void)
{
    uint16_t base, ctrl;
    int ret;
    
    /* If explicit drive number specified, use that */
    if (g_config.use_drive_num) {
        g_config.channel = (g_config.drive_num >> 1) & 1;
        g_config.device = g_config.drive_num & 1;
    }
    
    /* Get port addresses */
    if (g_config.channel == 0) {
        base = IDE_PRIMARY_BASE;
        ctrl = IDE_PRIMARY_CTRL;
    }
    else {
        base = IDE_SECONDARY_BASE;
        ctrl = IDE_SECONDARY_CTRL;
    }
    
    /* Detect drive */
    ret = ide_detect_drive(&g_drive, base, ctrl, g_config.device);
    if (ret != IDE_OK) {
        if (!g_config.quiet) {
            printf("Error: %s on %s %s\n",
                   get_error_string(ret),
                   g_config.channel ? "secondary" : "primary",
                   g_config.device ? "slave" : "master");
        }
        return ret;
    }
    
    if (!g_drive.is_atapi) {
        if (!g_config.quiet) {
            printf("Error: Device is not ATAPI\n");
        }
        return IDE_ERR_NOT_ATAPI;
    }
    
    return IDE_OK;
}

/*
 * Scan for ATAPI devices
 */
static int cmd_scan(void)
{
    uint16_t bases[] = {IDE_PRIMARY_BASE, IDE_SECONDARY_BASE};
    uint16_t ctrls[] = {IDE_PRIMARY_CTRL, IDE_SECONDARY_CTRL};
    const char *channels[] = {"Primary", "Secondary"};
    const char *devices[] = {"Master", "Slave"};
    ide_drive_t drive;
    int ch, dev;
    int found = 0;
    int ret;
    uint8_t inquiry[36];
    
    printf("Scanning IDE channels for ATAPI devices...\n\n");
    
    for (ch = 0; ch < 2; ch++) {
        for (dev = 0; dev < 2; dev++) {
            printf("%s %s: ", channels[ch], devices[dev]);
            
            ret = ide_detect_drive(&drive, bases[ch], ctrls[ch], dev);
            
            if (ret == IDE_OK && drive.is_atapi) {
                found++;
                printf("ATAPI device found\n");
                printf("  Model: %s\n", drive.model);
                
                /* Try INQUIRY for more details */
                ret = atapi_inquiry(&drive, inquiry, 36);
                if (ret == IDE_OK) {
                    print_inquiry(inquiry);
                }
                printf("\n");
            }
            else if (ret == IDE_ERR_NOT_ATAPI) {
                printf("ATA device (not ATAPI)\n");
            }
            else {
                printf("No device\n");
            }
        }
    }
    
    if (found == 0) {
        printf("\nNo ATAPI devices found.\n");
    }
    else {
        printf("Found %d ATAPI device(s).\n", found);
    }
    
    return 0;
}

/*
 * Show device information
 */
static int cmd_info(void)
{
    int ret;
    uint8_t inquiry[36];
    
    ret = find_zuluide();
    if (ret != IDE_OK) return ret;
    
    printf("Device: %s %s\n",
           g_config.channel ? "Secondary" : "Primary",
           g_config.device ? "Slave" : "Master");
    printf("Model:  %s\n", g_drive.model);
    
    ret = atapi_inquiry(&g_drive, inquiry, 36);
    if (ret == IDE_OK) {
        print_inquiry(inquiry);
    }
    
    /* Try ZuluIDE-specific info command */
    {
        uint8_t info[64];
        memset(info, 0, sizeof(info));
        ret = zuluide_get_info(&g_drive, info, sizeof(info));
        if (ret == IDE_OK && memcmp(info, "ZUTB", 4) == 0) {
            char build_str[17];
            const char *devtype_str;
            
            printf("\nZuluIDE Toolbox:\n");
            printf("  Protocol:  v%u\n", info[4]);
            
            switch (info[5]) {
                case 0x00: devtype_str = "CD-ROM"; break;
                case 0x05: devtype_str = "CD-ROM"; break;
                case 0x07: devtype_str = "Optical"; break;
                default:   devtype_str = "Unknown"; break;
            }
            printf("  Dev Type:  %s (0x%02X)\n", devtype_str, info[5]);
            
            memcpy(build_str, &info[8], 16);
            build_str[16] = '\0';
            if (build_str[0]) {
                printf("  Build:     %s\n", build_str);
            }
            
            if (g_config.verbose) {
                printf("  FW Bytes:  %u.%u\n", info[6], info[7]);
            }
        }
        else if (g_config.verbose) {
            printf("\nZuluIDE Toolbox: Not available\n");
        }
    }
    
    return 0;
}

/*
 * Show current status
 */
static int cmd_status(void)
{
    int ret;
    uint8_t sense[18];
    uint8_t event[8];
    
    ret = find_zuluide();
    if (ret != IDE_OK) return ret;
    
    printf("Device: %s %s\n",
           g_config.channel ? "Secondary" : "Primary",
           g_config.device ? "Slave" : "Master");
    printf("Model:  %s\n", g_drive.model);
    
    /* Test unit ready */
    ret = atapi_test_unit_ready(&g_drive);
    if (ret == IDE_OK) {
        printf("Status: Ready (media present)\n");
    }
    else {
        /* Get sense data for details */
        atapi_request_sense(&g_drive, sense, 18);
        if (sense[2] == 0x02) {  /* Not ready */
            if (sense[12] == 0x3A) {
                printf("Status: No media present\n");
            }
            else {
                printf("Status: Not ready (ASC=%02Xh)\n", sense[12]);
            }
        }
        else if (sense[2] == 0x06) {  /* Unit attention */
            printf("Status: Media changed\n");
        }
        else {
            printf("Status: Error (Sense=%02Xh, ASC=%02Xh)\n", 
                   sense[2], sense[12]);
        }
    }
    
    /* Try to get current image name (ZuluIDE Toolbox) */
    {
        char filename[256];
        memset(filename, 0, sizeof(filename));
        ret = zuluide_get_current_image(&g_drive, filename, sizeof(filename));
        if (ret == IDE_OK && filename[0] != '\0') {
            printf("Image:  %s\n", filename);
        }
    }
    
    return 0;
}

/*
 * Eject current media (this triggers next image load on ZuluIDE)
 */
static int cmd_eject(void)
{
    int ret;
    
    ret = find_zuluide();
    if (ret != IDE_OK) return ret;
    
    if (!g_config.quiet) {
        printf("Ejecting media on %s %s...\n",
               g_config.channel ? "secondary" : "primary",
               g_config.device ? "slave" : "master");
    }
    
    /* Send START/STOP UNIT with LOEJ=1, START=0 (eject) */
    ret = atapi_start_stop_unit(&g_drive, 0, 1);
    
    if (ret == IDE_OK) {
        if (!g_config.quiet) {
            printf("Eject command sent successfully.\n");
            printf("ZuluIDE should now load the next image.\n");
        }
    }
    else {
        printf("Error: %s\n", get_error_string(ret));
    }
    
    return ret;
}

/*
 * Load next image (currently same as eject)
 */
static int cmd_next(void)
{
    int ret;
    
    ret = find_zuluide();
    if (ret != IDE_OK) return ret;
    
    if (!g_config.quiet) {
        printf("Loading next image...\n");
    }
    
    /* First try ZuluIDE Toolbox command */
    ret = zuluide_next_image(&g_drive);
    
    if (ret != IDE_OK) {
        /* Fall back to eject method */
        if (g_config.verbose) {
            printf("Toolbox not supported, using eject method.\n");
        }
        ret = atapi_start_stop_unit(&g_drive, 0, 1);
    }
    
    if (ret == IDE_OK) {
        if (!g_config.quiet) {
            printf("Next image command sent successfully.\n");
        }
    }
    else {
        printf("Error: %s\n", get_error_string(ret));
    }
    
    return ret;
}

/*
 * Load previous image (requires firmware Toolbox support)
 */
static int cmd_prev(void)
{
    int ret;
    
    ret = find_zuluide();
    if (ret != IDE_OK) return ret;
    
    if (!g_config.quiet) {
        printf("Loading previous image...\n");
    }
    
    ret = zuluide_prev_image(&g_drive);
    
    if (ret == IDE_OK) {
        if (!g_config.quiet) {
            printf("Previous image command sent successfully.\n");
        }
    }
    else if (ret == IDE_ERR_COMMAND) {
        printf("Error: Toolbox commands not supported by firmware.\n");
        printf("Please update ZuluIDE firmware to a version with Toolbox support.\n");
    }
    else {
        printf("Error: %s\n", get_error_string(ret));
    }
    
    return ret;
}

/*
 * List available images (requires firmware Toolbox support)
 */
static int cmd_list(void)
{
    int ret;
    uint16_t count = 0;
    uint8_t buffer[2048];
    uint16_t i;
    char *p;
    
    ret = find_zuluide();
    if (ret != IDE_OK) return ret;
    
    if (!g_config.quiet) {
        printf("Querying available images...\n\n");
    }
    
    /* Get image count */
    ret = zuluide_get_image_count(&g_drive, &count);
    if (ret != IDE_OK) {
        printf("Error: Toolbox commands not supported by firmware.\n");
        printf("Please update ZuluIDE firmware to a version with Toolbox support.\n");
        return ret;
    }
    
    printf("Found %u images:\n\n", count);
    
    /* List images */
    memset(buffer, 0, sizeof(buffer));
    ret = zuluide_list_images(&g_drive, buffer, sizeof(buffer), 0);
    if (ret != IDE_OK) {
        printf("Error reading image list: %s\n", get_error_string(ret));
        return ret;
    }
    
    /* Parse and display image list */
    /* Format: each entry is null-terminated filename */
    p = (char *)buffer;
    i = 0;
    while (*p && i < count) {
        printf("  %3u. %s\n", i + 1, p);
        p += strlen(p) + 1;
        i++;
    }
    
    printf("\n");
    
    /* Show current image */
    {
        char current[256];
        memset(current, 0, sizeof(current));
        ret = zuluide_get_current_image(&g_drive, current, sizeof(current));
        if (ret == IDE_OK && current[0]) {
            printf("Current: %s\n", current);
        }
    }
    
    return 0;
}

/*
 * Select image by name (requires firmware Toolbox support)
 */
static int cmd_select(const char *name)
{
    int ret;
    
    ret = find_zuluide();
    if (ret != IDE_OK) return ret;
    
    if (!g_config.quiet) {
        printf("Selecting image: %s\n", name);
    }
    
    ret = zuluide_select_image_by_name(&g_drive, name);
    
    if (ret == IDE_OK) {
        if (!g_config.quiet) {
            printf("Image selected successfully.\n");
        }
    }
    else if (ret == IDE_ERR_COMMAND) {
        printf("Error: Toolbox commands not supported or image not found.\n");
        printf("Make sure:\n");
        printf("  1. ZuluIDE firmware has Toolbox support\n");
        printf("  2. Image filename is correct\n");
    }
    else {
        printf("Error: %s\n", get_error_string(ret));
    }
    
    return ret;
}

/*
 * Get error string
 */
static const char *get_error_string(int err)
{
    switch (err) {
        case IDE_OK:          return "Success";
        case IDE_ERR_TIMEOUT: return "Timeout waiting for device";
        case IDE_ERR_NO_DEVICE: return "No device detected";
        case IDE_ERR_NOT_ATAPI: return "Device is not ATAPI";
        case IDE_ERR_COMMAND: return "Command error";
        case IDE_ERR_DRQ:     return "Data request error";
        case IDE_ERR_ABORTED: return "Command aborted";
        default:              return "Unknown error";
    }
}

/*
 * Print INQUIRY data
 */
static void print_inquiry(const uint8_t *data)
{
    char vendor[9], product[17], revision[5];
    uint8_t devtype;
    
    devtype = data[0] & 0x1F;
    
    memcpy(vendor, &data[8], 8);
    vendor[8] = '\0';
    
    memcpy(product, &data[16], 16);
    product[16] = '\0';
    
    memcpy(revision, &data[32], 4);
    revision[4] = '\0';
    
    /* Trim trailing spaces */
    {
        int i;
        for (i = 7; i >= 0 && vendor[i] == ' '; i--) vendor[i] = '\0';
        for (i = 15; i >= 0 && product[i] == ' '; i--) product[i] = '\0';
        for (i = 3; i >= 0 && revision[i] == ' '; i--) revision[i] = '\0';
    }
    
    printf("  Type:     ");
    switch (devtype) {
        case 0x00: printf("Direct access (disk)\n"); break;
        case 0x05: printf("CD-ROM\n"); break;
        case 0x07: printf("Optical memory\n"); break;
        default:   printf("0x%02X\n", devtype); break;
    }
    
    if (vendor[0]) printf("  Vendor:   %s\n", vendor);
    if (product[0]) printf("  Product:  %s\n", product);
    if (revision[0]) printf("  Revision: %s\n", revision);
    
    if (data[1] & 0x80) {
        printf("  Media:    Removable\n");
    }
}
