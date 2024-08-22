#include "htram_group.h"
//#define DEBUG 1

CkReductionMsg* msgStatsCollection(int nMsg, CkReductionMsg** rdmsgs) {
  double *msg_stats;
  msg_stats = (double*)rdmsgs[0]->getData();
//  CkPrintf("\nInside reducer: %lf, %lf, %lf, %lf", msg_stats[0], msg_stats[1], msg_stats[2], msg_stats[3]);
  for (int i = 1; i < nMsg; i++) {
    CkAssert(rdmsgs[i]->getSize() == STATS_COUNT*sizeof(double));
    if (rdmsgs[i]->getSize() != STATS_COUNT*sizeof(double)) {
      CkPrintf("Error!!! Reduction not correct. Msg size is %d\n",
          rdmsgs[i]->getSize());
      CkAbort("Incorrect Reduction size in MetaBalancer\n");
    }
    
    double* m = (double *)rdmsgs[i]->getData();
    msg_stats[TOTAL_LATENCY] += m[TOTAL_LATENCY];
    msg_stats[MAX_LATENCY] = max(m[MAX_LATENCY], msg_stats[MAX_LATENCY]);
    msg_stats[MIN_LATENCY] = min(m[MIN_LATENCY], msg_stats[MIN_LATENCY]);
    msg_stats[TOTAL_MSGS] += m[TOTAL_MSGS];
  }
  return CkReductionMsg::buildNew(rdmsgs[0]->getSize(), NULL, rdmsgs[0]->getReducer(), rdmsgs[0]);
}

/*global*/ CkReduction::reducerType msgStatsCollectionType;
/*initnode*/ void registerMsgStatsCollection(void) {
  msgStatsCollectionType = CkReduction::addReducer(msgStatsCollection, true, "msgStatsCollection");
}

void periodic_tflush(void *htram_obj, double time);

HTram::HTram(CkGroupID recv_ngid, CkGroupID src_ngid, int buffer_size, bool enable_buffer_flushing, double time_in_ms, bool ret_item, bool req, CkCallback start_cb) {
  request = req;
  // TODO: Implement variable buffer sizes and timed buffer flushing
  flush_time = time_in_ms;
//  client_gid = cgid;
  enable_flush = enable_buffer_flushing;
  msg_stats[MIN_LATENCY] = 100.0;
  agg_msg_count = 0;
  flush_msg_count = 0;
//  if(thisIndex==0) CkPrintf("\nbuf_type = %d, type %d,%d,%d,%d", buf_type, use_src_grouping, use_src_agg, use_per_destpe_agg, use_per_destnode_agg);
/*
  if(use_per_destnode_agg)
    if(thisIndex==0) CkPrintf("\nDest-node side grouping/sorting enabled (1 buffer per src-pe, per dest-node)\n");
*/
  ret_list = !ret_item;
  agg = PNs;//NNs;//PP;

  myPE = CkMyPe();
  msgBuffers = (new HTramMessage*[CkNumPes()]);

  if(thisIndex == 0) {
    if(agg == PNs) CkPrintf("Aggregation type: PNs with buffer size %d\n", BUFSIZE);
    else if(agg == NNs) CkPrintf("Aggregation type: NNs with buffer size %d and local buffer size %d\n",BUFSIZE, LOCAL_BUFSIZE);
    else if(agg == PP) CkPrintf("Aggregation type: PP with buffer size %d\n", BUFSIZE);
  }

  localMsgBuffer = new HTramMessage();

  for(int i=0;i<CkNumPes();i++) {
    msgBuffers[i] = new HTramMessage();
    std::vector<HTramMessage*> buf_vec_pe;
    overflowBuffers.push_back(buf_vec_pe);
  }


  localBuffers = new std::vector<itemT>[CkNumPes()];

  local_buf = new HTramLocalMessage*[CkNumNodes()];
  for(int i=0;i<CkNumNodes();i++)
  {
    local_buf[i] = new HTramLocalMessage();
    local_idx[i] = 0;
  }

  tot_send_count = 0;
  tot_recv_count = 0;

  nodeGrpProxy = CProxy_HTramRecv(recv_ngid);
  srcNodeGrpProxy = CProxy_HTramNodeGrp(src_ngid);

  srcNodeGrp = (HTramNodeGrp*)srcNodeGrpProxy.ckLocalBranch();
  nodeGrp = (HTramRecv*)nodeGrpProxy.ckLocalBranch();

  CkGroupID my_gid = ckGetGroupID();
//  CkPrintf("\nmy_gid = %d", my_gid);
//  if(thisIndex==0)
  nodeGrp->setTramProxy(my_gid);

  if(enable_flush)
    periodic_tflush((void *) this, flush_time);
#ifdef IDLE_FLUSH
  CkCallWhenIdle(CkIndex_HTram::idleFlush(), this);
#endif
  contribute(start_cb);
}

