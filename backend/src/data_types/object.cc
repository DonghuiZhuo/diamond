
#include "storage/cloud.h"
#include "includes/data_types.h"
#include "lib/assert.h"
#include "lib/message.h"
#include "lib/timestamp.h"
#include <unordered_map>
#include <map>
#include <inttypes.h>
#include <set>

namespace diamond {

using namespace std;

static std::set<DObject*> RS;
static std::set<DObject*> WS;
static enum DConsistency globalConsistency = SEQUENTIAL_CONSISTENCY;


int
DObject::Map(DObject &addr, const string &key)
{
    addr._key = key;
   
    if (!cloudstore->IsConnected()) {
        Panic("Cannot map objects before connecting to backing store server");
    }

    return addr.Pull();
}

int
DObject::Pull(){
    string value;

    int ret = cloudstore->Read(_key, value);
    if (ret != ERR_OK) {
        return ret;
    }
    Deserialize(value);
    return 0;
}


int
DObject::Push(){
    string value;
    value = Serialize();

    int ret = cloudstore->Write(_key, value);
    if (ret != ERR_OK) {
        return ret;
    }
    return 0;
}

int
DObject::MultiMap(vector<DObject *> &objects, vector<string> &keys)  {

    if (keys.size() != objects.size()) {
        Panic("Mismatch between number of keys and DObjects");
    }

    vector<string> values;
    int ret = cloudstore->MultiGet(keys, values);
    if (ret != ERR_OK) {
        return ret;
    }

    if (keys.size() != values.size()) {
        Panic("Mismatch between number of keys and values returned by MultiGet");
    }

    for (size_t i = 0; i < keys.size(); i++) {
        string currentKey = keys.at(i);
        objects.at(i)->_key = currentKey;
        objects.at(i)->Deserialize(values.at(i));
    }

    return 0;
}


void
DObject::Lock(){
    pthread_mutex_lock(&_objectMutex);
    LockNotProtected();

    // RC code
    RS.clear();
    // Could also load the value of the current object in the background?

    pthread_mutex_unlock(&_objectMutex);
}


void
DObject::LockNotProtected(){
    int res;
    char value[50];
    int max_tries = 1000;
    int delay_ns = 1;
    int max_delay_ns = 1000 * 1000;

    int tid = getThreadID();

    if(_locked == tid){
        Panic("Current thread already holds the lock");
    }

    uint64_t lockid = getTimestamp();
    sprintf(value, "%" PRIu64 "", lockid);

    while(max_tries--){
        res = cloudstore->Write(_key + string("-lock"), string(value), WRITE_IFF_NOT_EXIST, LOCK_DURATION_MS);
        if(res == ERR_OK){
            _locked = tid;
            _lockid = lockid;
            return;
        }else if(res == ERR_NOT_PERFORMED){
            
            // Release the lock during sleep to prevent deadlocks
            pthread_mutex_unlock(&_objectMutex);
            usleep(delay_ns);
            pthread_mutex_lock(&_objectMutex);

            delay_ns = std::min(delay_ns * 2, max_delay_ns);
        }else{
            Panic("NYI");
        }
    }
    Panic("Unable to aquire lock");
}

void
DObject::ContinueLock(){
    pthread_mutex_lock(&_objectMutex);
    if(_locked != getThreadID()){
        Panic("Current thread does not hold the lock");
    }
    Panic("NYI");
    pthread_mutex_unlock(&_objectMutex);
}

void
DObject::Unlock(){

    pthread_mutex_lock(&_objectMutex);
    UnlockNotProtected();

    // RS Code
    set<DObject*>::iterator it;
    for (it = WS.begin(); it != WS.end(); it++) {
        (*it)->Push();
    }
    WS.clear();

    pthread_mutex_unlock(&_objectMutex);
}

void
DObject::UnlockNotProtected(){
    // From redlock
    string m_unlockScript = string("if redis.call('get', KEYS[1]) == ARGV[1] then return redis.call('del', KEYS[1]) else return 0 end");
    int res;
    char value[50];

    sprintf(value, "%" PRIu64 "", _lockid);
    res = cloudstore->RunOnServer(m_unlockScript, _key + string("-lock"), string(value));
    assert(res == ERR_OK);

    if(_locked != getThreadID()){
        Panic("Current thread does not hold the lock");
    }
    _locked = 0;
}


void
DObject::Signal(){
    int res;
    string value;
    string empty = "";

    pthread_mutex_lock(&_objectMutex);
    if(_locked != getThreadID()){
        Panic("Current thread does not hold the lock");
    }

    res = cloudstore->Lpop(_key + string("-lock-wait"), value, false);

    assert((res == ERR_OK) || (res == ERR_EMPTY));
    if(res == ERR_OK){
        assert(value != "");
        res = cloudstore->Rpush(_key + string("-lock-wait-") + value, empty);
        assert(res == ERR_OK);
    }

    // If we can assume _key is immutable then we can there is no need to hold the mutex till the end
    pthread_mutex_unlock(&_objectMutex);
}

void
DObject::Broadcast(){
    int res;
    string value;
    string empty = "";

    pthread_mutex_lock(&_objectMutex);
    if(_locked != getThreadID()){
        Panic("Current thread does not hold the lock");
    }
    
    while(1){
        res = cloudstore->Lpop(_key + string("-lock-wait"), value, false);
        if(res != ERR_OK){
            break;
        }
        res = cloudstore->Rpush(_key + string("-lock-wait-") + value, empty);
        assert(res == ERR_OK);
    }

    // If we can assume _key is immutable then we can there is no need to hold the mutex till the end
    pthread_mutex_unlock(&_objectMutex);
}


void
DObject::Wait(){
    int res;
    char lockid[50];
    string value;

    pthread_mutex_lock(&_objectMutex);
    if(_locked != getThreadID()){
        Panic("Current thread does not hold the lock");
    }


    sprintf(lockid, "%" PRIu64 "", _lockid);

    // A. Unlock & Sleep
    res = cloudstore->Rpush(_key + string("-lock-wait"), string(lockid));
    assert(res == ERR_OK);
    UnlockNotProtected();
    pthread_mutex_unlock(&_objectMutex);

    res = cloudstore->Lpop(_key + string("-lock-wait-") + string(lockid), value, true);
    
    pthread_mutex_lock(&_objectMutex);
    assert(res == ERR_OK);


    // B. Reaquire lock
    LockNotProtected();

    pthread_mutex_unlock(&_objectMutex);
}


void SetGlobalConsistency(enum DConsistency dc)
{
    globalConsistency = dc;
}


} // namespace diamond


