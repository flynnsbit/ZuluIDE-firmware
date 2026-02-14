# ZuluIDE Toolbox Integration Patch
# 
# This document describes the modifications needed to integrate Toolbox
# vendor commands into the ZuluIDE firmware.
#
# Files to modify:
#   1. src/ide_atapi.h - Add Toolbox method declarations
#   2. src/ide_atapi.cpp - Add Toolbox commands to dispatch
#   3. src/atapi_constants.h - Add Toolbox command opcodes
#   4. zuluide.ini - Add EnableToolbox option
#
# New files:
#   1. src/ide_toolbox.h - Toolbox header
#   2. src/ide_toolbox.cpp - Toolbox implementation

================================================================================
MODIFICATION 1: src/atapi_constants.h
================================================================================
Add after line 100 (after ATAPI_CMD_SEND_DISC_STRUCTURE):

--- a/src/atapi_constants.h
+++ b/src/atapi_constants.h
@@ -98,7 +98,15 @@ X(ATAPI_CMD_READ_CD_MSF                   , 0xB9) \
 X(ATAPI_CMD_SET_CD_SPEED                  , 0xBB) \
 X(ATAPI_CMD_MECHANISM_STATUS              , 0xBD) \
 X(ATAPI_CMD_READ_CD                       , 0xBE) \
-X(ATAPI_CMD_SEND_DISC_STRUCTURE           , 0xBF)
+X(ATAPI_CMD_SEND_DISC_STRUCTURE           , 0xBF) \
+/* ZuluIDE Toolbox vendor commands */ \
+X(ATAPI_CMD_TOOLBOX_COUNT_IMAGES          , 0xD0) \
+X(ATAPI_CMD_TOOLBOX_LIST_IMAGES           , 0xD1) \
+X(ATAPI_CMD_TOOLBOX_GET_CURRENT           , 0xD2) \
+X(ATAPI_CMD_TOOLBOX_SELECT_IMAGE          , 0xD3) \
+X(ATAPI_CMD_TOOLBOX_NEXT_IMAGE            , 0xD4) \
+X(ATAPI_CMD_TOOLBOX_PREV_IMAGE            , 0xD5) \
+X(ATAPI_CMD_TOOLBOX_GET_INFO              , 0xD6)

================================================================================
MODIFICATION 2: src/ide_atapi.h
================================================================================
Add after line 217 (after atapi_write declaration):

--- a/src/ide_atapi.h
+++ b/src/ide_atapi.h
@@ -215,6 +215,15 @@ protected:
     virtual bool atapi_read_capacity(const uint8_t *cmd);
     virtual bool atapi_read(const uint8_t *cmd);
     virtual bool atapi_write(const uint8_t *cmd);
+    
+    // ZuluIDE Toolbox vendor command handlers
+    virtual bool atapi_toolbox_count_images(const uint8_t *cmd);
+    virtual bool atapi_toolbox_list_images(const uint8_t *cmd);
+    virtual bool atapi_toolbox_get_current(const uint8_t *cmd);
+    virtual bool atapi_toolbox_select_image(const uint8_t *cmd);
+    virtual bool atapi_toolbox_next_image(const uint8_t *cmd);
+    virtual bool atapi_toolbox_prev_image(const uint8_t *cmd);
+    virtual bool atapi_toolbox_get_info(const uint8_t *cmd);

================================================================================
MODIFICATION 3: src/ide_atapi.cpp
================================================================================
Add include at top:

+#include "ide_toolbox.h"

Modify handle_atapi_command() function around line 779:

--- a/src/ide_atapi.cpp
+++ b/src/ide_atapi.cpp
@@ -798,6 +798,16 @@ bool IDEATAPIDevice::handle_atapi_command(const uint8_t *cmd)
         case ATAPI_CMD_WRITE10:         return atapi_write(cmd);
         case ATAPI_CMD_WRITE12:         return atapi_write(cmd);
         case ATAPI_CMD_WRITE_AND_VERIFY10: return atapi_write(cmd);