bool HTram::idleFlush() {
//    if(thisIndex==0) CkPrintf("\nCalling idleflush");
#ifdef IDLE_FLUSH
    tflush(true);
#endif
    return true;
}

void HTram::reset_stats(int btype, int buf_size, int agtype) {
  std::fill_n(msg_stats, STATS_COUNT, 0.0);
  msg_stats[MIN_LATENCY] = 100.0;
  std::fill_n(nodeGrp->msg_stats, STATS_COUNT, 0.0);
  nodeGrp->msg_stats[MIN_LATENCY] = 100.0;
  agg = agtype;
  int buf_count = CkNumNodes();
  if(agg == PP) buf_count = CkNumPes();
  for(int i=0;i<buf_count;i++)
    msgBuffers[i] = new HTramMessage();

  //if(thisIndex==0) CkPrintf("\nbuf_type = %d, size = %d, type = %d", buf_type, BUFSIZE, agg);
}

void HTram::avgLatency(CkCallback cb){
  return_cb = cb;
  msg_stats[TOTAL_LATENCY] /= (2*msg_stats[TOTAL_MSGS]);
//  CkPrintf("\n%lf, %lf, %lf, %lf", msg_stats[0], msg_stats[1], msg_stats[2], msg_stats[3]);
//  double avg = total_latency/total_msg_count;
//  CkCallback cb(CkReductionTarget(MetaBalancer, ReceiveMinStats),
//        thisProxy[0]);
  contribute(STATS_COUNT*sizeof(double), msg_stats, msgStatsCollectionType, cb);
//  CkPrintf("\navg = %lf", avg);
//  contribute(sizeof(double), &avg, CkReduction::sum_double, CkCallback(CkReductionTarget(HTram, printAvgLatency), thisProxy));
}

void HTramRecv::avgLatency(CkCallback cb){
  return_cb = cb;
  msg_stats[TOTAL_LATENCY] /= (2*msg_stats[TOTAL_MSGS]);
//  CkPrintf("\n%lf, %lf, %lf, %lf", msg_stats[0], msg_stats[1], msg_stats[2], msg_stats[3]);
//  double avg = total_latency/total_msg_count;
//  contribute(sizeof(double), &avg, CkReduction::sum_double, CkCallback(CkReductionTarget(HTramRecv, printAvgLatency), thisProxy));
  contribute(STATS_COUNT*sizeof(double), msg_stats, msgStatsCollectionType, cb);
}

HTram::HTram(CkGroupID cgid, CkCallback ecb){
  client_gid = cgid;
//  cb = delivercb;
  endCb = ecb;
  myPE = CkMyPe();
  localMsgBuffer = new HTramMessage();
#ifndef NODE_SRC_BUFFER
  msgBuffers = new HTramMessage*[CkNumNodes()];
  for(int i=0;i<CkNumNodes();i++)
    msgBuffers[i] = new HTramMessage();
#endif
}

void HTram::set_func_ptr(void (*func)(void*, datatype), void* obPtr) {
  cb = func;
  objPtr = obPtr;
}

void HTram::set_func_ptr_retarr(void (*func)(void*, datatype*, int), int (*func2)(void*, datatype), void (*func3)(void*), void* obPtr) {
  cb_retarr = func;
  get_dest_proc = func2;
  tram_done = func3;
  objPtr = obPtr;
}

HTram::HTram(CkMigrateMessage* msg) {}

void HTram::shareArrayOfBuckets(std::vector<datatype> *new_tram_hold, int bucket_count) {
  histo_bucket_count = bucket_count;
  tram_hold = new_tram_hold;
}

