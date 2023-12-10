#!/usr/bin/env python

commands = [
    ["./loh z FILE ARCH", "./loh x ARCH data/asdf", ".loh"],
    ["gzip -kf FILE -5 -S .-5.gz", "gzip -dkc ARCH", ".-5.gz"],
    ["gzip -kf FILE -9 -S .-9.gz", "gzip -dkc ARCH", ".-9.gz"],
    ["lz4 -f FILE ARCH -5", "lz4 -f ARCH data/asdf", ".-5.lz4"],
    ["lz4 -f FILE ARCH -9", "lz4 -f ARCH data/asdf", ".-9.lz4"],
    ["zstd -kf FILE -19 -o ARCH", "zstd -dkf ARCH -o data/asdf", ".-19.zst"],
    ["brotli -kf FILE -o ARCH", "brotli -dkf ARCH -o data/asdf", ".-11.br"],
]

extra_commands = {
    "blake" : [["./loh z FILE ARCH 1 1 4", "./loh x ARCH data/asdf", ".d4.loh"]],
    "photo" : [["./loh z FILE ARCH 0 1 3", "./loh x ARCH data/asdf", ".l0d3.loh"]],
}

files = [
    "data/cc0_photo.tga",
    "data/blake recorded 11.wav",
    "data/moby dick.txt",
    "data/oops all zeroes.bin",
    "data/white noise.bin",
    "data/Godot_v4.1.3-stable_win64.exe",
    "data/unifont-jp.tga",
]

outinfo = []

def formatsize(size):
    if size >= 1024:
        return str(round(size/1024)) + " KB"
    else:
        return str(size) + " Bytes"

import subprocess
import time
import os
for i in range(30000000):
    pass # do nothing for a while to spin up the cpu
for f in files:
    # cat infile into /dev/null to put it into the OS's cache
    os.system("cat \"" + f + "\" > /dev/null")
    outinfo += [[
        f,
        formatsize(os.path.getsize(f)),
        "-",
        "-"
    ]]
    cmds = commands.copy()
    for infix in extra_commands.keys():
        if infix in f:
            cmds += extra_commands[infix]
    for cmdinfo in cmds:
        outname = f + cmdinfo[2]
        times = []
        avgcount = 5
        actualcount = [0, 0]
        n = 0
        for c in cmdinfo[0:-1]:
            start = 0
            end = 0
            # one extra because we ignore the first one for cache reasons
            start = time.time()
            for i in range(avgcount):
                actualcount[n] += 1
                cmd = c.split(" ")
                cmd = [s.replace("FILE", f).replace("ARCH", outname) for s in cmd]
                if i == 0:
                    print("running: ", " ".join(cmd))
                subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if time.time() - start > 10:
                    break
            n += 1
            end = time.time()
            times += [end - start]
        outinfo += [[
            outname,
            formatsize(os.path.getsize(outname)),
            str(round(times[0]/actualcount[0]*1000.0)/1000.0) + "s",
            str(round(times[1]/actualcount[1]*1000.0)/1000.0) + "s",
        ]]
    outinfo += [["-", "-", "-", "-"]]

print("")
print("Name | Size | Compress time | Decompress time")
print("-|-|-|-")
for l in outinfo:
    print("|".join(l))
