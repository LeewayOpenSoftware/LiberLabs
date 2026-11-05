@echo off
rem L21-item9-shader-matrix guest launcher. Drive-agnostic (Startup hook as in L6/L7). No Vulkan ICD needed.
rem Payload: base\ = liberx_shaders.exe only (image d3dcompiler_47.dll + vkd3d);
rem          integ\ = liberx_shaders.exe + the 25-DLL set from the 3eb9f516b build (d3dcompiler_47.dll app-local).
rem Output: C:\l6\com2-L21.txt
setlocal EnableDelayedExpansion
set P=%~d0
if not exist C:\l6 mkdir C:\l6
set OUT=C:\l6\com2-L21.txt
> %OUT% echo === LIBERX-VM L21 START ===
>> %OUT% echo PAYLOAD_DRIVE=%P%
>> %OUT% ver
for %%F in (liberx_shaders) do (
  >> %OUT% echo --- base %%F ---
  cd /d %P%\base
  %P%\base\%%F.exe >> %OUT% 2>&1
  >> %OUT% echo --- base %%F exit=!ERRORLEVEL! ---
  >> %OUT% echo --- integ %%F ---
  cd /d %P%\integ
  %P%\integ\%%F.exe >> %OUT% 2>&1
  >> %OUT% echo --- integ %%F exit=!ERRORLEVEL! ---
)
>> %OUT% echo === LIBERX-VM L21 END ===
endlocal