void HTram::changeThreshold(int high) {
  tram_threshold = high;
  est_total_items_in_bucket_arr = 0;
  for(int i=0;i<=high;i++)
    est_total_items_in_bucket_arr += tram_hold[i].size();
  insertBuckets(high);
}

#define FACTOR 3

int sum_of_sent(vector<int> sent) {
  int sum = 0;
  for(int i=0;i<CkNumNodes();i++) sum += sent[i];
  return sum;
}

void HTram::insertBuckets(int high) {
  est_total_items_in_bucket_arr += 8000;
  if(est_total_items_in_bucket_arr < CkNumNodes()*BUFSIZE*FACTOR)
    return;

  vector<int> sent(CkNumNodes(),0);
  for(int dest_node=0;dest_node<CkNumNodes();dest_node++) {
#ifdef THROTTLE
    if(nodeGrp->msgs_in_transit[dest_node] < _K) //TODO: fix, not atomically done
#endif
    {
      int j=0;
#ifdef THROTTLE
      for(;j<overflowBuffers[dest_node].size() && nodeGrp->msgs_in_transit[dest_node] < _K;j++)
#else
      for(;j<overflowBuffers[dest_node].size();j++)
#endif
      {
        tot_send_count += overflowBuffers[dest_node][j]->next;
        nodeGrpProxy[dest_node].receive(overflowBuffers[dest_node][j]);
        nodeGrp->msgs_in_transit[dest_node]++;
#ifndef THROTTLE
        sent[dest_node] = 1;
#endif
      }
      if(j)
        overflowBuffers[dest_node].erase(overflowBuffers[dest_node].begin(), overflowBuffers[dest_node].begin() + j);
    }
  }

  //copy from vectors in order of index into messages
  int overflowed = 0;
  for(int i=0;i<=high/*histo_bucket_count*/;i++) {
    for(int j=0;j<tram_hold[i].size();j++) {
      datatype item = tram_hold[i][j];
      int dest_proc = get_dest_proc(objPtr, item);
      if(dest_proc == -1) continue;
      int dest_node = dest_proc/CkNodeSize(0);
      HTramMessage* destMsg = msgBuffers[dest_node];
      destMsg->buffer[destMsg->next].payload = item;
      destMsg->buffer[destMsg->next].destPe = dest_proc;
      destMsg->next++;
      if(destMsg->next == BUFSIZE) {
        destMsg->srcPe = CkMyPe();
#ifdef THROTTLE
        if(nodeGrp->msgs_in_transit[dest_node] < _K)
#endif
        {
#ifdef THROTTLE
          nodeGrp->msgs_in_transit[dest_node]++;
          destMsg->ack_count = (nodeGrp->msgs_received_from[dest_node]).exchange(0);
#endif
          tot_send_count += destMsg->next;
          nodeGrpProxy[dest_node].receive(destMsg);
          sent[dest_node] = 1;
        }
#ifdef THROTTLE
        else {
          overflowed = 1;
          overflowBuffers[dest_node].push_back(destMsg);
        }
#endif
        est_total_items_in_bucket_arr -= destMsg->next;
        msgBuffers[dest_node] = new HTramMessage();
      }
    }
    tram_hold[i].clear();
    if(sum_of_sent(sent) == CkNumNodes()) break;
    if(overflowed) break;
  }
  tram_done(objPtr);
}

