// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
// vim: set ts=4 sw=4:
/***********************************************************************
 *
 * storage/cloud.cc:
 *   Diamond client for cloud storage
 *
 **********************************************************************/

#include "storage/cloud.h"
#include "lib/assert.h"
#include <string>

namespace diamond {

using namespace std;


// Initializing static variables of Cloud
Cloud* Cloud::_instance = NULL; 
bool Cloud::_connected = false;

std::string serverAddress = "coldwater.cs.washington.edu";
//String serverAddress = "localhost";


Cloud::Cloud()
{
    Connect(serverAddress);
}

Cloud::~Cloud()
{
 
}

Cloud*
Cloud::Instance(void)
{
    if(!_instance){
        _instance = new Cloud();
    }
    return _instance;
}

void
Cloud::SetConnected(bool con)
{
    _connected = con;
}

bool
Cloud::GetConnected()
{
    return _connected;
}

int
Cloud::Connect(const std::string &host)
{
    redisContext* redis;
    long threadID;

    Notice("Connecting...\n");
    redis = redisConnect(host.c_str(), 6379);
    Notice("Connected.\n");
    if (redis != NULL && redis->err == 0) {
        _connected = true;
        threadID = getThreadID();
        _redisContexts[threadID] = redis; 
        return ERR_OK;
    } else {
        Panic("Unable to connect");
        return ERR_UNAVAILABLE;
    }
}

bool
Cloud::IsConnected()
{
    return _connected;
}


redisContext*
Cloud::GetRedisContext()
{
    long threadID = getThreadID();
    auto find = _redisContexts.find(threadID);

    if (find != _redisContexts.end()) {
        return find->second;
    }else{
        Connect(serverAddress);

        find = _redisContexts.find(threadID);
        assert(find != _redisContexts.end());
        return find->second;
    }
}

int
Cloud::Read(const string &key, string &value)
{
    char cmd[256];
    redisReply *reply;

    if (!_connected) {
        return ERR_UNAVAILABLE;
    }

    sprintf(cmd, "GET %s", key.c_str());
    LOG_REQUEST("GET", cmd);
    reply = (redisReply *)redisCommand(GetRedisContext(),  "GET %s", key.c_str());
    LOG_REPLY("GET", reply);


    if (reply == NULL){
        Panic("reply == null");
    }

    if (reply->type == REDIS_REPLY_STRING) {
        value = string(reply->str);
        freeReplyObject(reply);
        return ERR_OK;
    }else if(reply->type == REDIS_REPLY_NIL){
        freeReplyObject(reply);
        return ERR_EMPTY;
    }
    freeReplyObject(reply);
    return ERR_OK;
}

int
Cloud::Write(const string &key, const string &value)
{
    char cmd[256];
    redisReply *reply;

    if (!_connected) {
        return ERR_UNAVAILABLE;
    }

    sprintf(cmd, "SET %s %s", key.c_str(), value.c_str());
    LOG_REQUEST("SET", cmd);
    reply = (redisReply *)redisCommand(GetRedisContext(),  "SET %s %s", key.c_str(), value.c_str());
    LOG_REPLY("SET", reply);

    if (reply == NULL) {
        Panic("reply == null");
    }

    freeReplyObject(reply);
    return ERR_OK;
}



int
Cloud::Write(const string &key, const string &value, int write_cond, long expire_ms)
{
    char cmd[256];
    redisReply *reply;

    if (!_connected) {
        return ERR_UNAVAILABLE;
    }

    std::string write_cond_option;

    if(write_cond == WRITE_ALWAYS){
        sprintf(cmd, "SET %s %s PX %ld", key.c_str(), value.c_str(), expire_ms);
        LOG_REQUEST("SET ALWAYS", cmd);
        reply = (redisReply *)redisCommand(GetRedisContext(), "SET %s %s PX %ld", 
                                    key.c_str(), value.c_str(), expire_ms);
        LOG_REPLY("SET ALWAYS", reply);
    }else{
        switch(write_cond){
            case WRITE_IFF_EXIST:
                write_cond_option = "XX";
                break;
            case WRITE_IFF_NOT_EXIST:
                write_cond_option = "NX";
                break;
            default:
                Panic("Write condition invalid");
        }
        sprintf(cmd, "SET %s %s PX %ld %s", key.c_str(), value.c_str(), expire_ms, write_cond_option.c_str());
        LOG_REQUEST("SET NX/XX", cmd);
        reply = (redisReply *)redisCommand(GetRedisContext(), "SET %s %s PX %ld %s", 
                                    key.c_str(), value.c_str(), expire_ms, write_cond_option.c_str());
        LOG_REPLY("SET NX/XX", reply);
    }


    if (reply == NULL) {
        Panic("reply == null");
    }

    if (reply->type == REDIS_REPLY_NIL){
        freeReplyObject(reply);
        return ERR_NOT_PERFORMED;
    }else{
        freeReplyObject(reply);
        return ERR_OK;
    }
}


int 
Cloud::RunOnServer(const string &script, const string &resource, const string &value)
{
    char cmd[256];
    redisReply *reply;

    if (!_connected) {
        return ERR_UNAVAILABLE;
    }

    sprintf(cmd, "EVAL %s 1 %s %s", script.c_str(), resource.c_str(), value.c_str());
    LOG_REQUEST("EVAL", cmd);
    reply = (redisReply *)redisCommand(GetRedisContext(),  "EVAL %s 1 %s %s", script.c_str(), resource.c_str(), value.c_str());
    LOG_REPLY("EVAL", reply);

    if (reply == NULL) {
        Panic("reply == null");
    }

    freeReplyObject(reply);
    return ERR_OK;
}

int
Cloud::Rpush(const string &key, const string &value)
{
    char cmd[256];
    redisReply *reply;

    if (!_connected) {
        return ERR_UNAVAILABLE;
    }

    sprintf(cmd, "RPUSH %s %s", key.c_str(), value.c_str());
    LOG_REQUEST("RPUSH", cmd);
    reply = (redisReply *)redisCommand(GetRedisContext(), "RPUSH %s %s", key.c_str(), value.c_str());
    LOG_REPLY("RPUSH", reply);

    if (reply == NULL) {
        Panic("reply == null");
    }

    freeReplyObject(reply);
    return ERR_OK;
}

int
Cloud::Lpop(const string &key, string &value, bool block)
{
    char cmd[256];
    redisReply *reply;

    if (!_connected) {
        return ERR_UNAVAILABLE;
    }


    if(block){
        sprintf(cmd, "BLPOP %s %d", key.c_str(), 0);
        LOG_REQUEST("BLPOP", cmd);
        reply = (redisReply *)redisCommand(GetRedisContext(), "BLPOP %s %d", key.c_str(), 0);
        LOG_REPLY("BLPOP", reply);
    }else{
        sprintf(cmd, "LPOP %s", key.c_str());
        LOG_REQUEST("LPOP", cmd);
        reply = (redisReply *)redisCommand(GetRedisContext(), "LPOP %s", key.c_str());
        LOG_REPLY("LPOP", reply);
    }

    if (reply == NULL) {
        Panic("reply == null");
    }

    if (reply->type == REDIS_REPLY_STRING) {
        value = string(reply->str);
        freeReplyObject(reply);
        return ERR_OK;
    }else if(reply->type == REDIS_REPLY_NIL){
        freeReplyObject(reply);
        return ERR_EMPTY;
    }
    return ERR_OK;
}

long getThreadID(){
    long tid;
    tid = syscall(SYS_gettid);
    return tid;
}

} // namespace diamond
