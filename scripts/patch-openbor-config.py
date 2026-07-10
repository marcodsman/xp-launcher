#!/usr/bin/env python3
# Patch a MODERN OpenBOR (build ~6412, SDL2) Saves/<pak>.cfg (352-byte s_savedata).
# Layout verified empirically vs a real default cfg + DCurrent source:
#   usejoy    int @ 0x28
#   keys[4][13] int @ 0x34  (13 ints/player: 0x34, 0x68, 0x9c, 0xd0)
#   fullscreen int @ 0x140
#   stretch    int @ 0x144
#   (usegl @ 0x154 = 0 already; hwscale=1.0f @ 0x158)
# keys[] index order = SDID enum (openbor.h):
#   0 MOVEUP 1 MOVEDOWN 2 MOVELEFT 3 MOVERIGHT 4 ATTACK 5 ATTACK2 6 ATTACK3
#   7 ATTACK4 8 JUMP 9 SPECIAL 10 START 11 SCREENSHOT 12 ESC
# Joystick code (control.c): 600 + 1 + port*64 + inputindex   (JOY_LIST_FIRST=600, JOY_MAX_INPUTS=64)
#   button b        -> inputindex = b
#   hat h, dir      -> inputindex = NumButtons + 2*NumAxes + 4*h + dir  (0up 1right 2down 3left)
# Twin USB pad: NumButtons=12, NumAxes=4, NumHats=1 -> hat0 base inputindex = 12 + 8 = 20.
# Live pad = joystick PORT 1 (evidence from the SDL1.2 engine; 2nd adapter interface).
import sys, struct

UNBOUND = -999            # engine's "unset" sentinel (seen in default cfgs)
SC_F12  = 69              # SDL_SCANCODE_F12 (keyboard, harmless screenshot binding)
JLF, JMAX = 600, 64
NB, NA = 12, 4

def joy(port, idx):  return JLF + 1 + port*JMAX + idx
def btn(port, b):    return joy(port, b)
def hat(port, d):    return joy(port, NB + 2*NA + d)   # d: 0up 1right 2down 3left

def mapping(port):
    """13 controls in SDID order for a Twin USB pad on `port`."""
    UP, RIGHT, DOWN, LEFT = hat(port,0), hat(port,1), hat(port,2), hat(port,3)
    # PSX diamond (y=b0 top, b=b1 right, a=b2 bottom, x=b3 left):
    ATTACK  = btn(port,2)   # X (bottom)  -> primary attack
    JUMP    = btn(port,1)   # O (right)   -> jump
    SPECIAL = btn(port,3)   # [] (left)   -> special
    ATTACK2 = btn(port,0)   # /\ (top)    -> secondary attack/grab
    ATTACK3 = btn(port,4)   # L1
    ATTACK4 = btn(port,5)   # R1
    START   = btn(port,9)   # Start
    ESC     = btn(port,8)   # Select -> menu back (OpenBOR ESC only prompts, never instant-quits)
    #        MOVEUP MOVEDOWN MOVELEFT MOVERIGHT ATTACK ATTACK2 ATTACK3 ATTACK4 JUMP SPECIAL START SCREENSHOT ESC
    return [ UP,    DOWN,    LEFT,    RIGHT,    ATTACK, ATTACK2, ATTACK3, ATTACK4, JUMP, SPECIAL, START, SC_F12,   ESC ]

# Exact offsets for OpenBOR build 7533 (v7533 tag, 324-byte s_savedata):
#   usejoy @ 0x1c ; keys[4][13] @ 0x28 (52 bytes/player) ; fullscreen @ 0x120 ; stretch @ 0x124
OFF_USEJOY, OFF_KEYS, OFF_FULLSCREEN, OFF_STRETCH = 0x1c, 0x28, 0x120, 0x124

def patch(path, p1_port=1, p2_port=0):
    b = bytearray(open(path,"rb").read())
    assert len(b)==324, f"expected 324-byte v7533 cfg, got {len(b)}"
    players = [mapping(p1_port), mapping(p2_port), [UNBOUND]*13, [UNBOUND]*13]
    struct.pack_into("<i", b, OFF_USEJOY, 1)         # usejoy
    for pl,keys in enumerate(players):
        base = OFF_KEYS + pl*13*4
        for i,code in enumerate(keys):
            struct.pack_into("<i", b, base+i*4, code)
    struct.pack_into("<i", b, OFF_FULLSCREEN, 1)     # fullscreen
    struct.pack_into("<i", b, OFF_STRETCH, 0)        # stretch (0 = keep aspect, no distortion)
    open(path,"wb").write(b)
    return players

if __name__ == "__main__":
    pl = patch(sys.argv[1])
    print("patched (modern)", sys.argv[1])
    print("P1(port1):", pl[0])
    print("P2(port0):", pl[1])
    print("usejoy=1 fullscreen=1 stretch=0")
