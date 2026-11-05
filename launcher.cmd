@echo off
rem L19-dxc-l2 stage C guest launcher. Drive-agnostic (Startup hook or the Win+R line as in L7).
rem Payload: l2\ = upstream dxcompiler.dll + liberx_dxc_sm6.exe + liberx_dxc.exe + sm6\ + dxc.exe (optional)
rem          l1\ = Level 1 dxcompiler.dll + liberx_dxc.exe (control)
rem Output: C:\l6\com2-L19.txt and the objects in C:\l6\dxil\ (read both from the overlay afterwards).
setlocal EnableDelayedExpansion
set P=%~d0
if not exist C:\l6 mkdir C:\l6
if not exist C:\l6\dxil mkdir C:\l6\dxil
set OUT=C:\l6\com2-L19.txt
> %OUT% echo === LIBERX-VM L19 START ===
>> %OUT% echo PAYLOAD_DRIVE=%P%
>> %OUT% ver
>> %OUT% echo BEFORE time=%TIME% date=%DATE%
>> %OUT% echo --- step1 L2 liberx_dxc_sm6 (app-local dxcompiler.dll = upstream Level 2) ---
cd /d %P%\l2
%P%\l2\liberx_dxc_sm6.exe %P%\l2\sm6 C:\l6\dxil >> %OUT% 2>&1
>> %OUT% echo --- step1 exit=!ERRORLEVEL! ---
>> %OUT% echo --- step2 L2 liberx_dxc (Level 1 suite against Level 2: differences RECORDED, not gated) ---
%P%\l2\liberx_dxc.exe >> %OUT% 2>&1
>> %OUT% echo --- step2 exit=!ERRORLEVEL! ---
>> %OUT% echo --- step3 L1 control liberx_dxc (app-local dxcompiler.dll = Level 1; must stay 9/0/0) ---
cd /d %P%\l1
%P%\l1\liberx_dxc.exe >> %OUT% 2>&1
>> %OUT% echo --- step3 exit=!ERRORLEVEL! ---
>> %OUT% echo --- step4 dxc.exe CLI (best effort, not gated) ---
cd /d %P%\l2
if exist %P%\l2\dxc.exe (
  %P%\l2\dxc.exe -T cs_6_0 -E main %P%\l2\sm6\cs60_basic.hlsl -Fo C:\l6\dxil\cli_cs60_basic.dxil >> %OUT% 2>&1
  >> %OUT% echo --- step4 exit=!ERRORLEVEL! ---
  %P%\l2\dxc.exe --version >> %OUT% 2>&1
) else (
  >> %OUT% echo step4 SKIP no dxc.exe on payload
)
>> %OUT% echo --- system32 check ---
for %%D in (dxcompiler.dll dxil.dll dxilconv.dll ole32.dll ucrtbase.dll msvcrt.dll) do (
  if exist %SystemRoot%\system32\%%D (>> %OUT% echo system32\%%D PRESENT) else (>> %OUT% echo system32\%%D ABSENT)
)
>> %OUT% echo --- objects written ---
dir /b C:\l6\dxil >> %OUT% 2>&1
>> %OUT% echo AFTER time=%TIME% date=%DATE%
>> %OUT% echo === LIBERX-VM L19 END ===
endlocal
