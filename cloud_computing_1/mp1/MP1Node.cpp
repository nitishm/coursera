/**********************************
 * FILE NAME: MP1Node.cpp
 *
 * DESCRIPTION: Membership protocol run by this Node.
 *              Definition of MP1Node class functions.
 **********************************/

#include "MP1Node.h"

/*
 * Note: You can change/add any functions in MP1Node.{h,cpp}
 */

/**
 * Overloaded Constructor of the MP1Node class
 * You can add new members to the class if you think it
 * is necessary for your logic to work
 */
MP1Node::MP1Node(Member *member, Params *params, EmulNet *emul, Log *log, Address *address) {
    for( int i = 0; i < 6; i++ ) {
        NULLADDR[i] = 0;
    }
    this->memberNode = member;
    this->emulNet = emul;
    this->log = log;
    this->par = params;
    this->memberNode->addr = *address;
}

/**
 * Destructor of the MP1Node class
 */
MP1Node::~MP1Node() {}

/**
 * FUNCTION NAME: recvLoop
 *
 * DESCRIPTION: This function receives message from the network and pushes into the queue
 *              This function is called by a node to receive messages currently waiting for it
 */
int MP1Node::recvLoop() {
    if ( memberNode->bFailed ) {
        return false;
    }
    else {
        return emulNet->ENrecv(&(memberNode->addr), enqueueWrapper, NULL, 1, &(memberNode->mp1q));
    }
}

/**
 * FUNCTION NAME: enqueueWrapper
 *
 * DESCRIPTION: Enqueue the message from Emulnet into the queue
 */
int MP1Node::enqueueWrapper(void *env, char *buff, int size) {
    Queue q;
    return q.enqueue((queue<q_elt> *)env, (void *)buff, size);
}

/**
 * FUNCTION NAME: nodeStart
 *
 * DESCRIPTION: This function bootstraps the node
 *              All initializations routines for a member.
 *              Called by the application layer.
 */
void MP1Node::nodeStart(char *servaddrstr, short servport) {
    Address joinaddr;
    joinaddr = getJoinAddress();

    // Self booting routines
    if( initThisNode(&joinaddr) == -1 ) {
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "init_thisnode failed. Exit.");
#endif
        exit(1);
    }

    if( !introduceSelfToGroup(&joinaddr) ) {
        finishUpThisNode();
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Unable to join self to group. Exiting.");
#endif
        exit(1);
    }

    return;
}

/**
 * FUNCTION NAME: initThisNode
 *
 * DESCRIPTION: Find out who I am and start up
 */
int MP1Node::initThisNode(Address *joinaddr) {
    /*
     * This function is partially implemented and may require changes
     */
    int id = *(int*)(&memberNode->addr.addr);
    int port = *(short*)(&memberNode->addr.addr[4]);

    memberNode->bFailed = false;
    memberNode->inited = true;
    memberNode->inGroup = false;
    // node is up!
    memberNode->nnb = 0;
    memberNode->heartbeat = 0;
    memberNode->pingCounter = TFAIL;
    memberNode->timeOutCounter = -1;
    initMemberListTable(memberNode);

    return 0;
}

/**
 * FUNCTION NAME: introduceSelfToGroup
 *
 * DESCRIPTION: Join the distributed system
 */
int MP1Node::introduceSelfToGroup(Address *joinaddr) {
#ifdef DEBUGLOG
    static char s[1024];
#endif

    if ( 0 == memcmp((char *)&(memberNode->addr.addr), (char *)&(joinaddr->addr), sizeof(memberNode->addr.addr))) 
    {
        // I am the group booter (first process to join the group). Boot up the group
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Starting up group...");
#endif
        memberNode->inGroup = true;
    }
    else 
    {
        size_t msgsize = sizeof(MessageHdr) + sizeof(memberNode->addr);
        void *sendBuf = (void *)malloc(msgsize);

        serialize(sendBuf, msgsize, JOINREQ);

#ifdef DEBUGLOG
        sprintf(s, "Trying to join...");
        log->LOG(&memberNode->addr, s);
#endif

        // send JOINREQ message to introducer member
        emulNet->ENsend(&memberNode->addr, joinaddr, (char *)sendBuf, msgsize);

        free(sendBuf);
    }

    return 1;

}

/**
 * FUNCTION NAME: finishUpThisNode
 *
 * DESCRIPTION: Wind up this node and clean up state
 */
