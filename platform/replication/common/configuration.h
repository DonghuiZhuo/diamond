// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * configuration.h:
 *   Representation of a replica group configuration, i.e. the number
 *   and list of replicas in the group
 *
 * Copyright 2013 Dan R. K. Ports  <drkp@cs.washington.edu>
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

#ifndef _REPLICATION_CONFIGURATION_H_
#define _REPLICATION_CONFIGURATION_H_

#include "lib/configuration.h"
#include "replication/common/viewstamp.h"

#include <fstream>
#include <stdbool.h>
#include <string>
#include <vector>

using std::string;

namespace replication {

class ReplicaConfig : public transport::Configuration
{
public:
    ReplicaConfig(const ReplicaConfig &c);
    ReplicaConfig(int n, int f, std::vector<transport::HostAddress> hosts);
    ReplicaConfig(std::ifstream &file);
    virtual ~ReplicaConfig();
    transport::HostAddress replica(int idx) const;
    int GetLeaderIndex(view_t view) const;
    int QuorumSize() const;
    int FastQuorumSize() const;
    bool operator==(const ReplicaConfig &other) const;
    inline bool operator!= (const ReplicaConfig &other) const {
        return !(*this == other);
    }
    
public:

    int f;                      // number of failures tolerated
};

}      // namespace replication

#endif  /* _REPLICATION_CONFIGURATION_H_ */
