
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

static std::set<DObject*> rcRS;
static std::set<DObject*> rcWS;
static enum DConsistency globalConsistency = SEQUENTIAL_CONSISTENCY;


static std::set<int> transationsTID; // set with the TIDs of the running transactions
static std::map<int, std::set<DObject*> > transactionsRS; // map with the RS for each transaction
static std::map<int, std::set<DObject*> > transactionsWS; // map with the WS for each transaction
static std::map<int, std::map<DObject*, string > > transactionsLocal; // map with the local values of the objects for each tx
pthread_mutex_t  _transactionMutex = PTHREAD_MUTEX_INITIALIZER; // Protects the global transaction structures


int
DObject::Map(DObject &addr, const string &key)
{
    pthread_mutex_lock(&addr._objectMutex);

    addr._key = key;
   
    if (!cloudstore->IsConnected()) {
        Panic("Cannot map objects before connecting to backing store server");
    }
    
    int res = addr.Pull();

    pthread_mutex_unlock(&addr._objectMutex);
    return res;
}

// XXX: Ensure return codes are correct
// XXX: Protect with pthread locks
// XXX: per-key vs per-dobject?

int
DObject::PullAlways(){
    string value;
    LOG_RC("PullAlways()"); 

    int ret = cloudstore->Read(_key, value);
    if (ret != ERR_OK) {
        return ret;
    }

    Deserialize(value);
    return 0;
}

// XXX: need to use locks in the readers/writers

int
DObject::Pull(){
    string value;

    // XXX: use the tx lock

    if(IsTransactionInProgress()){
        std::set<DObject*>* txRS = GetTransactionRS();
        std::set<DObject*>* txWS = GetTransactionWS();

        if((txRS->find(this) != txRS->end()) || (txWS->find(this) != txWS->end())){
            // Don't pull if the object is already in our WS or our RS

            // Use our local TX value
            string value;
            std::map<DObject*, string >* locals = GetTransactionLocals();
            value = (*locals)[this];
            Deserialize(value);
            return 0;
        }
        cloudstore->Watch(this->_key);
        txRS->insert(this);
        int res = PullAlways();

        // Add new value to our local TX view
        string value;
        value = Serialize();
        std::map<DObject*, string >* locals = GetTransactionLocals();
        (*locals)[this]=value;

        return res;

    }else if(globalConsistency == SEQUENTIAL_CONSISTENCY){
        return PullAlways();

    } else {
        // Release consistency

        if((rcRS.find(this) != rcRS.end()) || (rcWS.find(this) != rcWS.end())){
            // Don't do anything if object is in the WS or RS
            LOG_RC("Pull(): Object in rcRS or rcWS -> Returning local copy");
            return 0;
        }
        LOG_RC("Pull(): Object neither in rcRS nor in rcWS -> Calling PullAlways()");
        LOG_RC("Pull(): Adding object to rcRS"); 
        rcRS.insert(this);

        return PullAlways();
    }
}

int
DObject::PushAlways(){
    string value;
    LOG_RC("PushAlways()"); 

    value = Serialize();

    int ret = cloudstore->Write(_key, value);
    if (ret != ERR_OK) {
        return ret;
    }
    return 0;
}


int
DObject::Push(){

    if(IsTransactionInProgress()){
        // Add object to our WS
        std::set<DObject*>* txWS = GetTransactionWS();
        txWS->insert(this);

        // Add new value to our local TX view
        string value;
        value = Serialize();
        std::map<DObject*, string >* locals = GetTransactionLocals();
        (*locals)[this]=value;

        // Do not push to storage yet, wait for commit
        return 0; 

    }else if(globalConsistency == SEQUENTIAL_CONSISTENCY){
        return PushAlways();

    }else{
        LOG_RC("Push(): Adding object to rcWS");
        rcWS.insert(this);
        return 0; 
    }
}

// XXX: Thread-safety: make sure Multi-get is thread-safe
// We're not protecting against situations where the user modifies the parameters concurrently with MultiMap
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
        pthread_mutex_lock(&objects.at(i)->_objectMutex);

        string currentKey = keys.at(i);
        objects.at(i)->_key = currentKey;
        objects.at(i)->Deserialize(values.at(i));

        pthread_mutex_unlock(&objects.at(i)->_objectMutex);
    }

    return 0;
}


