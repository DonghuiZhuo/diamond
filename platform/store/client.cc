// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
// vim: set ts=4 sw=4:
/***********************************************************************
 *
 * store/strongstore/client.cc:
 *   Client to transactional storage system with strong consistency
 *
 * Copyright 2015 Irene Zhang <iyzhang@cs.washington.edu>
 *                Naveen Kr. Sharma <nksharma@cs.washington.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#include "client.h"
#include "store/common/frontend/asynccacheclient.h"

using namespace std;

namespace strongstore {

Client::Client(const string configPath,
               const int nShards,
	       const int closestReplica,
               Transport *transport)
    : transport(transport)
{
    // Initialize all state here;
    client_id = 0;
    while (client_id == 0) {
        random_device rd;
        mt19937_64 gen(rd());
        uniform_int_distribution<uint64_t> dis;
        client_id = dis(gen);
    }

    nshards = nShards;
    cclient.reserve(nshards);

    Debug("Initializing Diamond Store client with id [%lu]", client_id);

    /* Start a client for time stamp server. */
    string tssConfigPath = configPath + ".tss.config";
    ifstream tssConfigStream(tssConfigPath);
    if (tssConfigStream.fail()) {
	fprintf(stderr, "unable to read configuration file: %s\n",
		tssConfigPath.c_str());
    }
    replication::ReplicaConfig tssConfig(tssConfigStream);
    tss = new replication::VRClient(tssConfig, transport);
    
    /* Start a client for each shard. */
    for (int i = 0; i < nShards; i++) {
        string shardConfigPath =
            configPath + to_string(i) + ".config";
        ShardClient *sclient =
            new ShardClient(shardConfigPath,
                            transport,
                            client_id, i,
                            closestReplica);
        cclient.push_back(new AsyncCacheClient(sclient));
    }

    Debug("Diamond Store client [%lu] created!", client_id);
}

Client::~Client()
{
    delete tss;
    for (auto b : cclient) {
        delete b;
    }
}

void
Client::SetPublish(publish_handler_t publish) {
    Debug("Set message handler %u", cclient.size());
    for (auto client : cclient) {
        Debug("Set message handler");
        client->SetPublish(publish);
    }
}
    
void
Client::MultiGet(const uint64_t tid,
		 const set<string> &keys,
                 callback_t callback,
                 const Timestamp timestamp)
{
    map<int, set<string>> participants;
    
    for (auto &key : keys) {
        int i = key_to_shard(key, nshards);
        participants[i].insert(key);
    }

    vector<int> *results = new vector<int>();
    map<string, Version> *values = new map<string, Version>();
    callback_t cb = bind(&Client::MultiGetCallback,
                         this,
                         callback,
                         participants.size(),
                         results,
                         values,
                         placeholders::_1);
    for (auto &p : participants) {
        // Send the GET operation to appropriate shard.
	cclient[p.first]->MultiGet(tid, p.second, 
				   cb, timestamp);
    }
}
    
void
Client::MultiGetCallback(callback_t callback,
			 size_t total,
			 vector<int> *results,
                         map<string, Version> *values,
			 Promise &promise)
{
    Debug("MultiGetCall back"); 
    int ret = promise.GetReply();
    results->push_back(ret);
    if (ret == REPLY_OK) {
        for (auto &v : promise.GetValues()) {
            (*values)[v.first] = v.second;
        }
    }
                              
    if (results->size() == total) {
	Promise pp;
	pp.Reply(REPLY_OK, *values);
        delete results;
        delete values;
	callback(pp);
    }
}

void
Client::PrepareInternal(const uint64_t tid,
			const map<int, Transaction> &participants,
			callback_t callback)
{
    // 1. Send commit-prepare to all shards.
    Debug("PREPARE Transaction");
    vector<int> *results = new vector<int>();
    uint64_t *ts = new uint64_t();
    *ts = 0;
    callback_t cb =
        bind(&Client::PrepareCallback,
             this, callback,
             participants.size(),
             results, ts, false,
             placeholders::_1);

    for (auto &p : participants) {
        Debug("Sending prepare to shard [%d]", p.first);
        cclient[p.first]->Prepare(tid, cb,
                                  p.second);
    }

    // In the meantime ... go get a timestamp for OCC
    transport->Timer(0, [=]() {
            Debug("Sending request to TimeStampServer");
            function<void (const string &, const string &)> cb =
                bind(&Client::tssCallback,
                     this, callback,
                     participants.size(),
                     results, ts,
                     placeholders::_1,
                     placeholders::_2);
            tss->Invoke("", cb);});
}

void
Client::tssCallback(callback_t callback,
		    size_t total,
		    vector<int> *results,
		    uint64_t *ts,
		    const string &request,
		    const string &reply)
{
    Debug("tsscallback back"); 

    Promise p;
    p.Reply(REPLY_OK);
    *ts = stol(reply, NULL, 10);
    PrepareCallback(callback,
                    total,
                    results,
                    ts,
                    true,
                    p);
}
		