//one per node, message, fixed 
//Client inserts
void HTram::insertValue(datatype value, int dest_pe) {
//  CkPrintf("\nInserting on PE-%d", dest_pe);
  int destNode = dest_pe/CkNodeSize(0); //find safer way to find dest node,
  // node size is not always same
  
  if(agg == NNs) {
    int increment = 1;
    int idx = -1;
    int idx_dnode = local_idx[destNode];
    if(idx_dnode<=LOCAL_BUFSIZE-1) {
      local_buf[destNode]->buffer[idx_dnode].payload = value;
      local_buf[destNode]->buffer[idx_dnode].destPe = dest_pe;
      local_idx[destNode]++;
    }
    bool local_buf_full = false;
    if(local_idx[destNode] == LOCAL_BUFSIZE)
      local_buf_full = true;
    increment = LOCAL_BUFSIZE;
    int done_idx = -1;
    if(local_buf_full) {
  //    CkPrintf("\n[PE-%d]Copying for dest node %d, size = %d", CkMyPe(), destNode, increment);
      copyToNodeBuf(destNode, increment);
    }
  }
  else {
    HTramMessage *destMsg = msgBuffers[destNode];
    if(agg == PP)
      destMsg = msgBuffers[dest_pe];

    if(agg == PsN) {
      itemT itm = {dest_pe, value};
      localBuffers[dest_pe].push_back(itm);
    } else if (agg == PP) {//change msg type to not include destPE
      destMsg->buffer[destMsg->next].payload = value;
    } else {
      destMsg->buffer[destMsg->next].payload = value;
      destMsg->buffer[destMsg->next].destPe = dest_pe;
    }
#if 0
    if(*(destMsg->next) == 0 || *(destMsg->next) == BUFSIZE-1) {
      if(*(destMsg->next) == 0)
        destMsg->getTimer()[0] = CkWallTimer();
      else
        destMsg->getTimer()[1] = CkWallTimer();
    }
#endif

    destMsg->next++;
    if(destMsg->next == BUFSIZE) {
       agg_msg_count++;
      if(agg == PsN) {
        int sz = 0;
        for(int i=0;i<CkNodeSize(0);i++) {
          std::vector<itemT> localMsg = localBuffers[destNode*CkNodeSize(0)+i];
          std::copy(localMsg.begin(), localMsg.end(), &(destMsg->buffer[sz]));
          sz += localMsg.size();
//          destMsg->getIndex()[i] = sz;
          localBuffers[destNode*CkNodeSize(0)+i].clear();
        }
      }
//      ((envelope *)UsrToEnv(destMsg))->setUsersize(0);//destMsg->next-20)*4+32);
      int dest_idx = dest_pe;
      if(agg == PP) {
//        CkPrintf("\nmsg size = %d", *destMsg->next);
        thisProxy[dest_pe].receiveOnPE(destMsg);
      } else {
          dest_idx = destNode;
        if(agg == PsN) {
          nodeGrpProxy[destNode].receive_no_sort(destMsg);
//          nodeGrpProxy[destNode].receive_no_sort(destMsg);
        } else {
          tot_send_count += destMsg->next;
//          if(request) CkPrintf("\nSending msg from requesting htram");
//          else CkPrintf("\nSending msg from responding htram");
          nodeGrpProxy[destNode].receive(destMsg);
        }
      }
      msgBuffers[dest_idx] = new HTramMessage();
    }
  }
}

void HTram::registercb() {
  CcdCallFnAfter(periodic_tflush, (void *) this, flush_time);
}

void HTram::copyToNodeBuf(int destnode, int increment) {

// Get atomic index
  int idx = srcNodeGrp->get_idx[destnode].fetch_add(increment, std::memory_order_relaxed);
  while(idx >= BUFSIZE) {
    idx = srcNodeGrp->get_idx[destnode].fetch_add(increment, std::memory_order_relaxed);
  }
#if 0
  if(idx==0 || idx == BUFSIZE-increment)
    srcNodeGrp->msgBuffers[destnode]->getTimer()[idx/(BUFSIZE-increment)] = CkWallTimer();
#endif
// Copy data into node buffer from PE-local buffer
  int i;
  for(i=0;i<increment;i++) {
    srcNodeGrp->msgBuffers[destnode]->buffer[idx+i].payload = local_buf[destnode]->buffer[i].payload;
    srcNodeGrp->msgBuffers[destnode]->buffer[idx+i].destPe = local_buf[destnode]->buffer[i].destPe;
  }

  int done_count = srcNodeGrp->done_count[destnode].fetch_add(increment, std::memory_order_relaxed);
  if(done_count+increment == BUFSIZE) {
    agg_msg_count++;
    srcNodeGrp->msgBuffers[destnode]->next = BUFSIZE;
    
      nodeGrpProxy[destnode].receive(srcNodeGrp->msgBuffers[destnode]);
      srcNodeGrp->msgBuffers[destnode] = new HTramMessage();
    srcNodeGrp->done_count[destnode] = 0;
    srcNodeGrp->get_idx[destnode] = 0;
  }
  local_idx[destnode] = 0;
}

