#!/usr/bin/python

import os
import random
import re
import redis
import subprocess
import sys

if "SRC_HOST" not in os.environ:
    print("Error: environment variable SRC_HOST is not set")
    sys.exit()
if "DATA_REDIS_PORT" not in os.environ:
    print("Error: environment variable DATA_REDIS_PORT is not set")
    sys.exit()

SRC_HOST = os.environ["SRC_HOST"]
DATA_REDIS_PORT = os.environ["DATA_REDIS_PORT"]

workingDir = "."

configPrefix = ""
numFrontends = 0
keyFile = ""
numKeys = 0

processes = []
outputFiles = []

def addCommonArgs(parser):
    parser.add_argument('--config', required=True, help='config file prefix (path on SRC_HOST)')
    parser.add_argument('--numfrontends', type=int, default=1, help='number of frontend servers to use')
    parser.add_argument('--keys', default="diamond-src/scripts/experiments/keys.txt", help='keys file to use (path on SRC_HOST)')
    parser.add_argument('--numkeys', type=int, default=100000, help='number of keys to read from keys file')

def handleCommonArgs(args):
    global configPrefix
    global numFrontends
    global keyFile
    global numKeys
    configPrefix = args.config
    numFrontends = args.numfrontends
    keyFile = args.keys
    numKeys = args.numkeys

def getWorkingDir():
    return workingDir

def getNumFrontends():
    return numFrontends

def copyCommonFiles():
    copyFromSrcHost("diamond-src/platform/build/libdiamond.so");
    copyConfigFiles()
    copyFromSrcHostWithName(keyFile, "keys.txt")

def copyConfigFiles():
    for i in range(0, numFrontends):
        configFile = configPrefix + ".frontend" + repr(i)
        copyFromSrcHostWithName(configPrefix + ".frontend" + repr(i) + ".config", "diamond.frontend" + repr(i) + ".config")

def copyFromSrcHost(filename):
    os.system("rsync " + SRC_HOST + ":" + filename + " " + workingDir)

def copyFromSrcHostWithName(fileName, newName):
    os.system("rsync " + SRC_HOST + ":" + fileName + " " + workingDir + "/" + newName)

# getCommandFunc must be a function with arguments (workingDir, configFile, keyFile) that
# returns the the shell command to execute the client
def runProcesses(getCommandFunc, numClients):
    sys.stderr.write("Running clients...\n")

    for i in range(0, numClients):
        outputFile = "output-" + repr(random.randint(0, sys.maxint))
        configFile = "diamond.frontend" + repr(i % numFrontends)
        cmd = getCommandFunc(workingDir, configFile, "keys.txt", numKeys) + " > " + workingDir + "/" + outputFile
        processes.append(subprocess.Popen(cmd, shell=True))
        outputFiles.append(outputFile)

    for process in processes:
        process.wait()

    sys.stderr.write("Finished running clients\n")
    success = True
    for process in processes:
        if process.returncode:
            success = False
    if not success:
        sys.stderr.write("Error: some clients returned nonzero return codes\n")


def processOutput(processOutputFunc):
    for outputFileName in outputFiles:
        fullPath = os.path.expanduser(workingDir) + "/" + outputFileName
        processOutputFunc(fullPath)
        subprocess.call(["rm", fullPath]);
        sys.stderr.write("Finished processing output file %s\n" % outputFileName)

def putDataInRedis(outputFileName):
    r = redis.StrictRedis(host=SRC_HOST, port=DATA_REDIS_PORT)
    r.incr("clients")
    outputFile = open(outputFileName, 'r')
    for line in outputFile:
        match = re.match("^(\d+)\s+(\d+)\s+(\d+)", line)
        if match:
            startTime = match.group(1)
            endTime = match.group(2)
            committed = match.group(3)
            txnNum = random.randint(0, sys.maxint)
            txnKey = "txn-" + repr(txnNum)
            mapping = dict()
            mapping['start-time'] = startTime
            mapping['end-time'] = endTime
            mapping['committed'] = committed
            r.hmset(txnKey, mapping)
            r.lpush("txns", txnKey)

def putRetwisDataInRedis(outputFileName):
    r = redis.StrictRedis(host=SRC_HOST, port=DATA_REDIS_PORT)
    r.incr("clients")
    outfile = open(outputFileName, 'r')
    for line in outfile:
        match = re.match("^\d+\s+([\d\.]+)\s+([\d\.]+)\s+\d+\s+(\d+)\s+(\d+)", line)
        if match:
            startTime = int(float(match.group(1)) * 1000)
            endTime = int(float(match.group(2)) * 1000)
            committed = int(match.group(3))
            txnType = int(match.group(4))
            txnNum = random.randint(0, sys.maxint)
            txnKey = "txn-" + repr(txnNum)
            mapping = dict()
            mapping['start-time'] = startTime
            mapping['end-time'] = endTime
            mapping['committed'] = committed
            mapping['type'] = txnType
            r.hmset(txnKey, mapping)
            r.lpush("txns", txnKey)

def putGameDataInRedis(outputFileName):
    import random
    import redis
    import re
    import sys
    r = redis.StrictRedis(host=SRC_HOST, port=DATA_REDIS_PORT)
    r.incr("clients")
    turnCount = 0
    outfile = open(outputFileName, 'r')
    for line in outfile:
        match = re.match("(\d+)\s+(\d+)", line)
        if match:
            prevEndTime = int(match.group(1))
            endTime = int(match.group(2))
            txnNum = random.randint(0, sys.maxint)
            txnKey = "txn-" + repr(txnNum)
            mapping = dict()
            mapping['prev-end-time'] = prevEndTime
            mapping['end-time'] = endTime
            r.hmset(txnKey, mapping)
            r.lpush("txns", txnKey)
            turnCount += 1
    if turnCount >= 50:
        r.incr("activeplayers")
