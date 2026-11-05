@echo off
rem L20-item10-crast-rov guest launcher. Drive-agnostic (Startup hook as in L6/L7).
rem Payload: base\ = liberx_contracts.exe only (image DLLs); integ\ = liberx_contracts.exe + the
rem 25-DLL set from the 8c674f984 build (d3d12.dll carries the new vkd3d); mesa\ = the LiberNT
rem Lavapipe/LLVMpipe MSI (optional; gives the image a Vulkan ICD so the d3d12 cases run).
rem Output: C:\l6\com2-L20.txt
setlocal EnableDelayedExpansion
set P=%~d0
if not exist C:\l6 mkdir C:\l6
set OUT=C:\l6\com2-L20.txt
> %OUT% echo === LIBERX-VM L20 START ===
>> %OUT% echo PAYLOAD_DRIVE=%P%
>> %OUT% ver
>> %OUT% echo --- step0 Vulkan ICD ---
if exist %P%\mesa\*.msi (
  for %%M in (%P%\mesa\*.msi) do (
    >> %OUT% echo installing %%M
    msiexec /i "%%M" /qn /norestart >> %OUT% 2>&1
    >> %OUT% echo --- msiexec exit=!ERRORLEVEL! ---
  )
) else (
  >> %OUT% echo no mesa\*.msi on payload: d3d12 cases will SKIP without an ICD
)
reg query "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" >> %OUT% 2>&1
if exist %SystemRoot%\system32\mesa_llvmpipe.dll (>> %OUT% echo system32\mesa_llvmpipe.dll PRESENT) else (>> %OUT% echo system32\mesa_llvmpipe.dll ABSENT)
for %%F in (liberx_contracts) do (
  >> %OUT% echo --- base %%F ---
  cd /d %P%\base
  %P%\base\%%F.exe >> %OUT% 2>&1
  >> %OUT% echo --- base %%F exit=!ERRORLEVEL! ---
  >> %OUT% echo --- integ %%F ---
  cd /d %P%\integ
  %P%\integ\%%F.exe >> %OUT% 2>&1
  >> %OUT% echo --- integ %%F exit=!ERRORLEVEL! ---
)
>> %OUT% echo === LIBERX-VM L20 END ===
endlocal