int MP1Node::finishUpThisNode(){
   /*
    * Your code goes here
    */
    vector<MemberListEntry>::iterator it = memberNode->memberList.begin();
    for (it = memberNode->memberList.begin(); it != memberNode->memberList.end() ; ++it)
    {
        printf("[%d] ID %d HB %ld TS %ld FAILED %d\n", *(int *)&memberNode->addr.addr[0], it->getid(), it->getheartbeat(), it->gettimestamp(), (int)it->getfailed());
    }
    return 1;
}

/**
 * FUNCTION NAME: nodeLoop
 *
 * DESCRIPTION: Executed periodically at each member
 *              Check your messages in queue and perform membership protocol duties
 */
void MP1Node::nodeLoop() {
    if (memberNode->bFailed) {
        return;
    }

    // Check my messages
    checkMessages();

    // Wait until you're in the group...
    if( !memberNode->inGroup ) {
        return;
    }

    // ...then jump in and share your responsibilites!
    nodeLoopOps();

    return;
}

/**
 * FUNCTION NAME: checkMessages
 *
 * DESCRIPTION: Check messages in the queue and call the respective message handler
 */
void MP1Node::checkMessages() {
    void *ptr;
    int size;

    // Pop waiting messages from memberNode's mp1q
    while ( !memberNode->mp1q.empty() ) {
        ptr = memberNode->mp1q.front().elt;
        size = memberNode->mp1q.front().size;
        memberNode->mp1q.pop();
        recvCallBack((void *)memberNode, (char *)ptr, size);
    }
    return;
}

/**
 * FUNCTION NAME: nodeLoopOps
 *
 * DESCRIPTION: Check if any node hasn't responded within a timeout period and then delete
 *              the nodes
 *              Propagate your membership list
 */
void MP1Node::nodeLoopOps() {
    /*
     * Your code goes here
     */
    vector<MemberListEntry>::iterator it = memberNode->memberList.begin();
    vector<MemberListEntry>::iterator temp = memberNode->memberList.begin();
    int myId = 0;
    memcpy(&myId, &memberNode->addr.addr[0], sizeof(int));

    for (it = memberNode->memberList.begin(); it != memberNode->memberList.end() ; ++it)
    {
        // Update your own heartbeat and timestamp
        if(it->getid() == myId)
        {
            it->setheartbeat(++memberNode->heartbeat);
            it->settimestamp(par->getcurrtime());
            it->setfailed(false);
            continue;
        }

        // Check other nodes timestamp and remove stale entries
        int timeDelta = par->getcurrtime() - it->gettimestamp();

        if(timeDelta >= TFAIL)
        {
            //cout << "[" << myId << "] " << "FAILED node " << it->getid() << " at time " << par->getcurrtime() << endl;
            it->setfailed(true);

            if(timeDelta >= TREMOVE)
            {
                //cout << "[" << myId << "] " << "REMOVED node " << it->getid() << " at time " << par->getcurrtime() << endl;
                Address addr;
                addr.addr[0] = it->getid();
                addr.addr[1] = 0;
                addr.addr[2] = 0;
                addr.addr[3] = 0;
                addr.addr[4] = it->getport();
                addr.addr[5] = 0;
                log->logNodeRemove(&memberNode->addr, &addr);

                it--;
                memberNode->memberList.erase(it + 1);
            }
        }

    }

    int count = 0;
    it = memberNode->memberList.begin() + rand() % memberNode->memberList.size();
    while(it->getfailed() && it->getid() != myId && count <= 4)
    {
        it = memberNode->memberList.begin() + rand() % memberNode->memberList.size();

        size_t msgsize = sizeof(MessageHdr) + sizeof(memberNode->addr) + (sizeof(MemberListEntry) * memberNode->memberList.size());

        void *sendBuf = (void *)malloc(msgsize);

        Address addr;
        addr.addr[0] = it->getid();
        addr.addr[1] = 0;
        addr.addr[2] = 0;
        addr.addr[3] = 0;
        addr.addr[4] = it->getport();
        addr.addr[5] = 0;

        // Send update to node
        serialize(sendBuf, msgsize, UPDATE);

        emulNet->ENsend(&memberNode->addr, &addr, (char *)sendBuf, msgsize);

        free(sendBuf);

        count++;
    }

    return;
}

/**
 * FUNCTION NAME: updateHandler
 *
 * DESCRIPTION: Message handler for update messages.
                Update memberList with list from other nodes
                Update the nodes timestamp as well
 */
