# DX repo: import library only. comdlg32.dll itself is not built in this tree; the spec
# (LiberNT-LDW 5a371715618) gives the nested dxilconv/dxcompiler and mesa-llvmpipe
# links, and the delay-import users (setupapi, shlwapi), a libcomdlg32 target.
spec2def(comdlg32.dll comdlg32.spec ADD_IMPORTLIB)
