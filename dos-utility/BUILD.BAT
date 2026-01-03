@echo off
REM ZuluIDE DOS Utility Build Script
REM For Turbo C / Borland C
REM
REM Usage: BUILD.BAT

echo Building ZuluIDE DOS Utility...

REM Check for Turbo C
if exist \tc\bin\tcc.exe goto turboc
if exist \bc\bin\bcc.exe goto borlandc
if exist \watcom\binw\wcc.exe goto watcom

echo Error: No supported compiler found.
echo Please install Turbo C, Borland C, or Open Watcom.
goto end

:turboc
echo Using Turbo C...
cd src
\tc\bin\tcc -ml -O -Z -DTURBO_C -o..\zuluide.exe zuluide.c ide.c
cd ..
goto done

:borlandc  
echo Using Borland C...
cd src
\bc\bin\bcc -ml -O2 -DBORLAND_C -e..\zuluide.exe zuluide.c ide.c
cd ..
goto done

:watcom
echo Using Open Watcom...
cd src
\watcom\binw\wcc -bt=dos -ms -os zuluide.c
\watcom\binw\wcc -bt=dos -ms -os ide.c
\watcom\binw\wlink system dos name ..\zuluide.exe file zuluide.obj,ide.obj
del *.obj
cd ..
goto done

:done
if exist zuluide.exe (
    echo.
    echo Build successful: zuluide.exe
) else (
    echo.
    echo Build failed!
)

:end
