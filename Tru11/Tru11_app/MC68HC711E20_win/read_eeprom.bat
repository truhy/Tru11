@ECHO OFF
CALL env_win.bat

:: Run
SET runcmd=%APP% read path=%SERIALPATH% from_addr=0xb600 to_addr=0xb7ff file=eeprom.s19
ECHO %runcmd%
%runcmd% & IF %errorlevel% NEQ 0 GOTO :err_handler

:: Pause if run from double-click
IF /I %0 EQU "%~dpnx0" PAUSE

GOTO :end_of_script

:err_handler
:: Pause if run from double-click
IF /I %0 EQU "%~dpnx0" PAUSE

:end_of_script
