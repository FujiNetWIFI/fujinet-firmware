@ECHO OFF

REM Bypass "Terminate Batch Job" prompt.
IF "%~1"=="-FIXED_CTRL_C" (
    REM Remove the -FIXED_CTRL_C parameter
    SHIFT
) ELSE (
    ECHO Re-run
    REM Run the batch with <NUL and -FIXED_CTRL_C
    CALL <NUL %0 -FIXED_CTRL_C %*
    GOTO :EOF
)

ECHO Starting FujiNet

:START
fujinet.exe %1 %2 %3 %4 %5 %6 %7 %8

REM from sysexits.h
REM #define EX_TEMPFAIL     75      /* temp failure; user is invited to retry */

IF %ERRORLEVEL% EQU 75 (
    ECHO Restarting FujiNet 
    GOTO START
)

ECHO FujiNet ended with exit code %ERRORLEVEL%
