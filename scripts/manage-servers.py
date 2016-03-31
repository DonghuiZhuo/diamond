#!/usr/bin/python

import argparse
import glob
import os
import re
import time
import sys

WORKING_DIR = "/scratch/nl35"
BUILD_DIR = "../platform/build"

debug = True

parser = argparse.ArgumentParser(description='Launch servers.')
parser.add_argument('action', choices=['start', 'kill'], help='the action to take')
parser.add_argument('config_prefix', help='the config file prefix')
args = parser.parse_args()

frontendConfigPath = args.config_prefix + ".frontend.config"
frontendExecutablePath = BUILD_DIR + "/frontserver"
backendExecutablePath = BUILD_DIR + "/storeserver"
tssConfigPath = args.config_prefix + ".tss.config"
tssExecutablePath = BUILD_DIR + "/tss"

remoteFrontendConfigPath = WORKING_DIR + "/diamond.frontend.config"
remoteFrontendExecutablePath = WORKING_DIR + "/frontserver"
remoteFrontendOutputPath = WORKING_DIR + "/output.frontend.txt"
remoteBackendExecutablePath = WORKING_DIR + "/storeserver"
remoteTssConfigPath = WORKING_DIR + "/diamond.tss.config"
remoteTssExecutablePath = WORKING_DIR + "/tss"

setVarsCmd = "LD_LIBRARY_PATH=" + repr(WORKING_DIR)
debugCmd = ""
if debug:
    debugCmd = "DEBUG=all"

# find number of shards
files = glob.glob(args.config_prefix + "*.config")
maxShardNum = -1
for filename in files:
    match = re.match(args.config_prefix + "(\d+)\.config", filename)
    if match:
        shardNum = int(match.group(1))
        if maxShardNum < shardNum:
            maxShardNum = shardNum
numShards = maxShardNum + 1

print("Detected " + repr(numShards) + " shards")
if args.action == 'start':
    if debug:
        print("Enabling debug output")
    print("Starting servers...")
elif args.action == 'kill':
    print("Killing servers...")

# launch frontend server
frontendConfig = open(frontendConfigPath, 'r')
for line in frontendConfig:
    match = re.match("replica\s+([\w\.-]+):(\d)", line)
    if match:
        hostname = match.group(1)
        if args.action == 'start':
            os.system("rsync " + frontendConfigPath + " " + hostname + ":" + remoteFrontendConfigPath)
            os.system("rsync " + frontendExecutablePath + " " + hostname + ":" + remoteFrontendExecutablePath)
            os.system("rsync " + tssConfigPath + " " + hostname + ":" + remoteTssConfigPath)
            for shardNum in range(0, numShards):
                backendConfigPath = args.config_prefix + repr(shardNum) + ".config"
                remoteBackendConfigPath = WORKING_DIR + "/diamond" + repr(shardNum) + ".config"
                os.system("rsync " + backendConfigPath + " " + hostname + ":" + remoteBackendConfigPath)
            remoteBackendConfigPrefix = WORKING_DIR + "/diamond"
            os.system("ssh -f " + hostname + " '" + debugCmd + " " + setVarsCmd + " " + remoteFrontendExecutablePath + " -c " + remoteFrontendConfigPath + " -b " + remoteBackendConfigPrefix + " -N " + repr(numShards) +  " > " + remoteFrontendOutputPath + " 2>&1'");
        elif args.action == 'kill':
            os.system("ssh " + hostname + " 'pkill -9 -f " + remoteFrontendConfigPath + "'");



# launch backend servers
for shardNum in range(0, numShards):
    backendConfigPath = args.config_prefix + repr(shardNum) + ".config"
    remoteBackendConfigPath = WORKING_DIR + "/diamond" + repr(shardNum) + ".config"
    backendConfig = open(backendConfigPath, 'r')
    replicaNum = 0
    for line in backendConfig:
        match = re.match("replica\s+([\w\.-]+):(\d)", line)
        if match:
            hostname = match.group(1)
            remoteBackendOutputPath = WORKING_DIR + "/output.backend." + repr(shardNum) + "." + repr(replicaNum) + ".txt"
            if args.action == 'start':
                os.system("rsync " + backendConfigPath + " " + hostname + ":" + remoteBackendConfigPath)
                os.system("rsync " + backendExecutablePath + " " + hostname + ":" + remoteBackendExecutablePath)
                os.system("ssh -f " + hostname + " '" + debugCmd + " " + setVarsCmd + " " + remoteBackendExecutablePath + " -c " + remoteBackendConfigPath + " -i " + repr(replicaNum) + " > " + remoteBackendOutputPath + " 2>&1'");
            elif args.action == 'kill':
                os.system("ssh " + hostname + " 'pkill -9 -f " + remoteBackendConfigPath + "'");
            replicaNum = replicaNum + 1
    backendConfig.close()

# launch tss servers
tssConfig = open(tssConfigPath, 'r')
replicaNum = 0
for line in tssConfig:
    match = re.match("replica\s+([\w\.-]+):(\d)", line)
    if match:
        hostname = match.group(1)
        remoteTssOutputPath = WORKING_DIR + "/output.tss." + repr(replicaNum) + ".txt"
        if args.action == 'start':
            os.system("rsync " + tssConfigPath + " " + hostname + ":" + remoteTssConfigPath)
            os.system("rsync " + tssExecutablePath + " " + hostname + ":" + remoteTssExecutablePath)
            os.system("ssh -f " + hostname + " '" + debugCmd + " " + setVarsCmd + " " + remoteTssExecutablePath + " -c " + remoteTssConfigPath + " -i " + repr(replicaNum) + " > " + remoteTssOutputPath + " 2>&1'");
        elif args.action == 'kill':
            os.system("ssh " + hostname + " 'pkill -9 -f " + remoteTssConfigPath + "'");
        replicaNum = replicaNum + 1
tssConfig.close()