void HTram::enableIdleFlush() {
#ifdef IDLE_FLUSH
  CkCallWhenIdle(CkIndex_HTram::idleFlush(), this);
#endif
}
void HTram::tflush(bool idleflush) {
//    CkPrintf("\nCalling flush on PE-%d", thisIndex); fflush(stdout);
  if(agg == NNs) {
#if 1
    int flush_count = srcNodeGrp->flush_count.fetch_add(1, std::memory_order_seq_cst);
    //Send your local buffer
    for(int i=0;i<CkNumNodes();i++) {
      local_buf[i]->next = local_idx[i];
      nodeGrpProxy[i].receive_small(local_buf[i]);
      local_buf[i] = new HTramLocalMessage();
      local_idx[i] = 0;
    }
    //If you're last rank on node to flush, then flush your buffer and by setting idx to a high count, node level buffer as well
//    if(flush_count+1==CkNodeSize(0))
    {
      for(int i=0;i<CkNumNodes();i++) {
        if(srcNodeGrp->done_count[i]) {
          flush_msg_count++;
#if 1
          int idx = srcNodeGrp->get_idx[i].fetch_add(BUFSIZE, std::memory_order_relaxed);
          int done_count = srcNodeGrp->done_count[i].fetch_add(0, std::memory_order_relaxed);
          if(idx >= BUFSIZE) continue;
          while(idx!=done_count) { done_count = srcNodeGrp->done_count[i].fetch_add(0, std::memory_order_relaxed);}
#endif
  //          CkPrintf("\nCalling TFLUSH---\n");
          srcNodeGrp->msgBuffers[i]->next = srcNodeGrp->done_count[i];
          ((envelope *)UsrToEnv(srcNodeGrp->msgBuffers[i]))->setUsersize(sizeof(int)+sizeof(envelope)+sizeof(itemT)*srcNodeGrp->msgBuffers[i]->next);
 /* 
          CkPrintf("\n[PE-%d]TF-Sending out data with size = %d", thisIndex, srcNodeGrp->msgBuffers[i]->next);
          for(int j=0;j<srcNodeGrp->msgBuffers[i]->next;j++)
            CkPrintf("\nTFvalue=%d, pe=%d", srcNodeGrp->msgBuffers[i]->buffer[j].payload, srcNodeGrp->msgBuffers[i]->buffer[j].destPe);
  */
//          *(srcNodeGrp->msgBuffers[i]->getDoTimer()) = 0;
          nodeGrpProxy[i].receive(srcNodeGrp->msgBuffers[i]);
//          srcNodeGrp->msgBuffers[i] = new HTramMessageSmall();//new HTramMessage();//localMsgBuffer;
          srcNodeGrp->msgBuffers[i] = new HTramMessage();
          srcNodeGrp->done_count[i] = 0;
          srcNodeGrp->flush_count = 0;
          srcNodeGrp->get_idx[i] = 0;
        }
//          localMsgBuffer = new HTramMessage();
        
      }
    }
#endif
  }
  else {
    int buf_count = CkNumNodes();
    if(agg == PP) buf_count = CkNumPes();

    for(int i=0;i<buf_count;i++) {
//      if(msgBuffers[i]->next)
#ifdef IDLE_FLUSH
      if(!idleflush || msgBuffers[i]->next > BUFSIZE*PARTIAL_FLUSH)
#else
      if(!idleflush && msgBuffers[i]->next)
#endif
      {
        flush_msg_count++;
//        if(idleflush) CkPrintf("\n[PE-%d] flushing buf[%d] at %d", CkMyPe(), i,  msgBuffers[i]->next);
//        else CkPrintf("\nReg[PE-%d] flushing buf[%d] at %d", CkMyPe(), i, msgBuffers[i]->next);
        HTramMessage *destMsg = msgBuffers[i];
//        *destMsg->getDoTimer() = 0;
        if(agg == PsN) {
          int destNode = i;
          int sz = 0;
          for(int k=0;k<CkNodeSize(0);k++) {
            std::vector<itemT> localMsg = localBuffers[destNode*CkNodeSize(0)+k];
            std::copy(localMsg.begin(), localMsg.end(), &(destMsg->buffer[sz]));
            sz += localMsg.size();
//            destMsg->getIndex()[k] = sz;
            localBuffers[destNode*CkNodeSize(0)+k].clear();
          }
          nodeGrpProxy[i].receive_no_sort(destMsg);
        }
        else if(agg == PNs)
        {
          ((envelope *)UsrToEnv(destMsg))->setUsersize(sizeof(int)+sizeof(envelope)+sizeof(itemT)*(destMsg->next));
//          nodeGrpProxy[i].receive(destMsg); //todo - Resize only upto next
          destMsg->srcPe = CkMyPe();
          tot_send_count += destMsg->next;
#ifdef THROTTLE
          destMsg->ack_count = (nodeGrp->msgs_received_from[i]).exchange(0);
#endif
          nodeGrpProxy[i].receive(destMsg);
        } else if(agg == PP) {
          ((envelope *)UsrToEnv(destMsg))->setUsersize(sizeof(int)+sizeof(envelope)+sizeof(itemT)*destMsg->next);
//          CkPrintf("\nmsg size = %d", *destMsg->next);
          thisProxy[i].receiveOnPE(destMsg);
        }
        //msgBuffers[i] = new HTramMessageSmall();//new HTramMessage();
          msgBuffers[i] = new HTramMessage();
      }
    }
  }
  if(agg == PNs) {
    for(int dest_node=0;dest_node<CkNumNodes();dest_node++) {
      int j=0;
      for(;j<overflowBuffers[dest_node].size();j++) {
        tot_send_count += overflowBuffers[dest_node][j]->next;
        nodeGrpProxy[dest_node].receive(overflowBuffers[dest_node][j]);
#ifdef THROTTLE
        nodeGrp->msgs_in_transit[CkMyRank()*CkNumNodes()+dest_node]++;
//        CkPrintf("\n[PE-%d Sending overflow messages to node %d, in transit for the src-dest pair #[%d]", CkMyPe(), dest_node, nodeGrp->msgs_in_transit[CkMyRank()*CkNumNodes()+dest_node].load());
#endif
      }
      if(j)
        overflowBuffers[dest_node].erase(overflowBuffers[dest_node].begin(), overflowBuffers[dest_node].begin() + j);
    }

#if 1
    int filler_item_count = 0;
    for(int i=0/*tram_threshold+1*/;i<histo_bucket_count;i++) {
      for(int j=0;j<tram_hold[i].size();j++) {
        datatype item = tram_hold[i][j];
        int dest_proc = get_dest_proc(objPtr, item);
        if(dest_proc == -1) continue;
        filler_item_count++;
        int dest_node = dest_proc/CkNodeSize(0);
        HTramMessage* destMsg = msgBuffers[dest_node];
        destMsg->buffer[destMsg->next].payload = item;
        destMsg->buffer[destMsg->next].destPe = dest_proc;
        destMsg->next++;
        if(destMsg->next == BUFSIZE) {
          destMsg->srcPe = CkMyPe();
#ifdef THROTTLE
          nodeGrp->msgs_in_transit[dest_node]++;
          destMsg->ack_count = (nodeGrp->msgs_received_from[dest_node]).exchange(0);
#endif
          tot_send_count += destMsg->next;
          nodeGrpProxy[dest_node].receive(destMsg);
          msgBuffers[dest_node] = new HTramMessage();
        }
      }
      tram_hold[i].clear();
    }
    //Last filler count is 0, fix to send last set of message buffers
//    CkPrintf("\nFlush sent these items from fillers %d from buckets[%d-%d]", filler_item_count, tram_threshold+1, histo_bucket_count);
    tram_done(objPtr);
#endif
  }
}

