@echo off
rem L19-dxc-l2 stage A1 guest launcher: the Level 1 differential suite on the REBASED head
rem (9b328b9fa), same layout as L7: dxc\ = liberx_dxc.exe + Level 1 dxcompiler.dll app-local,
rem dxc-nodll\ = liberx_dxc.exe alone (control). Writes C:\l6\com2-L19a.txt.
setlocal EnableDelayedExpansion
set P=%~d0
if not exist C:\l6 mkdir C:\l6
set OUT=C:\l6\com2-L19a.txt
> %OUT% echo === LIBERX-VM L19a START (Level 1 on rebased head 9b328b9fa) ===
>> %OUT% echo PAYLOAD_DRIVE=%P%
>> %OUT% ver
>> %OUT% echo --- step1 dxc (with Level 1 dxcompiler.dll) ---
cd /d %P%\dxc
%P%\dxc\liberx_dxc.exe >> %OUT% 2>&1
>> %OUT% echo --- step1 exit=!ERRORLEVEL! ---
>> %OUT% echo --- step2 dxc-nodll (control) ---
cd /d %P%\dxc-nodll
%P%\dxc-nodll\liberx_dxc.exe >> %OUT% 2>&1
>> %OUT% echo --- step2 exit=!ERRORLEVEL! ---
>> %OUT% echo --- system32 check ---
for %%D in (ole32.dll ucrtbase.dll msvcrt.dll dxcompiler.dll) do (
  if exist %SystemRoot%\system32\%%D (>> %OUT% echo system32\%%D PRESENT) else (>> %OUT% echo system32\%%D ABSENT)
)
>> %OUT% echo === LIBERX-VM L19a END ===
endlocal
