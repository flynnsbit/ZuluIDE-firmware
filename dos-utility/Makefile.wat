# ZuluIDE DOS Utility Makefile
# For use with Open Watcom C/C++ Compiler
#
# Build: wmake -f Makefile.wat
# Clean: wmake -f Makefile.wat clean

CC = wcc
CFLAGS = -bt=dos -ms -os -zq -w4
LINK = wlink
LFLAGS = system dos option quiet

TARGET = zuluide.exe
OBJS = zuluide.obj ide.obj

.c.obj:
	$(CC) $(CFLAGS) $<

$(TARGET): $(OBJS)
	$(LINK) $(LFLAGS) name $(TARGET) file zuluide.obj,ide.obj

zuluide.obj: zuluide.c ide.h

ide.obj: ide.c ide.h

clean: .SYMBOLIC
	del *.obj
	del $(TARGET)

all: clean $(TARGET)