HTramNodeGrp::HTramNodeGrp() {
  msgBuffers = new HTramMessage*[CkNumNodes()];
  for(int i=0;i<CkNumNodes();i++) {
    msgBuffers[i] = new HTramMessage();
    get_idx[i] = 0;
    done_count[i] = 0;
  }
}

HTramNodeGrp::HTramNodeGrp(CkMigrateMessage* msg) {}


HTramRecv::HTramRecv(){
#ifdef THROTTLE
  msgs_received_from = new std::atomic_int[CkNumNodes()];
  msgs_in_transit = new std::atomic_int[CkNumNodes()];
  for(int i=0;i<CkNumNodes();i++) {
    msgs_in_transit[i] = 0;
    msgs_received_from[i] = 0;
  }
#endif
  msg_stats[MIN_LATENCY] = 100.0;
}

#if 0
bool comparePayload(itemT a, itemT b)
{
    return (a.payload > b.payload);
}

bool lower(itemT a, double value) {
  return a.payload < value;
}

bool upper(itemT a, double value) {
  return a.payload > value;
}
#endif

HTramRecv::HTramRecv(CkMigrateMessage* msg) {}

//#ifdef SRC_GROUPING
void HTramRecv::receive_no_sort(HTramMessage* agg_message) {
  for(int i=CkNodeFirst(CkMyNode()); i < CkNodeFirst(CkMyNode())+CkNodeSize(0);i++) {
    HTramMessage* tmpMsg = (HTramMessage*)CkReferenceMsg(agg_message);
    _SET_USED(UsrToEnv(tmpMsg), 0);
    tram_proxy[i].receivePerPE(tmpMsg);
  }
  CkFreeMsg(agg_message);
}

  void HTram::receivePerPE(HTramMessage* msg) {
    int llimit = 0;
    int rank = CkMyRank();
    if(rank > 0) llimit = 0;//msg->getIndex()[rank-1];
    int ulimit = 0;//msg->getIndex()[rank];
    for(int i=llimit; i<ulimit;i++){
      cb(objPtr, msg->buffer[i].payload);
    }
    CkFreeMsg(msg);
  }


