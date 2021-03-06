#!/bin/bash

if [[ $# -ne 1 ]]
then
    echo "usage: ./baseline-chat-server-wrapper.sh port"
    exit
fi

DIAMOND_SRC="/home/nl35/research/diamond-src"
PROJECT_DIR="$DIAMOND_SRC/apps/chat/BaselineChatServer"
JAVA_BINARY="/home/nl35/research/jdk1.8.0_60/jre/bin/java"

if [ ! -d $DIAMOND_SRC ]
then
    echo "Error: DIAMOND_SRC directory does not exist"
    exit
fi

if [ ! -e "$PROJECT_DIR/bin/Main.class" ]
then
    echo "Error: BaselineChatServer has not been compiled"
    exit
fi

classpath="$PROJECT_DIR/bin:$PROJECT_DIR/libs/gson-2.3.1.jar:$PROJECT_DIR/libs/commons-pool2-2.0.jar:$PROJECT_DIR/libs/jedis-2.4.2.jar"

port=$1

$JAVA_BINARY -cp $classpath Main $port
