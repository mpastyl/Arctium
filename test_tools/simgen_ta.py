#!/usr/bin/env python2.7
import sys
import argparse
import traceback

ap = argparse.ArgumentParser(description='Simulation generator')
ap.add_argument('--basepath', required=False, default="../..",
                   help='Base path')
ap.add_argument('-c', '--config', required=False, default="params.py",
                   help='Configuration python file')

args = ap.parse_args()

import os
from collections import namedtuple, OrderedDict
from shutil import copy, rmtree
import traceback
import subprocess
import itertools
import re
import random

basepath = args.basepath
#apppath = os.path.join(basepath, "apps", "glossy-test")
#apppath = os.path.join(basepath, "apps", "ta")
apppath = os.path.join(basepath, "apps", "crystal")
sys.path += [".", os.path.join(basepath,"test_tools")]
params = args.config

def rnd_unique(nodes, n):
    l = 0
    r = []
    while (l<n):
        x = random.choice(nodes)
        if x not in r:
            r.append(x)
            l += 1
    return r

def generate_table_array(nodes, num_epochs, concurrent_txs):
    tbl = []
    if seed is not None:
        random.seed(seed)

    for _ in xrange(num_epochs):
        tbl += rnd_unique(nodes, concurrent_txs)
    return "static const uint8_t sndtbl[] = {%s};"%",".join([str(x) for x in tbl])


binary_name = "crystal.sky"

def prepare_binary(simdir, nodes, num_epochs, concurrent_txs, new_env):
    env = os.environ.copy()
    env.update(new_env)

    abs_bname = os.path.join(apppath, binary_name)
    abs_ihex_name = abs_bname + ".ihex"
    abs_tbl_name = os.path.join(apppath, "sndtbl.c")
    abs_env_name = abs_bname + ".env"

    with open(abs_tbl_name, "w") as f:
        f.write(generate_table_array(nodes, num_epochs, concurrent_txs))

    pwd = os.getcwd()
    os.chdir(apppath)
    subprocess.check_call(["sh","-c","./build_simgen.sh"], env=env)
    os.chdir(pwd)
    
    try:
        os.makedirs(simdir)
    except OSError,e:
        print e

    nodelist = os.path.join(simdir, "nodelist.txt")
    with open(nodelist, "w") as f:
        for n in nodes:
            f.write("%d\n"%n)
    
    copy(abs_bname, simdir)
    copy(abs_ihex_name, simdir)
    copy(abs_env_name, simdir)
    copy(abs_tbl_name, simdir)



def mk_env(power, channel, sink, num_senders, longskip, n_empty, cca):
    cflags = [
    "-DTX_POWER=%d"%power,
    "-DRF_CHANNEL=%d"%channel,
    "-DCRYSTAL_SINK_ID=%d"%sink,
    "-DSTART_EPOCH=%d"%start_epoch,
    "-DCONCURRENT_TXS=%d"%num_senders,
    "-DNUM_ACTIVE_EPOCHS=%d"%active_epochs,
    "-DCRYSTAL_CONF_PERIOD=%f"%period,
    "-DN_TX_S=%d"%n_tx_s,
    "-DN_TX_T=%d"%n_tx_t,
    "-DN_TX_A=%d"%n_tx_a,
    "-DDUR_S_MS=%d"%dur_s,
    "-DDUR_T_MS=%d"%dur_t,
    "-DDUR_A_MS=%d"%dur_a,
    "-DCRYSTAL_SYNC_ACKS=%d"%sync_ack,
    "-DCRYSTAL_LONGSKIP=%d"%longskip,
    "-DCRYSTAL_PAYLOAD_LENGTH=%d"%payload,
    "-DCRYSTAL_SINK_MAX_EMPTY_TS=%d"%n_empty.r,
    "-DCRYSTAL_MAX_SILENT_TAS=%d"%n_empty.y,
    "-DCRYSTAL_MAX_MISSING_ACKS=%d"%n_empty.z,
    "-DCRYSTAL_SINK_MAX_NOISY_TS=%d"%n_empty.x,
    "-DCRYSTAL_USE_DYNAMIC_NEMPTY=%d"%dyn_nempty,
    "-DCCA_THRESHOLD=%d"%cca.dbm,
    "-DCCA_COUNTER_THRESHOLD=%d"%cca.counter,
    "-DCHHOP_MAPPING=CHMAP_%s"%chmap,
    "-DBOOT_CHOPPING=BOOT_%s"%boot_chop,
    "-DN_FULL_EPOCHS=%d"%full_epochs,
    ]

    if logging:
        cflags += ["-DCRYSTAL_LOGGING=1"]
    else:
        cflags += ["-DDISABLE_UART=1"]

    if testbed in ("indriya", "fbk", "twist"):
        cflags += ["-DTINYOS_SERIAL_FRAMES=1"]
    if testbed in ("indriya", "fbk", "flock", "twist"):
        cflags += ["-DTINYOS_NODE_ID=1"]
    if testbed == "indriya":
        cflags += ["-DSHORT_LOGS=1"]
    if testbed == "cooja":
        cflags += ["-DCOOJA=1"]
    if testbed in ("indriya", "fbk"):
        cflags += ["-DCRYSTAL_START_DELAY_SINK=40", "-DCRYSTAL_START_DELAY_NONSINK=20"]
    else:
        cflags += ["-DCRYSTAL_START_DELAY_SINK=0", "-DCRYSTAL_START_DELAY_NONSINK=0"]
    
    cflags = " ".join(cflags)
    new_env = {"CFLAGS":cflags}
    return new_env