/* Attempts to commit the ongoing transaction. */
void
Client::Commit(const uint64_t tid,
	       callback_t callback,
	       const Transaction &txn)
{
    // Implementing 2 Phase Commit
    map<int, Transaction> participants;

    // split up the transaction across shards
    for (auto &r : txn.GetReadSet()) {
        int i = key_to_shard(r.first, nshards);
        if (participants.find(i) == participants.end()) {
            participants[i] =
		Transaction(txn.IsolationMode(),
			    txn.GetTimestamp());
        }
        participants[i].AddReadSet(r.first, r.second);
    }

    for (auto &w : txn.GetWriteSet()) {
        int i = key_to_shard(w.first, nshards);
        if (participants.find(i) == participants.end()) {
            participants[i] =
		Transaction(txn.IsolationMode(),
			    txn.GetTimestamp());
        }
        participants[i].AddWriteSet(w.first, w.second);
    }

    for (auto &inc : txn.GetIncrementSet()) {
        int i = key_to_shard(inc.first, nshards);
        if (participants.find(i) == participants.end()) {
            participants[i] =
		Transaction(txn.IsolationMode(),
			    txn.GetTimestamp());
        }
        participants[i].AddIncrementSet(inc.first,
					inc.second);
    }

    // Do two phase commit for linearizable and SI
    callback_t cb =
	bind(&Client::CommitCallback,
	     this,
	     tid,
	     callback,
	     participants,
	     placeholders::_1);
    PrepareInternal(tid, participants, cb);
}

void
Client::PrepareCallback(callback_t callback,
                        const size_t total,
                        vector<int> *results,
                        const uint64_t *ts,
                        const bool isTSS,
                        Promise &promise)
{
    if (!isTSS) results->push_back(promise.GetReply());
    Debug("Prepare callback size %lu", results->size()); 
    // check whether we're done
    if (results->size() == total && *ts > 0) {
        int ret = REPLY_OK;
        for (auto &r : *results) {
            // check what the reply was
            if (r != REPLY_OK) {
                ret = r;
            }
        }

        Promise pp;
        pp.Reply(ret, *ts);
        delete results;
        delete ts;
        // call commit callback
        callback(pp);
    }

}

void
Client::CommitCallback(uint64_t tid,
		       callback_t callback,
		       map<int, Transaction> participants,
		       Promise &promise) {

    if (promise.GetReply() == REPLY_OK) {
	// Send commits
	Debug("COMMIT Transaction at [%lu]",
	      promise.GetTimestamp());
	for (auto &p : participants) {
	    Transaction &txn2 = p.second;
	    if (txn2.IsolationMode() == LINEARIZABLE ||
		txn2.IsolationMode() == SNAPSHOT_ISOLATION) {
		Debug("Sending commit to shard [%d]",
		      p.first);
		txn2.SetTimestamp(promise.GetTimestamp());
		cclient[p.first]->Commit(tid, NULL, txn2);
	    }
	}
    } else {
	AbortInternal(tid, participants);
    }
    callback(promise);
}    

void
Client::AbortInternal(const uint64_t tid,
		      const map<int, Transaction> &participants) {
    for (auto &p : participants) {
	if (p.second.IsolationMode() == LINEARIZABLE ||
	    p.second.IsolationMode() == SNAPSHOT_ISOLATION) {
	    cclient[p.first]->Abort(tid, [] (Promise &promise) {});
	}
    }
}    

/* Aborts the ongoing transaction. */
void
Client::Abort(const uint64_t tid,
              callback_t callback)
{
    // Ignore external abort calls at this level
    Panic("ABORT Transaction");
}

    
/* Return statistics of most recent transaction. */
vector<int>
Client::Stats()
{
    vector<int> v;
    return v;
}

/* Takes a key and number of shards; returns shard corresponding to key. */
uint64_t
Client::key_to_shard(const string &key, const uint64_t nshards)
{
    uint64_t hash = 5381;
    const char* str = key.c_str();
    for (unsigned int i = 0; i < key.length(); i++) {
        hash = ((hash << 5) + hash) + (uint64_t)str[i];
    }

    return (hash % nshards);
}

void
Client::Subscribe(const uint64_t reactive_id,
                  const set<string> &keys,
                  const Timestamp timestamp,
                  callback_t callback) {
    map<int, set<string> > participants;

    for (auto &key : keys) {
        int i = key_to_shard(key, nshards);
        participants[i].insert(key);
    }

    ASSERT(participants.size() > 0);

    for (auto &p : participants) {
        cclient[p.first]->Subscribe(reactive_id,
                                    p.second,
                                    timestamp,
                                    callback);
    }
}

void
Client::Unsubscribe(const uint64_t reactive_id,
                    const set<string> &keys,
                    callback_t callback) {
    map<int, set<string> > participants;

    for (auto &key : keys) {
        int i = key_to_shard(key, nshards);
        participants[i].insert(key);
    }

    ASSERT(participants.size() > 0);

    for (auto &p : participants) {
        cclient[p.first]->Unsubscribe(reactive_id,
                                      p.second,
                                      callback);
    }
}

void
Client::Ack(const uint64_t reactive_id,
            const std::set<std::string> &keys,
            const Timestamp timestamp,
            callback_t callback)
{
    map<int, set<string> > participants;

    for (auto &key : keys) {
        int i = key_to_shard(key, nshards);
        participants[i].insert(key);
    }

    ASSERT(participants.size() > 0);
    
    for (auto &p : participants) {
        cclient[p.first]->Ack(reactive_id,
                              p.second,
                              timestamp,
                              callback);
    }
}
    

} // namespace strongstore
