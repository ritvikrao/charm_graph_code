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

  for(int i=0;i<CkNumPes();i++)
    msgBuffers[i] = new HTramMessage();

  for(int i=0;i<CkNumNodes();i++) {
    std::vector<HTramMessage*> vec1;
    overflowBuffers.push_back(vec1);
    std::vector<HTramMessage*> vec2;
    fillerOverflowBuffers.push_back(vec2);
    std::vector<int> int_min;
    std::vector<int> int_max;
    fillerOverflowBuffersBucketMin.push_back(int_min);
    fillerOverflowBuffersBucketMax.push_back(int_max);
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
  local_updates = 0;

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
//  CkPrintf("\nCalling insert buckets with threshold %d", high);
  if(high < 0) return;
  tram_threshold = high;
  est_total_items_in_bucket_arr = 0;
  for(int i=0;i<=high;i++)
    est_total_items_in_bucket_arr += tram_hold[i].size();
  insertBuckets(high);
}

#define FACTOR 2

int sum_of_sent(vector<int> sent) {
  int sum = 0;
  for(int i=0;i<CkNumNodes();i++) sum += sent[i];
  return sum;
}

void HTram::insertBuckets(int high) {
  est_total_items_in_bucket_arr += 256;
  if(est_total_items_in_bucket_arr < CkNumNodes()*BUFSIZE*FACTOR)
    return;

  vector<int> sent(CkNumNodes(),0);
#if 1
  for(int dest_node=0;dest_node<CkNumNodes();dest_node++) {
      int j=0;
      for(;j<overflowBuffers[dest_node].size();j++)
      {
//        CkPrintf("\nSending overflow buffers");
        tot_send_count += overflowBuffers[dest_node][j]->next;
        HTramMessage* sendMsg = new HTramMessage(overflowBuffers[dest_node][j]);
        nodeGrpProxy[dest_node].receive(sendMsg);
        //nodeGrp->msgs_in_transit[dest_node]++;
        sent[dest_node] = 1;
      }
      if(j)
        overflowBuffers[dest_node].erase(overflowBuffers[dest_node].begin(), overflowBuffers[dest_node].begin() + j);
  }
#endif

  //copy from vectors in order of index into messages
  int overflowed = 0;
  for(int i=0;i<=high/*histo_bucket_count*/;i++) {
    for(int j=0;j<tram_hold[i].size();j++) {
//      CkPrintf("\ni=%d,high=%d, j=%d, size = %d", i, high, j, tram_hold[i].size());
//      CkPrintf("\nIn tram bucket iterator");
      datatype item = tram_hold[i][j];
      int dest_proc = get_dest_proc(objPtr, item);
      if(dest_proc == -1) { local_updates++; continue;}
      int dest_node = dest_proc/CkNodeSize(0);
      HTramMessage* destMsg = msgBuffers[dest_node];
      destMsg->buffer[destMsg->next].payload = item;
      destMsg->buffer[destMsg->next].destPe = dest_proc;
      destMsg->next++;
      if(destMsg->next == BUFSIZE) {
        est_total_items_in_bucket_arr -= destMsg->next;
        if(sent[dest_node] == 1) {
//          CkPrintf("\nAdding to overflow buffers");
          overflowBuffers[dest_node].push_back(destMsg);
          msgBuffers[dest_node] = new HTramMessage();
        } else {
//          CkPrintf("\nSending regular msg");
          destMsg->srcPe = CkMyPe();
          tot_send_count += destMsg->next;
          nodeGrpProxy[dest_node].receive(destMsg);
          sent[dest_node]++;
          msgBuffers[dest_node] = new HTramMessage();
        }
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
//          srcNodeGrp->msgBuffers[i] = localMsgBuffer;
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
          nodeGrpProxy[i].receive(destMsg);
        } else if(agg == PP) {
          ((envelope *)UsrToEnv(destMsg))->setUsersize(sizeof(int)+sizeof(envelope)+sizeof(itemT)*destMsg->next);
//          CkPrintf("\nmsg size = %d", *destMsg->next);
          thisProxy[i].receiveOnPE(destMsg);
        }
          msgBuffers[i] = new HTramMessage();
      }
    }
  }
  if(agg == PNs) {
#if 1
    for(int dest_node=0;dest_node<CkNumNodes();dest_node++) {
      int j=0;
      for(;j<overflowBuffers[dest_node].size();j++) {
        tot_send_count += overflowBuffers[dest_node][j]->next;
        HTramMessage* sendMsg = new HTramMessage(overflowBuffers[dest_node][j]);
        nodeGrpProxy[dest_node].receive(sendMsg);
      }
      if(j)
        overflowBuffers[dest_node].erase(overflowBuffers[dest_node].begin(), overflowBuffers[dest_node].begin() + j);
    }
#endif

#if 1
    for(int i=0;i<=tram_threshold;i++) {
      for(int j=0;j<tram_hold[i].size();j++) {
        datatype item = tram_hold[i][j];
        int dest_proc = get_dest_proc(objPtr, item);
        if(dest_proc == -1) {local_updates++; continue;}
        int dest_node = dest_proc/CkNodeSize(0);
        HTramMessage* destMsg = msgBuffers[dest_node];
        destMsg->buffer[destMsg->next].payload = item;
        destMsg->buffer[destMsg->next].destPe = dest_proc;
        destMsg->next++;
        if(destMsg->next == BUFSIZE) {
          destMsg->srcPe = CkMyPe();
          tot_send_count += destMsg->next;
          nodeGrpProxy[dest_node].receive(destMsg);
          msgBuffers[dest_node] = new HTramMessage();
        }
      }
      tram_hold[i].clear();
    }
#if 1
    for(int node=0;node<CkNumNodes();node++) {
      HTramMessage* destMsg = msgBuffers[node];
      if(!destMsg->next) continue;
      int tram_filler_items = 0;

      for(int i=tram_threshold+1;i<histo_bucket_count;i++)
        tram_filler_items += tram_hold[i].size();

      if(destMsg->next < BUFSIZE/2)
//      while(destMsg->next < BUFSIZE/2 && (fillerOverflowBuffers[node].size() > 0 || tram_filler_items))
      { //If buffer is less than half full, fill it with filler items before send
        //Take filler items from filler overflow buffers
#if 1
//        CkPrintf("\n[PE-%d] Attempting to use fillers for msg buffer[to node %d] size = %d [%d > 0 or %d > 0]", thisIndex, node, destMsg->next, fillerOverflowBuffers[node].size(), tram_filler_items);
        if(fillerOverflowBuffers[node].size() > 0) {
          int j = 0;
          int fillers = BUFSIZE/2-destMsg->next;
          int k = 0;
          int items_donated = 0;
          for(;k<fillers;k++) {
            if(k >= fillerOverflowBuffers[node][j]->next) {
              fillerOverflowBuffers[node][j]->next = 0;
              items_donated = 0;
              if(j+1 > fillerOverflowBuffers[node].size()-1) break;
              j++;
            }
            datatype item = fillerOverflowBuffers[node][j]->buffer[k].payload;
            items_donated++;
            int dest_proc = get_dest_proc(objPtr, item);
            destMsg->buffer[destMsg->next].payload = item;
            destMsg->buffer[destMsg->next++].destPe = dest_proc;
          }
          int del = j-1;
          fillerOverflowBuffers[node][j]->next -= items_donated;
          if(!fillerOverflowBuffers[node][j]->next) del++;
          if(del >=0 ) fillerOverflowBuffers[node].erase(fillerOverflowBuffers[node].begin(), fillerOverflowBuffers[node].begin()+del);
        }
        if(destMsg->next < BUFSIZE/2) //still
#endif
        {
#if 1
          int filled_msg = 0;
          //Else take filler item from filler buckets
          for(int i=tram_threshold+1;i<histo_bucket_count;i++) {
            for(int j=0;j<tram_hold[i].size();j++) {
//              CkPrintf("\nAttempting to take item %d,%d", i,j);
              datatype item = tram_hold[i][j];
              int dest_proc = get_dest_proc(objPtr, item);
              if(dest_proc == -1) {local_updates++; continue;}
              int dest_node = dest_proc/CkNodeSize(0);
              if(dest_node == node && filled_msg == 0) {
                destMsg->buffer[destMsg->next].payload = item;
                destMsg->buffer[destMsg->next++].destPe = dest_proc;
                if(destMsg->next >= BUFSIZE/2) {
                  filled_msg = 1;
                }
              } else {
#if 1
                //CkPrintf("\ndest_node = %d, number of fill_ov_bufs = %d", dest_node, fillerOverflowBuffers.size());
//                int size = 1;//fillerOverflowBuffers[dest_node].size();
                HTramMessage* msg;
                int index;
                if(fillerOverflowBuffers[dest_node].size()==0) {
                  msg = new HTramMessage();
                  fillerOverflowBuffers[dest_node].push_back(msg);
                  index = fillerOverflowBuffers[dest_node].size()-1;
                  fillerOverflowBuffersBucketMin[dest_node].push_back(0);
                  fillerOverflowBuffersBucketMax[dest_node].push_back(100);
                } else {
                  msg = fillerOverflowBuffers[dest_node].back();
                  index = fillerOverflowBuffers[dest_node].size()-1;
                  if(msg->next == BUFSIZE) {
                    msg = new HTramMessage();
                    fillerOverflowBuffers[dest_node].push_back(msg);
                    index = fillerOverflowBuffers[dest_node].size()-1;
                    fillerOverflowBuffersBucketMin[dest_node].push_back(0);
                    fillerOverflowBuffersBucketMax[dest_node].push_back(100);
                  }
                }
                if(fillerOverflowBuffersBucketMin[dest_node][index] > i) fillerOverflowBuffersBucketMin[dest_node][index] = i;
                if(fillerOverflowBuffersBucketMax[dest_node][index] < i) fillerOverflowBuffersBucketMax[dest_node][index] = i;
                msg->buffer[msg->next].payload = item;
                msg->buffer[msg->next++].destPe = dest_proc;
#endif
              }
            }
            tram_hold[i].clear();
            if(destMsg->next >= BUFSIZE/2 || filled_msg) break;
          }
#endif
        }
        tram_filler_items = 0;
        for(int i=tram_threshold+1;i<histo_bucket_count;i++)
          tram_filler_items += tram_hold[i].size();
      }
      tot_send_count += destMsg->next;
      nodeGrpProxy[node].receive(destMsg);
      msgBuffers[node] = new HTramMessage();
    }
#endif
    //Last filler count is 0, fix to send last set of message buffers
//    CkPrintf("\nFlush sent these items from fillers %d from buckets[%d-%d]", filler_item_count, tram_threshold+1, histo_bucket_count);
    tram_done(objPtr);
#endif
}
}