void
DObject::Lock(){
    pthread_mutex_lock(&_objectMutex);
    LockNotProtected();

    if(globalConsistency == RELEASE_CONSISTENCY){
        rcRS.clear();
        // Could also load the value of the current object in the background?
        LOG_RC("Lock(): Clearing rcRS");
    }

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

    if(globalConsistency == RELEASE_CONSISTENCY){
        LOG_RC("Unlock(): Pushing everything")
        set<DObject*>::iterator it;
        for (it = rcWS.begin(); it != rcWS.end(); it++) {
            (*it)->PushAlways();
        }
        rcWS.clear();
        LOG_RC("Unlock(): Clearing rcWS")
    }

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

void
DObject::SetGlobalConsistency(enum DConsistency dc)
{
    LOG_RC("SetGlobalConsistency");
    if((dc == RELEASE_CONSISTENCY) && (globalConsistency == SEQUENTIAL_CONSISTENCY)){
        // Updating from RELEASE_CONSISTENCY to SEQUENTIAL_CONSISTENCY
        set<DObject*>::iterator it;
        for (it = rcWS.begin(); it != rcWS.end(); it++) {
            (*it)->PushAlways();
        }
        rcWS.clear();
    }

    globalConsistency = dc;
}


bool
DObject::IsTransactionInProgress(void)
{
    long threadID = getThreadID();

    auto find = transationsTID.find(threadID);
    if(find != transationsTID.end()){
        return true;
    }else{
        return false;
    }
}

void
DObject::SetTransactionInProgress(bool inProgress)
{
    long threadID = getThreadID();

    auto find = transationsTID.find(threadID);
    if(find == transationsTID.end()){
        LOG_TX("change state to \"transaction in progress\"");
        assert(inProgress);
        transationsTID.insert(threadID);
    }else{
        LOG_TX("change state to \"transaction not in progress\"");
        assert(!inProgress);
        transationsTID.erase(threadID);
    }

    std::set<DObject*>* txWS = GetTransactionWS();
    std::set<DObject*>* txRS = GetTransactionRS();
    std::map<DObject*, string >* locals = GetTransactionLocals();

    txWS->clear();
    txRS->clear();
    locals->clear();
}


std::set<DObject*>*
DObject::GetTransactionRS(void){
    long tid = getThreadID();
    
    std::set<DObject*> *s;
    s = &transactionsRS[tid];
    return s;
}

std::set<DObject*>*
DObject::GetTransactionWS(void){
    long tid = getThreadID();
    
    std::set<DObject*> *s;
    s = &transactionsWS[tid];
    return s;
}

std::map<DObject*, string >*
DObject::GetTransactionLocals(void){
    long tid = getThreadID();
    
    std::map<DObject*, string >* m;
    m = &transactionsLocal[tid];
    return m;
}



// XXX: Ensure that it's ok to call the Begin, Commit, Rollback and Retry concurrently from different threads

void
DObject::TransactionBegin(void)
{
    pthread_mutex_lock(&_transactionMutex);

    SetTransactionInProgress(true);

    pthread_mutex_unlock(&_transactionMutex);

    // XXX: Prevent locks from being acquired during a Tx

}

int
DObject::TransactionCommit(void)
{
    pthread_mutex_lock(&_transactionMutex);

    LOG_TX_DUMP_RS()
    LOG_TX_DUMP_WS()

    // Begin storage transaction 
    // (the Watch commands executed earlier detect the conflicts)
    int res = cloudstore->Multi();
    assert(res == ERR_OK);

    string value;
    std::map<DObject*, string >* locals = GetTransactionLocals();
    std::set<DObject*>* txWS = GetTransactionWS();
    auto it = txWS->begin();
    for (; it != txWS->end(); it++) {
        // Push our local Tx values for all objects in our WS
        DObject *obj = *it;
        pthread_mutex_lock(&obj->_objectMutex);

        value = (*locals)[obj];
        obj->Deserialize(value);

        // Push to storage
        obj->PushAlways();

        pthread_mutex_unlock(&obj->_objectMutex);
    }

    // Try to commit the storage transaction
    res = cloudstore->Exec();

    SetTransactionInProgress(false);

    pthread_mutex_unlock(&_transactionMutex);

    if(res == ERR_EMPTY){
        // XXX: Need to revert the changes to the WS
        LOG_TX("Transaction commit failed");
        return false;
    }else{
        LOG_TX("Transaction commit succeeded");
        return true;
    }
}

void
DObject::TransactionRollback(void)
{

    pthread_mutex_lock(&_transactionMutex);

    int res = cloudstore->Unwatch(); 
    assert(res == ERR_OK); // Is this assert really necessary?

    SetTransactionInProgress(false);

    pthread_mutex_unlock(&_transactionMutex);

}

void
DObject::TransactionRetry(void)
{
    // Implement with pooling for now

    assert(0);

    pthread_mutex_lock(&_transactionMutex);
    SetTransactionInProgress(false);
    pthread_mutex_unlock(&_transactionMutex);

}

std::string
DObject::GetKey(){
    return _key;
}

} // namespace diamond