+        
+        // ZuluIDE Toolbox vendor commands
+        case ATAPI_CMD_TOOLBOX_COUNT_IMAGES:  return atapi_toolbox_count_images(cmd);
+        case ATAPI_CMD_TOOLBOX_LIST_IMAGES:   return atapi_toolbox_list_images(cmd);
+        case ATAPI_CMD_TOOLBOX_GET_CURRENT:   return atapi_toolbox_get_current(cmd);
+        case ATAPI_CMD_TOOLBOX_SELECT_IMAGE:  return atapi_toolbox_select_image(cmd);
+        case ATAPI_CMD_TOOLBOX_NEXT_IMAGE:    return atapi_toolbox_next_image(cmd);
+        case ATAPI_CMD_TOOLBOX_PREV_IMAGE:    return atapi_toolbox_prev_image(cmd);
+        case ATAPI_CMD_TOOLBOX_GET_INFO:      return atapi_toolbox_get_info(cmd);

         default:
             logmsg("-- WARNING: Unsupported ATAPI command ", get_atapi_command_name(cmd[0]));

================================================================================
MODIFICATION 4: zuluide.ini
================================================================================
Add in [IDE] section:

+# EnableToolbox = 0 # Set to 1 to enable host-side Toolbox commands for image management

================================================================================
NEW FILE: src/ide_toolbox.h
================================================================================
(See src/ide_toolbox.h - already created)

================================================================================
NEW FILE: src/ide_toolbox.cpp
================================================================================
(See src/ide_toolbox.cpp - already created)

================================================================================
TESTING
================================================================================

To test the Toolbox commands:

1. Build firmware with PlatformIO:
   $ pio run

2. Flash to ZuluIDE device

3. Add to zuluide.ini on SD card:
   [IDE]
   EnableToolbox = 1

4. Use DOS utility to test:
   ZULUIDE scan
   ZULUIDE list
   ZULUIDE select game.iso

================================================================================
COMMAND REFERENCE
================================================================================

0xD0 - TOOLBOX_COUNT_IMAGES
  CDB: [D0 00 00 00 00 00 00 00 04 00 00 00]
  Response (4 bytes):
    [0-1] uint16_t image_count
    [2]   uint8_t  current_index (0xFF if none)
    [3]   uint8_t  flags (bit 0 = enabled)

0xD1 - TOOLBOX_LIST_IMAGES
  CDB: [D1 00 SS SS 00 00 00 LL LL 00 00 00]
    SS SS = start index (little-endian)
    LL LL = allocation length (big-endian, standard ATAPI)
  Response: Null-terminated filename strings, concatenated

0xD2 - TOOLBOX_GET_CURRENT
  CDB: [D2 00 00 00 00 00 00 LL LL 00 00 00]
    LL LL = allocation length (big-endian, standard ATAPI)
  Response: Null-terminated current image filename

0xD3 - TOOLBOX_SELECT_IMAGE
  By index:
    CDB: [D3 00 II II 00 00 00 00 00 00 00 00]
      II II = image index (little-endian)
  By name:
    CDB: [D3 01 00 00 00 00 00 LL LL 00 00 00]
      LL LL = filename length (big-endian)
    DATA OUT: Null-terminated filename

0xD4 - TOOLBOX_NEXT_IMAGE
  CDB: [D4 00 00 00 00 00 00 00 00 00 00 00]
  No data transfer

0xD5 - TOOLBOX_PREV_IMAGE
  CDB: [D5 00 00 00 00 00 00 00 00 00 00 00]
  No data transfer

0xD6 - TOOLBOX_GET_INFO
  CDB: [D6 00 00 00 00 00 00 00 40 00 00 00]
  Response (64 bytes):
    [0-3]   "ZUTB" magic
    [4]     Protocol version (1)
    [5]     Device type
    [6-7]   Firmware version
    [8-23]  Build string
    [24-63] Reserved