void HTram::flush_everything() {
  if(agg == PNs) {
    for(int i=0;i<histo_bucket_count;i++) {
      for(int j=0;j<tram_hold[i].size();j++) {
        datatype item = tram_hold[i][j];
        int dest_proc = get_dest_proc(objPtr, item);
        if(dest_proc == -1) {local_updates++;continue;}
        int dest_node = dest_proc/CkNodeSize(0);
        HTramMessage* destMsg = msgBuffers[dest_node];
        destMsg->buffer[destMsg->next].payload = item;
        destMsg->buffer[destMsg->next].destPe = dest_proc;
        destMsg->next++;
        if(destMsg->next == BUFSIZE) {
          destMsg->srcPe = CkMyPe();
          tot_send_count += destMsg->next;
          nodeGrpProxy[dest_node].receive(destMsg);
          msgBuffers[dest_node] = new HTramMessage();
        }
      }
      tram_hold[i].clear();
    }
    for(int dest_node=0;dest_node<CkNumNodes();dest_node++) {
      int j=0;
      for(;j<overflowBuffers[dest_node].size();j++) {
        //if(overflowBuffers[dest_node][j]->next <=0)
//          {CkPrintf("\noverflow buffer[dest%d, idx%d] = next%d",dest_node, j, overflowBuffers[dest_node][j]->next);} ;
        tot_send_count += overflowBuffers[dest_node][j]->next;
        HTramMessage* sendMsg = new HTramMessage(overflowBuffers[dest_node][j]);
        nodeGrpProxy[dest_node].receive(sendMsg);
      }
      if(j)
        overflowBuffers[dest_node].erase(overflowBuffers[dest_node].begin(), overflowBuffers[dest_node].begin() + j);
    }

    for(int dest_node=0;dest_node<CkNumNodes();dest_node++) {
      int j=0;
      for(;j<fillerOverflowBuffers[dest_node].size();j++) {
//        {CkPrintf("\nfiller overflow buffer[dest%d, idx%d] = next%d",dest_node, j, fillerOverflowBuffers[dest_node][j]->next);} ;
        HTramMessage* sendMsg = new HTramMessage(fillerOverflowBuffers[dest_node][j]);
        nodeGrpProxy[dest_node].receive(sendMsg);
        tot_send_count += fillerOverflowBuffers[dest_node][j]->next;
      }
      if(j)
        fillerOverflowBuffers[dest_node].erase(fillerOverflowBuffers[dest_node].begin(), fillerOverflowBuffers[dest_node].begin() + j);
    }

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
  int tram_h_count = tot_send_count+local_updates;
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