bool MP1Node::updateHandler(Address *addr, int id, short port, void *recv, int size)
{
    int found = 0;

    MemberListEntry *recvMemberList = (MemberListEntry *)recv;

    vector<MemberListEntry> recvList(recvMemberList, recvMemberList + size/sizeof(MemberListEntry));
    vector<MemberListEntry>::iterator it = recvList.begin();
    vector<MemberListEntry>::iterator it_curr = memberNode->memberList.begin();


    for(it_curr = memberNode->memberList.begin() ; it_curr != memberNode->memberList.end() ; ++it_curr)
    {
        if(it_curr->getid() == id)
        {
            it_curr->settimestamp(par->getcurrtime());
        }
    }

    for (it = recvList.begin(); it != recvList.end() ; ++it)
    {
        found = 0;

        for(it_curr = memberNode->memberList.begin() ; it_curr != memberNode->memberList.end() ; ++it_curr)
        {
            if(it_curr->getid() == it->getid())
            {
                if(it_curr->getheartbeat() < it->getheartbeat())
                {
                    *it_curr = *it;
                    it_curr->settimestamp(par->getcurrtime());
                    it_curr->setfailed(false);
                }

                found = 1;
                break;
            }
        }

        if(!found && !it->getfailed())
        {
            it->settimestamp(par->getcurrtime());
            it->setfailed(false);
            memberNode->memberList.insert(memberNode->memberList.begin(), *it);
            Address addr;
            addr.addr[0] = it->getid();
            addr.addr[1] = 0;
            addr.addr[2] = 0;
            addr.addr[3] = 0;
            addr.addr[4] = it->getport();
            addr.addr[5] = 0;
            log->logNodeAdd(&memberNode->addr, &addr);
        }
    }

    std::sort(memberNode->memberList.begin(), memberNode->memberList.end());

    return true;
}

/**
 * FUNCTION NAME: joinRepHandler
 *
 * DESCRIPTION: Message handler for join reply.
                Update memberList with list from introducer &
                Set memberNode to inGroup to true to start sending updates.
 */
bool MP1Node::joinRepHandler(Address *addr, int id, short port, void *recv, int size)
{
    MemberListEntry *recvMemberList = (MemberListEntry *)recv;

    vector<MemberListEntry> recvList(recvMemberList, recvMemberList + size/sizeof(MemberListEntry));
    vector<MemberListEntry>::iterator it = recvList.begin();

    int myId;
    memcpy(&myId, &memberNode->addr.addr[0], sizeof(int));

    for (it = recvList.begin(); it != recvList.end() ; ++it)
    {
        // Dont add yourself to the memberList
        if(it->getid() != myId)
        {
            it->settimestamp(par->getcurrtime());
            it->setfailed(false);
            memberNode->memberList.push_back(*it);
            Address addr;
            addr.addr[0] = it->getid();
            addr.addr[1] = 0;
            addr.addr[2] = 0;
            addr.addr[3] = 0;
            addr.addr[4] = it->getport();
            addr.addr[5] = 0;
            log->logNodeAdd(&memberNode->addr, &addr);
        }
    }

    std::sort(memberNode->memberList.begin(), memberNode->memberList.end());

    memberNode->inGroup = true;

    return true;
}

/**
 * FUNCTION NAME: joinReqHandler
 *
 * DESCRIPTION: Message handler for join request.
                Add entry to memberList and send reply to node
 */
 bool MP1Node::joinReqHandler(Address *addr, int id, short port)
 {
    memberNode->memberList.push_back(MemberListEntry(id, port, 0, par->getcurrtime(), false));
    log->logNodeAdd(&memberNode->addr, addr);

    std::sort(memberNode->memberList.begin(), memberNode->memberList.end());

    // Send reply to node
    size_t msgsize = sizeof(MessageHdr) + sizeof(memberNode->addr) + (sizeof(MemberListEntry) * memberNode->memberList.size());
    
    void *sendBuf = (void *)malloc(msgsize);

    serialize(sendBuf, msgsize, JOINREP);

    emulNet->ENsend(&memberNode->addr, addr, (char *)sendBuf, msgsize);

    free(sendBuf);

    return true;
 }

/**
 * FUNCTION NAME: serializeMemberList
 *
 * DESCRIPTION: Serialze the memberList and return total size

 */
 void MP1Node::serializeMemberList(void *buf)
 {
    MemberListEntry *entry = (MemberListEntry *)buf;
    vector<MemberListEntry>::iterator it = memberNode->memberList.begin();
    for (it = memberNode->memberList.begin(); it != memberNode->memberList.end() ; ++it)
    {
        entry->setid(it->getid());
        entry->setport(it->getport());
        entry->setheartbeat(it->getheartbeat());
        entry->settimestamp(it->gettimestamp());
        entry->setfailed(it->getfailed());
        entry++;
    }
 }