//#elif defined PER_DESTPE_BUFFER
void HTram::receiveOnPE(HTramMessage* msg) {
//    CkPrintf("\ntotal_latency=%lfs",total_latency);

  if(!ret_list) {
    for(int i=0;i<msg->next;i++)
      cb(objPtr, msg->buffer[i].payload);
  } else {
    datatype *buf = new datatype[msg->next];
    for(int i=0;i<msg->next;i++)
      buf[i] = msg->buffer[i].payload;
    cb_retarr(objPtr, buf, msg->next);
  }
  delete msg;
}

void HTramRecv::receive(HTramMessage* agg_message) {
#ifdef THROTTLE
  int src_node = agg_message->srcPe/CkNodeSize(0);
  msgs_received_from[src_node]++;

  if(agg_message->ack_count) {
    msgs_in_transit[src_node] -= agg_message->ack_count; //TODO: Not sure if this is done atomically
#if 0
    if(msgs_in_transit[src_node].load() < _K)
      for(int i=0;i<CkNodeSize(0);i++)
        tram_proxy[i+thisIndex*CkNodeSize(0)].insertBuckets(0,512);
#endif
//     CkPrintf("\n[PE-%d] Reducing msg_in_transit(to)[%d] from %d to %d", CkMyPe(), src_node, msgs_in_transit[k*CkNumNodes()+src_node].load()+agg_message->ack_count[k], msgs_in_transit[k*CkNumNodes()+src_node].load());
  }
#endif

  
  //broadcast to each PE and decr refcount
  //nodegroup //reference from group
  int rank0PE = CkNodeFirst(thisIndex);
  HTramNodeMessage* sorted_agg_message = new HTramNodeMessage();
  
  int sizes[PPN_COUNT] = {0};
  
  for(int i=0;i<agg_message->next;i++) {
    int rank = agg_message->buffer[i].destPe - rank0PE;
    sizes[rank]++;
  } 
  
  sorted_agg_message->offset[0] = 0;
  for(int i=1;i<CkNodeSize(0);i++)
    sorted_agg_message->offset[i] = sorted_agg_message->offset[i-1]+sizes[i-1];
    
  for(int i=0;i<agg_message->next;i++) {
    int rank = agg_message->buffer[i].destPe - rank0PE;
    sorted_agg_message->buffer[sorted_agg_message->offset[rank]++] = agg_message->buffer[i].payload;
  } 
  delete agg_message;
  
  sorted_agg_message->offset[0] = sizes[0];
  for(int i=1;i<CkNodeSize(0);i++)
    sorted_agg_message->offset[i] = sorted_agg_message->offset[i-1] + sizes[i];
    
  for(int i=CkNodeFirst(CkMyNode()); i < CkNodeFirst(CkMyNode())+CkNodeSize(0);i++) {
    HTramNodeMessage* tmpMsg = (HTramNodeMessage*)CkReferenceMsg(sorted_agg_message);
    _SET_USED(UsrToEnv(tmpMsg), 0);
    tram_proxy[i].receivePerPE(tmpMsg);
  } 
  CkFreeMsg(sorted_agg_message);
}