glb = {}
pars = {}
execfile(params, glb, pars)

def set_defaults(dst, src):
    for k,v in src.items():
        if k not in dst:
            dst[k] = v

CcaTuple = namedtuple("CcaTuple", "dbm counter")
NemptyTuple = namedtuple("NemptyTuple", "r y z x")

defaults = {
    "period":2,
    "sync_ack":1,
    "dyn_nempty":0,
    #"n_emptys":[(2, 2, 4, 0)],
    "nodemaps":["all"],
    "ccas":[(-32, 100)],
    "payload":2,
    #"chmap":"nohop",
    #"boot_chop":"nohop",
    "logging":True,
    "seed":None,
    }

set_defaults(pars, defaults)

print "using the following params"
print pars

globals().update(pars)


print "--- Preparing simulations ------------------------"

all_nodes = set()
with open("nodelist.txt") as f:
    for l in f:
        l = l.strip()
        if l:
            all_nodes.add(int(l.strip()))


simnum = 0
for (power, channel, sink, num_senders, longskip, n_empty, cca, nodemap) in itertools.product(powers, channels, sinks, num_senderss, longskips, n_emptys, ccas, nodemaps):
    n_empty = NemptyTuple(*n_empty)
    cca = CcaTuple(*cca)
    simdir = "sink%03d_snd%02d_p%02d_c%02d_e%.2f_ns%02d_nt%02d_na%02d_ds%02d_dt%02d_da%02d_syna%d_ls%02d_pl%03d_r%02dy%02dz%02dx%02d_dyn%d_cca%d_%d_fe%02d_%s_%s_%s_B%s"%(sink, num_senders, power, channel, period, n_tx_s, n_tx_t, n_tx_a, dur_s, dur_t, dur_a, sync_ack, longskip, payload, n_empty.r, n_empty.y, n_empty.z, n_empty.x, dyn_nempty, cca.dbm, cca.counter, full_epochs, testbed, nodemap, chmap, boot_chop)
    if os.path.isdir(simdir):
        continue
    try:
        nodemap_txt = nodemap+".txt"
        if nodemap != "all" and not os.path.exists(nodemap_txt):
            raise Exception("Node map file does not exist: " + nodemap_txt)

        nodes = set(all_nodes)
        if nodemap != "all": 
            with open(nodemap_txt) as f:
                for l in f:
                    l = l.strip()
                    if l:
                        nodes.remove(int(l.strip().split()[0]))

        if sink not in nodes:
            raise Exception("Sink node doesn't exist")
        all_senders = [x for x in nodes if x!=sink]


        new_env = mk_env(power, channel, sink, num_senders, longskip, n_empty, cca)
        prepare_binary(simdir, all_senders, active_epochs, num_senders, new_env)
        if nodemap != "all":
            copy(nodemap_txt, os.path.join(simdir, "nodemap.txt"))
        
        num_nodes = len(all_senders)

        with open(os.path.join(simdir, "params_tbl.txt"), "w") as f:
            p = OrderedDict()
            p["testbed"] = testbed
            p["num_nodes"] = num_nodes
            p["active_epochs"] = active_epochs
            p["start_epoch"] = start_epoch
            p["seed"] = seed
            p["power"] = power
            p["channel"] = channel
            p["period"] = period
            p["senders"] = num_senders
            p["sink"] = sink
            p["n_tx_s"] = n_tx_s
            p["n_tx_t"] = n_tx_t
            p["n_tx_a"] = n_tx_a
            p["dur_s"] = dur_s
            p["dur_a"] = dur_a
            p["dur_t"] = dur_t
            p["sync_ack"] = sync_ack
            p["longskip"] = longskip
            p["n_empty"] = n_empty.r
            p["n_empty.y"] = n_empty.y
            p["n_empty.z"] = n_empty.z
            p["n_empty.x"] = n_empty.x
            p["nodemap"] = nodemap
            p["cca"] = cca.dbm
            p["cca_cnt"] = cca.counter
            p["payload"] = payload
            p["chmap"] = chmap
            p["boot_chop"] = boot_chop
            p["full_epochs"] = full_epochs
            header = " ".join(p.keys())
            values = " ".join([str(x) for x in p.values()])
            f.write(header)
            f.write("\n")
            f.write(values)
            f.write("\n")
        simnum += 1
    except Exception, e:
        traceback.print_exc()
        if os.path.isdir(simdir):
            rmtree(simdir)
        raise e


print "%d simulation(s) generated"%simnum
