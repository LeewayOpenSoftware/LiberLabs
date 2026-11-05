@echo off
set D=
for %%L in (d e f) do if exist %%L:\gate.cmd set D=%%L
if "%D%"=="" set D=d
%D%:
echo === S1 GATE ; drive %D% ===> %D%:\s1-result.txt
echo --- stage census:--->> %D%:\s1-result.txt
s0probe.exe enum>> %D%:\s1-result.txt 2>&1
echo --- install tlspoc identity to ROOT:--->> %D%:\s1-result.txt
s0probe.exe install ROOT>> %D%:\s1-result.txt 2>&1
echo --- hosts entry:--->> %D%:\s1-result.txt
echo 10.0.2.15 tlspoc.local>> C:\Windows\System32\drivers\etc\hosts
echo done>> %D%:\s1-result.txt
start /b %D%:\s0server.exe>> %D%:\s1-result.txt 2>&1
echo --- server start returned errorlevel %errorlevel%--->> %D%:\s1-result.txt
ping -n 6 127.0.0.1 >nul
echo --- R3 e2e: WinHTTPS to https://tlspoc.local with anchored CA:--->> %D%:\s1-result.txt
s0winhttp.exe x tlspoc.local 4443>> %D%:\s1-result.txt 2>&1
echo --- crypt32_apitest:--->> %D%:\s1-result.txt
crypt32_apitest.exe>> %D%:\s1-result.txt 2>&1
echo --- schannel_winetest:--->> %D%:\s1-result.txt
schannel_winetest.exe>> %D%:\s1-result.txt 2>&1
echo === GATE DONE--->> %D%:\s1-result.txt
taskkill /im s0server.exe /f>nul 2>&1
