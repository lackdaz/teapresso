#!/usr/bin/env python3
# gen_recipe.py — .secret_recipe -> compiler -D flags
#
# PlatformIO pre-build hook. Add to platformio.ini:
#     extra_scripts = pre:gen_recipe.py
#
# .secret_recipe format, one cycle per line:
#     <NAME>,<brew_seconds>,<wait_seconds>
# e.g.  C1,5,20
# Blank lines and everything after '#' are ignored.
#
# Emits -DC1_B=5 -DC1_W=20 ... for use directly in main.cpp.
# Run standalone (python3 gen_recipe.py) to check the file parses.

import sys

PUMP_MAX_S = 30
WAIT_MAX_S = 120

def parse(path=".secret_recipe"):
    vals = {}
    for n,line in enumerate(open(path), 1): # start from count 1
        line = line.split("#")[0].strip() # skip comments
        if not line:
            continue # skip comment headers
        items = [p.strip() for p in line.split(",")]
        if len(items) != 3:
            raise SystemExit(f"gen_recipe: line {n}: expected NAME,brew,wait")
        cycle, b, w = items
        try:
            b, w = int(b), int(w)
        except ValueError:
            raise SystemExit(f"gen_recipe: line {n}: brew/wait must be whole numbers")
        if not 0 < b <= PUMP_MAX_S:
            raise SystemExit(f"gen_recipe: line {n}: brew {b}s out of range 1-{PUMP_MAX_S}")
        if not 0 < w <= WAIT_MAX_S:
            raise SystemExit(f"gen_recipe: line {n}: wait {w}s out of range 1-{WAIT_MAX_S}")        
        vals[f"{cycle.upper()}_B"] = b
        vals[f"{cycle.upper()}_W"] = w
    if not vals:
        raise SystemExit("gen_recipe: no cycles found")
    return vals


vals = parse()
print("recipe:", " ".join(f"{k}={v}" for k, v in vals.items()))

try:
    Import("env")
    env.Append(CPPDEFINES=list(vals.items()))
except NameError:
    print("(standalone - no defines emitted)", file=sys.stderr)