void HTramRecv::receive_small(HTramLocalMessage* agg_message) {
  //broadcast to each PE and decr refcount
  //nodegroup //reference from group
  int rank0PE = CkNodeFirst(thisIndex);
  HTramNodeMessage* sorted_agg_message = new HTramNodeMessage();

  int sizes[PPN_COUNT] = {0};

  for(int i=0;i<agg_message->next;i++) {
    int rank = agg_message->buffer[i].destPe - rank0PE;
//    CkPrintf("\nrank=%d, i=%d, next=%d", rank, i, agg_message->next);
    sizes[rank]++;
  }

  sorted_agg_message->offset[0] = 0;
  for(int i=1;i<CkNodeSize(0);i++)
    sorted_agg_message->offset[i] = sorted_agg_message->offset[i-1]+sizes[i-1];

  for(int i=0;i<agg_message->next;i++) {
    int rank = agg_message->buffer[i].destPe - rank0PE;
    sorted_agg_message->buffer[sorted_agg_message->offset[rank]++] = agg_message->buffer[i].payload;
  }
  delete agg_message;

  sorted_agg_message->offset[0] = sizes[0];
  for(int i=1;i<CkNodeSize(0);i++)
    sorted_agg_message->offset[i] = sorted_agg_message->offset[i-1] + sizes[i];

  for(int i=CkNodeFirst(CkMyNode()); i < CkNodeFirst(CkMyNode())+CkNodeSize(0);i++) {
    HTramNodeMessage* tmpMsg = (HTramNodeMessage*)CkReferenceMsg(sorted_agg_message);
    _SET_USED(UsrToEnv(tmpMsg), 0);
    tram_proxy[i].receivePerPE(tmpMsg);
  }
  CkFreeMsg(sorted_agg_message);
}

void HTramRecv::setTramProxy(CkGroupID tram_gid) {
  tram_proxy = CProxy_HTram(tram_gid);
}


void HTram::receivePerPE(HTramNodeMessage* msg) {
  int llimit = 0;
  int rank = CkMyRank();
  if(rank > 0) llimit = msg->offset[rank-1];
  int ulimit = msg->offset[rank];
  if(!ret_list) {
    for(int i=llimit; i<ulimit;i++)
      cb(objPtr, msg->buffer[i]);
  } else
    cb_retarr(objPtr, &msg->buffer[llimit], ulimit-llimit);
  CkFreeMsg(msg);
  tot_recv_count += (ulimit-llimit);
}
//#endif

void HTram::stop_periodic_flush() {
  enable_flush = false;
}

void periodic_tflush(void *htram_obj, double time) {
//  CkPrintf("\nIn callback_fn on PE#%d at time %lf",CkMyPe(), CkWallTimer());
  HTram *proper_obj = (HTram *)htram_obj;
  proper_obj->tflush();
  if(proper_obj->enable_flush)
    proper_obj->registercb();
}

void HTram::sanityCheck() {
  int tram_h_count = 0;
  if(tram_hold)
  for(int i=0; i< 512;i++)
       tram_h_count += tram_hold[i].size(); 
  contribute(sizeof(int), &tot_send_count, CkReduction::sum_int, CkCallback(CkReductionTarget(HTram, getTotSendCount), thisProxy[0]));
  contribute(sizeof(int), &tot_recv_count, CkReduction::sum_int, CkCallback(CkReductionTarget(HTram, getTotRecvCount), thisProxy[0]));
  contribute(sizeof(int), &tram_h_count, CkReduction::sum_int, CkCallback(CkReductionTarget(HTram, getTotTramHCount), thisProxy[0]));
}

void HTram::getTotTramHCount(int hcount) {
  CkPrintf("Total items remaining in tram_hold = %d\n", hcount);
}

void HTram::getTotSendCount(int scount){
  CkPrintf("Total items sent via tram library = %d", scount);
}

void HTram::getTotRecvCount(int rcount){
  CkPrintf("Total items recevied via tram library = %d\n", rcount);
}

#include "htram_group.def.h"