/**
 * FUNCTION NAME: recvCallBack
 *
 * DESCRIPTION: Message handler for different message types
 */
bool MP1Node::recvCallBack(void *env, char *data, int size ) {
    /*
     * Your code goes here
     */
     int id;
     short port;

     uint8_t *msgPtr = NULL;
     int msgSize = 0;

     if(env == NULL)
     {
        printf("env is NULL\n");
        return false;
     }

     if(data == NULL)
     {
        printf("data pointer is NULL\n");
        return false;
     }

     if(size < sizeof(uint8_t))
     {
        printf("Message size too small [%d]\n", size);
        return false;
     }

     msgPtr = (uint8_t *)data;
     msgSize = size;

     //Read msg header
     MessageHdr *msg = (MessageHdr *)msgPtr;
     msgPtr += sizeof(MessageHdr);
     msgSize -= sizeof(MessageHdr);

     //Read msg address
     Address *addr = (Address *)msgPtr;
     msgPtr += sizeof(Address); 
     msgSize -= sizeof(Address);
     id = addr->addr[0]; 
     port = addr->addr[4];

     switch(msg->msgType)
     {
        case JOINREQ :  
#ifdef NM_DEBUG
                        cout << "JOIN Received By :";
                        printAddress(&memberNode->addr);
                        cout << "Sent From : ";
                        printAddress(addr);
#endif // NM_DEBUG
                        joinReqHandler(addr, id, port);
                        break;
        case JOINREP :
#ifdef NM_DEBUG
                        cout << "REPLY Received By :";
                        printAddress(&memberNode->addr);
                        cout << "Sent From : ";
                        printAddress(addr);
#endif // NM_DEBUG
                        joinRepHandler(addr, id, port, (void *)msgPtr, msgSize);
                        break;
        case UPDATE  :  
#ifdef NM_DEBUG
                        cout << "UPDATE Received By : ";
                        printAddress(&memberNode->addr);
                        cout << "Sent From : ";
                        printAddress(addr);
                        cout << "Size " << msgSize << endl;
#endif // NM_DEBUG
                        updateHandler(addr, id, port, (void *)msgPtr, msgSize);
                        break;

        default      :  cout << "Received unknown message type " << msg->msgType << endl;
     }

     return true;
}

void MP1Node::serialize(void *buffer, size_t size, MsgTypes type)
{
    uint8_t *msgPtr = (uint8_t *)buffer;

    MessageHdr *msg = (MessageHdr *)msgPtr;
    msg->msgType = type;
    msgPtr += sizeof(MessageHdr);

    memcpy(msgPtr, &memberNode->addr, sizeof(memberNode->addr));
    msgPtr += sizeof(memberNode->addr);

    // Send current membership list to node
    if(type != JOINREQ)
    {
        serializeMemberList((void *)msgPtr);
        msgPtr += (sizeof(MemberListEntry) * memberNode->memberList.size());
    }

    return;
}

/**
 * FUNCTION NAME: isNullAddress
 *
 * DESCRIPTION: Function checks if the address is NULL
 */
int MP1Node::isNullAddress(Address *addr) {
    return (memcmp(addr->addr, NULLADDR, 6) == 0 ? 1 : 0);
}

/**
 * FUNCTION NAME: getJoinAddress
 *
 * DESCRIPTION: Returns the Address of the coordinator
 */
Address MP1Node::getJoinAddress() {
    Address joinaddr;

    memset(&joinaddr, 0, sizeof(Address));
    *(int *)(&joinaddr.addr) = 1;
    *(short *)(&joinaddr.addr[4]) = 0;

    return joinaddr;
}

/**
 * FUNCTION NAME: initMemberListTable
 *
 * DESCRIPTION: Initialize the membership list
 */
void MP1Node::initMemberListTable(Member *memberNode) {
    memberNode->memberList.clear();
    int id = memberNode->addr.addr[0];
    short port = memberNode->addr.addr[4];
    memberNode->memberList.push_back(MemberListEntry(id, port, memberNode->heartbeat, par->getcurrtime(), false));
    log->logNodeAdd(&memberNode->addr, &memberNode->addr);
    memberNode->myPos = memberNode->memberList.begin();
}

/**
 * FUNCTION NAME: printAddress
 *
 * DESCRIPTION: Print the Address
 */
void MP1Node::printAddress(Address *addr)
{
    printf("%d.%d.%d.%d:%d \n",  addr->addr[0],addr->addr[1],addr->addr[2],
                                                       addr->addr[3], *(short*)&addr->addr[4]) ;    
}
