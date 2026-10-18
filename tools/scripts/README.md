# Game script tools

Run from a working folder that has a `bnk/` subfolder (output goes there; keep it out of the repo).

- `extract.py`: reads the `gamescripts.bnk` index (needs `bnk/gamescripts.idx`, made by the inline
  index decoder described in docs/findings.md R33) and writes every script to `bnk/all/`.
- `luadis.py`: parses Fable III's Lua 5.1 bytecode (it has an extra function-name string per
  function). The opcodes are remapped, so the instruction listing is not meaningful yet, but
  constants, locals and function names are.
- `luafuncs.py <file> <regex>`: prints the constants and locals of the functions whose names match.
- `luagrep.py <name>`, `luagrep2.py <name...>`: finds the script functions that use every given constant.
