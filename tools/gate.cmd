@echo off
set D=
for %%L in (d e f) do if exist %%L:\gate.cmd set D=%%L
if "%D%"=="" set D=d
%D%:
set R=%D%:\s1-result.txt
echo === S1b GATE ; drive %D% ===> %R%
echo --- stage census:--->> %R%
s0probe.exe enum>> %R% 2>&1
echo --- install tlspoc identity to ROOT:--->> %R%
s0probe.exe install ROOT>> %R% 2>&1
echo --- server (own log, never the result file):--->> %R%
start /b %D%:\s0server.exe> %D%:\s0server.log 2>&1
ping -n 6 127.0.0.1 >nul
echo --- R3a e2e by IP 10.0.2.15, no ignore flags (SAN IP:10.0.2.15; PASS = SUCCESS http status=200):--->> %R%
s0winhttp.exe x 10.0.2.15 4443>> %R% 2>&1
echo --- R3b control by IP 127.0.0.1, no ignore flags (not in the SAN; expect 12157 name mismatch, proves the name check runs):--->> %R%
s0winhttp.exe x 127.0.0.1 4443>> %R% 2>&1
echo --- crypt32_apitest CertCreateChainEngine:--->> %R%
crypt32_apitest.exe CertCreateChainEngine>> %R% 2>&1
echo --- crypt32_apitest CertOpenSystemStore:--->> %R%
crypt32_apitest.exe CertOpenSystemStore>> %R% 2>&1
echo --- schannel_winetest:--->> %R%
schannel_winetest.exe>> %R% 2>&1
echo === GATE DONE--->> %R%
taskkill /im s0server.exe /f>nul 2>&1
ping -n 3 127.0.0.1 >nul
echo --- server log (read after the server stopped):--->> %R%
type %D%:\s0server.log>> %R% 2>&1
echo === LOG DONE--->> %R%
