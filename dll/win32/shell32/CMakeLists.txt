# DX repo: import library only. shell32.dll itself is not built in this tree; the spec
# (LiberNT-LDW 5a371715618) gives the nested dxilconv/dxcompiler and mesa-llvmpipe
# links, and the delay-import users (setupapi, shlwapi, printui), a libshell32 target.
spec2def(shell32.dll shell32.spec ADD_IMPORTLIB)
