










/* ---------------- method closures -------------- */
#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_Main::begin_2_closure : public SDAG::Closure {
            int result;


      begin_2_closure() {
        init();
      }
      begin_2_closure(CkMigrateMessage*) {
        init();
      }
            int & getP0() { return result;}
      void pup(PUP::er& __p) {
        __p | result;
        packClosure(__p);
      }
      virtual ~begin_2_closure() {
      }
      PUPable_decl(SINGLE_ARG(begin_2_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_Main::print_3_closure : public SDAG::Closure {
      

      print_3_closure() {
        init();
      }
      print_3_closure(CkMigrateMessage*) {
        init();
      }
            void pup(PUP::er& __p) {
        packClosure(__p);
      }
      virtual ~print_3_closure() {
      }
      PUPable_decl(SINGLE_ARG(print_3_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_Main::done_4_closure : public SDAG::Closure {
            int result;


      done_4_closure() {
        init();
      }
      done_4_closure(CkMigrateMessage*) {
        init();
      }
            int & getP0() { return result;}
      void pup(PUP::er& __p) {
        __p | result;
        packClosure(__p);
      }
      virtual ~done_4_closure() {
      }
      PUPable_decl(SINGLE_ARG(done_4_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_Main::check_buffer_done_5_closure : public SDAG::Closure {
            int *msg_stats;
            int N;

      CkMarshallMsg* _impl_marshall;
      char* _impl_buf_in;
      int _impl_buf_size;

      check_buffer_done_5_closure() {
        init();
        _impl_marshall = 0;
        _impl_buf_in = 0;
        _impl_buf_size = 0;
      }
      check_buffer_done_5_closure(CkMigrateMessage*) {
        init();
        _impl_marshall = 0;
        _impl_buf_in = 0;
        _impl_buf_size = 0;
      }
            int *& getP0() { return msg_stats;}
            int & getP1() { return N;}
      void pup(PUP::er& __p) {
        __p | N;
        packClosure(__p);
        __p | _impl_buf_size;
        bool hasMsg = (_impl_marshall != 0); __p | hasMsg;
        if (hasMsg) CkPupMessage(__p, (void**)&_impl_marshall);
        else PUParray(__p, _impl_buf_in, _impl_buf_size);
        if (__p.isUnpacking()) {
          char *impl_buf = _impl_marshall ? _impl_marshall->msgBuf : _impl_buf_in;
          PUP::fromMem implP(impl_buf);
  int impl_off_msg_stats, impl_cnt_msg_stats;
  implP|impl_off_msg_stats;
  implP|impl_cnt_msg_stats;
  PUP::detail::TemporaryObjectHolder<int> N;
  implP|N;
          impl_buf+=CK_ALIGN(implP.size(),16);
          msg_stats = (int *)(impl_buf+impl_off_msg_stats);
        }
      }
      virtual ~check_buffer_done_5_closure() {
        if (_impl_marshall) CmiFree(UsrToEnv(_impl_marshall));
      }
      PUPable_decl(SINGLE_ARG(check_buffer_done_5_closure));
    };
#endif /* CK_TEMPLATES_ONLY */


/* ---------------- method closures -------------- */
#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_WeightedArray::initiate_pointers_2_closure : public SDAG::Closure {
      

      initiate_pointers_2_closure() {
        init();
      }
      initiate_pointers_2_closure(CkMigrateMessage*) {
        init();
      }
            void pup(PUP::er& __p) {
        packClosure(__p);
      }
      virtual ~initiate_pointers_2_closure() {
      }
      PUPable_decl(SINGLE_ARG(initiate_pointers_2_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_WeightedArray::get_graph_3_closure : public SDAG::Closure {
            LongEdge *edges;
            int E;
            int *partition;
            int dividers;

      CkMarshallMsg* _impl_marshall;
      char* _impl_buf_in;
      int _impl_buf_size;

      get_graph_3_closure() {
        init();
        _impl_marshall = 0;
        _impl_buf_in = 0;
        _impl_buf_size = 0;
      }
      get_graph_3_closure(CkMigrateMessage*) {
        init();
        _impl_marshall = 0;
        _impl_buf_in = 0;
        _impl_buf_size = 0;
      }
            LongEdge *& getP0() { return edges;}
            int & getP1() { return E;}
            int *& getP2() { return partition;}
            int & getP3() { return dividers;}
      void pup(PUP::er& __p) {
        __p | E;
        __p | dividers;
        packClosure(__p);
        __p | _impl_buf_size;
        bool hasMsg = (_impl_marshall != 0); __p | hasMsg;
        if (hasMsg) CkPupMessage(__p, (void**)&_impl_marshall);
        else PUParray(__p, _impl_buf_in, _impl_buf_size);
        if (__p.isUnpacking()) {
          char *impl_buf = _impl_marshall ? _impl_marshall->msgBuf : _impl_buf_in;
          PUP::fromMem implP(impl_buf);
  int impl_off_edges, impl_cnt_edges;
  implP|impl_off_edges;
  implP|impl_cnt_edges;
  PUP::detail::TemporaryObjectHolder<int> E;
  implP|E;
  int impl_off_partition, impl_cnt_partition;
  implP|impl_off_partition;
  implP|impl_cnt_partition;
  PUP::detail::TemporaryObjectHolder<int> dividers;
  implP|dividers;
          impl_buf+=CK_ALIGN(implP.size(),16);
          edges = (LongEdge *)(impl_buf+impl_off_edges);
          partition = (int *)(impl_buf+impl_off_partition);
        }
      }
      virtual ~get_graph_3_closure() {
        if (_impl_marshall) CmiFree(UsrToEnv(_impl_marshall));
      }
      PUPable_decl(SINGLE_ARG(get_graph_3_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_WeightedArray::update_distances_4_closure : public SDAG::Closure {
            std::pair<int,int> new_vertex_and_distance;


      update_distances_4_closure() {
        init();
      }
      update_distances_4_closure(CkMigrateMessage*) {
        init();
      }
            std::pair<int,int> & getP0() { return new_vertex_and_distance;}
      void pup(PUP::er& __p) {
        __p | new_vertex_and_distance;
        packClosure(__p);
      }
      virtual ~update_distances_4_closure() {
      }
      PUPable_decl(SINGLE_ARG(update_distances_4_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_WeightedArray::check_buffer_5_closure : public SDAG::Closure {
      

      check_buffer_5_closure() {
        init();
      }
      check_buffer_5_closure(CkMigrateMessage*) {
        init();
      }
            void pup(PUP::er& __p) {
        packClosure(__p);
      }
      virtual ~check_buffer_5_closure() {
      }
      PUPable_decl(SINGLE_ARG(check_buffer_5_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_WeightedArray::keep_going_6_closure : public SDAG::Closure {
      

      keep_going_6_closure() {
        init();
      }
      keep_going_6_closure(CkMigrateMessage*) {
        init();
      }
            void pup(PUP::er& __p) {
        packClosure(__p);
      }
      virtual ~keep_going_6_closure() {
      }
      PUPable_decl(SINGLE_ARG(keep_going_6_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_WeightedArray::print_distances_7_closure : public SDAG::Closure {
      

      print_distances_7_closure() {
        init();
      }
      print_distances_7_closure(CkMigrateMessage*) {
        init();
      }
            void pup(PUP::er& __p) {
        packClosure(__p);
      }
      virtual ~print_distances_7_closure() {
      }
      PUPable_decl(SINGLE_ARG(print_distances_7_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */




/* DEFS: readonly CProxy_Main mainProxy;
 */
extern CProxy_Main mainProxy;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_mainProxy(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|mainProxy;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly CProxy_WeightedArray arr;
 */
extern CProxy_WeightedArray arr;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_arr(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|arr;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly int N;
 */
extern int N;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_N(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|N;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly int V;
 */
extern int V;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_V(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|V;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly int imax;
 */
extern int imax;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_imax(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|imax;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly int average;
 */
extern int average;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_average(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|average;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly int buffer_size;
 */
extern int buffer_size;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_buffer_size(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|buffer_size;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly double flush_timer;
 */
extern double flush_timer;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_flush_timer(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|flush_timer;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly bool enable_buffer_flushing;
 */
extern bool enable_buffer_flushing;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_enable_buffer_flushing(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|enable_buffer_flushing;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: mainchare Main: Chare{
Main(CkArgMsg* impl_msg);
void begin(int result);
void print();
void done(int result);
void check_buffer_done(const int *msg_stats, int N);
};
 */
#ifndef CK_TEMPLATES_ONLY
 int CkIndex_Main::__idx=0;
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
/* DEFS: Main(CkArgMsg* impl_msg);
 */
CkChareID CProxy_Main::ckNew(CkArgMsg* impl_msg, int impl_onPE)
{
  CkChareID impl_ret;
  CkCreateChare(CkIndex_Main::__idx, CkIndex_Main::idx_Main_CkArgMsg(), impl_msg, &impl_ret, impl_onPE);
  return impl_ret;
}
void CProxy_Main::ckNew(CkArgMsg* impl_msg, CkChareID* pcid, int impl_onPE)
{
  CkCreateChare(CkIndex_Main::__idx, CkIndex_Main::idx_Main_CkArgMsg(), impl_msg, pcid, impl_onPE);
}

// Entry point registration function
int CkIndex_Main::reg_Main_CkArgMsg() {
  int epidx = CkRegisterEp("Main(CkArgMsg* impl_msg)",
      reinterpret_cast<CkCallFnPtr>(_call_Main_CkArgMsg), CMessage_CkArgMsg::__idx, __idx, 0);
  CkRegisterMessagePupFn(epidx, (CkMessagePupFn)CkArgMsg::ckDebugPup);
  return epidx;
}

void CkIndex_Main::_call_Main_CkArgMsg(void* impl_msg, void* impl_obj_void)
{
  Main* impl_obj = static_cast<Main*>(impl_obj_void);
  new (impl_obj_void) Main((CkArgMsg*)impl_msg);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void begin(int result);
 */
void CProxy_Main::begin(int result, const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  //Marshall: int result
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    implP|result;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    implP|result;
  }
  if (ckIsDelegated()) {
    int destPE=CkChareMsgPrep(CkIndex_Main::idx_begin_marshall2(), impl_msg, &ckGetChareID());
    if (destPE!=-1) ckDelegatedTo()->ChareSend(ckDelegatedPtr(),CkIndex_Main::idx_begin_marshall2(), impl_msg, &ckGetChareID(),destPE);
  } else {
    CkSendMsg(CkIndex_Main::idx_begin_marshall2(), impl_msg, &ckGetChareID(),0);
  }
}
void CkIndex_Main::_call_redn_wrapper_begin_marshall2(void* impl_msg, void* impl_obj_void)
{
  Main* impl_obj = static_cast<Main*> (impl_obj_void);
  char* impl_buf = (char*)((CkReductionMsg*)impl_msg)->getData();
  /*Unmarshall pup'd fields: int result*/
  PUP::fromMem implP(impl_buf);
  /* non two-param case */
  PUP::detail::TemporaryObjectHolder<int> result;
  implP|result;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->begin(std::move(result.t));
  delete (CkReductionMsg*)impl_msg;
}


// Entry point registration function
int CkIndex_Main::reg_begin_marshall2() {
  int epidx = CkRegisterEp("begin(int result)",
      reinterpret_cast<CkCallFnPtr>(_call_begin_marshall2), CkMarshallMsg::__idx, __idx, 0+CK_EP_NOKEEP);
  CkRegisterMarshallUnpackFn(epidx, _callmarshall_begin_marshall2);
  CkRegisterMessagePupFn(epidx, _marshallmessagepup_begin_marshall2);

  return epidx;
}


// Redn wrapper registration function
int CkIndex_Main::reg_redn_wrapper_begin_marshall2() {
  return CkRegisterEp("redn_wrapper_begin(CkReductionMsg *impl_msg)",
      reinterpret_cast<CkCallFnPtr>(_call_redn_wrapper_begin_marshall2), CkMarshallMsg::__idx, __idx, 0);
}

void CkIndex_Main::_call_begin_marshall2(void* impl_msg, void* impl_obj_void)
{
  Main* impl_obj = static_cast<Main*>(impl_obj_void);
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: int result*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<int> result;
  implP|result;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->begin(std::move(result.t));
}
int CkIndex_Main::_callmarshall_begin_marshall2(char* impl_buf, void* impl_obj_void) {
  Main* impl_obj = static_cast<Main*>(impl_obj_void);
  envelope *env = UsrToEnv(impl_buf);
  /*Unmarshall pup'd fields: int result*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<int> result;
  implP|result;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->begin(std::move(result.t));
  return implP.size();
}
void CkIndex_Main::_marshallmessagepup_begin_marshall2(PUP::er &implDestP,void *impl_msg) {
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: int result*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<int> result;
  implP|result;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  if (implDestP.hasComments()) implDestP.comment("result");
  implDestP|result;
}
PUPable_def(SINGLE_ARG(Closure_Main::begin_2_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void print();
 */
void CProxy_Main::print(const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  if (ckIsDelegated()) {
    int destPE=CkChareMsgPrep(CkIndex_Main::idx_print_void(), impl_msg, &ckGetChareID());
    if (destPE!=-1) ckDelegatedTo()->ChareSend(ckDelegatedPtr(),CkIndex_Main::idx_print_void(), impl_msg, &ckGetChareID(),destPE);
  } else {
    CkSendMsg(CkIndex_Main::idx_print_void(), impl_msg, &ckGetChareID(),0);
  }
}

// Entry point registration function
int CkIndex_Main::reg_print_void() {
  int epidx = CkRegisterEp("print()",
      reinterpret_cast<CkCallFnPtr>(_call_print_void), 0, __idx, 0);
  return epidx;
}

void CkIndex_Main::_call_print_void(void* impl_msg, void* impl_obj_void)
{
  Main* impl_obj = static_cast<Main*>(impl_obj_void);
  impl_obj->print();
  if(UsrToEnv(impl_msg)->isVarSysMsg() == 0)
    CkFreeSysMsg(impl_msg);
}
PUPable_def(SINGLE_ARG(Closure_Main::print_3_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void done(int result);
 */
void CProxy_Main::done(int result, const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  //Marshall: int result
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    implP|result;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    implP|result;
  }
  if (ckIsDelegated()) {
    int destPE=CkChareMsgPrep(CkIndex_Main::idx_done_marshall4(), impl_msg, &ckGetChareID());
    if (destPE!=-1) ckDelegatedTo()->ChareSend(ckDelegatedPtr(),CkIndex_Main::idx_done_marshall4(), impl_msg, &ckGetChareID(),destPE);
  } else {
    CkSendMsg(CkIndex_Main::idx_done_marshall4(), impl_msg, &ckGetChareID(),0);
  }
}
void CkIndex_Main::_call_redn_wrapper_done_marshall4(void* impl_msg, void* impl_obj_void)
{
  Main* impl_obj = static_cast<Main*> (impl_obj_void);
  char* impl_buf = (char*)((CkReductionMsg*)impl_msg)->getData();
  /*Unmarshall pup'd fields: int result*/
  PUP::fromMem implP(impl_buf);
  /* non two-param case */
  PUP::detail::TemporaryObjectHolder<int> result;
  implP|result;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->done(std::move(result.t));
  delete (CkReductionMsg*)impl_msg;
}


// Entry point registration function
int CkIndex_Main::reg_done_marshall4() {
  int epidx = CkRegisterEp("done(int result)",
      reinterpret_cast<CkCallFnPtr>(_call_done_marshall4), CkMarshallMsg::__idx, __idx, 0+CK_EP_NOKEEP);
  CkRegisterMarshallUnpackFn(epidx, _callmarshall_done_marshall4);
  CkRegisterMessagePupFn(epidx, _marshallmessagepup_done_marshall4);

  return epidx;
}


// Redn wrapper registration function
int CkIndex_Main::reg_redn_wrapper_done_marshall4() {
  return CkRegisterEp("redn_wrapper_done(CkReductionMsg *impl_msg)",
      reinterpret_cast<CkCallFnPtr>(_call_redn_wrapper_done_marshall4), CkMarshallMsg::__idx, __idx, 0);
}

void CkIndex_Main::_call_done_marshall4(void* impl_msg, void* impl_obj_void)
{
  Main* impl_obj = static_cast<Main*>(impl_obj_void);
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: int result*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<int> result;
  implP|result;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->done(std::move(result.t));
}
int CkIndex_Main::_callmarshall_done_marshall4(char* impl_buf, void* impl_obj_void) {
  Main* impl_obj = static_cast<Main*>(impl_obj_void);
  envelope *env = UsrToEnv(impl_buf);
  /*Unmarshall pup'd fields: int result*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<int> result;
  implP|result;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->done(std::move(result.t));
  return implP.size();
}
void CkIndex_Main::_marshallmessagepup_done_marshall4(PUP::er &implDestP,void *impl_msg) {
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: int result*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<int> result;
  implP|result;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  if (implDestP.hasComments()) implDestP.comment("result");
  implDestP|result;
}
PUPable_def(SINGLE_ARG(Closure_Main::done_4_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void check_buffer_done(const int *msg_stats, int N);
 */
void CProxy_Main::check_buffer_done(const int *msg_stats, int N, const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  //Marshall: const int *msg_stats, int N
  int impl_off=0;
  int impl_arrstart=0;
  int impl_off_msg_stats, impl_cnt_msg_stats;
  impl_off_msg_stats=impl_off=CK_ALIGN(impl_off,sizeof(int));
  impl_off+=(impl_cnt_msg_stats=sizeof(int)*(N));
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    implP|impl_off_msg_stats;
    implP|impl_cnt_msg_stats;
    implP|N;
    impl_arrstart=CK_ALIGN(implP.size(),16);
    impl_off+=impl_arrstart;
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    implP|impl_off_msg_stats;
    implP|impl_cnt_msg_stats;
    implP|N;
  }
  char *impl_buf=impl_msg->msgBuf+impl_arrstart;
  memcpy(impl_buf+impl_off_msg_stats,msg_stats,impl_cnt_msg_stats);
  if (ckIsDelegated()) {
    int destPE=CkChareMsgPrep(CkIndex_Main::idx_check_buffer_done_marshall5(), impl_msg, &ckGetChareID());
    if (destPE!=-1) ckDelegatedTo()->ChareSend(ckDelegatedPtr(),CkIndex_Main::idx_check_buffer_done_marshall5(), impl_msg, &ckGetChareID(),destPE);
  } else {
    CkSendMsg(CkIndex_Main::idx_check_buffer_done_marshall5(), impl_msg, &ckGetChareID(),0);
  }
}
void CkIndex_Main::_call_redn_wrapper_check_buffer_done_marshall5(void* impl_msg, void* impl_obj_void)
{
  Main* impl_obj = static_cast<Main*> (impl_obj_void);
  char* impl_buf = (char*)((CkReductionMsg*)impl_msg)->getData();
  /*Unmarshall pup'd fields: const int *msg_stats, int N*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<int> N; N.t = ((CkReductionMsg*)impl_msg)->getLength() / sizeof(int);
  int* msg_stats; msg_stats = (int*)impl_buf;
  impl_obj->check_buffer_done(msg_stats, std::move(N.t));
  delete (CkReductionMsg*)impl_msg;
}


// Entry point registration function
int CkIndex_Main::reg_check_buffer_done_marshall5() {
  int epidx = CkRegisterEp("check_buffer_done(const int *msg_stats, int N)",
      reinterpret_cast<CkCallFnPtr>(_call_check_buffer_done_marshall5), CkMarshallMsg::__idx, __idx, 0+CK_EP_NOKEEP);
  CkRegisterMarshallUnpackFn(epidx, _callmarshall_check_buffer_done_marshall5);
  CkRegisterMessagePupFn(epidx, _marshallmessagepup_check_buffer_done_marshall5);

  return epidx;
}


// Redn wrapper registration function
int CkIndex_Main::reg_redn_wrapper_check_buffer_done_marshall5() {
  return CkRegisterEp("redn_wrapper_check_buffer_done(CkReductionMsg *impl_msg)",
      reinterpret_cast<CkCallFnPtr>(_call_redn_wrapper_check_buffer_done_marshall5), CkMarshallMsg::__idx, __idx, 0);
}

void CkIndex_Main::_call_check_buffer_done_marshall5(void* impl_msg, void* impl_obj_void)
{
  Main* impl_obj = static_cast<Main*>(impl_obj_void);
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const int *msg_stats, int N*/
  PUP::fromMem implP(impl_buf);
  int impl_off_msg_stats, impl_cnt_msg_stats;
  implP|impl_off_msg_stats;
  implP|impl_cnt_msg_stats;
  PUP::detail::TemporaryObjectHolder<int> N;
  implP|N;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  int *msg_stats=(int *)(impl_buf+impl_off_msg_stats);
  impl_obj->check_buffer_done(msg_stats, std::move(N.t));
}
int CkIndex_Main::_callmarshall_check_buffer_done_marshall5(char* impl_buf, void* impl_obj_void) {
  Main* impl_obj = static_cast<Main*>(impl_obj_void);
  envelope *env = UsrToEnv(impl_buf);
  /*Unmarshall pup'd fields: const int *msg_stats, int N*/
  PUP::fromMem implP(impl_buf);
  int impl_off_msg_stats, impl_cnt_msg_stats;
  implP|impl_off_msg_stats;
  implP|impl_cnt_msg_stats;
  PUP::detail::TemporaryObjectHolder<int> N;
  implP|N;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  int *msg_stats=(int *)(impl_buf+impl_off_msg_stats);
  impl_obj->check_buffer_done(msg_stats, std::move(N.t));
  return implP.size();
}
void CkIndex_Main::_marshallmessagepup_check_buffer_done_marshall5(PUP::er &implDestP,void *impl_msg) {
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const int *msg_stats, int N*/
  PUP::fromMem implP(impl_buf);
  int impl_off_msg_stats, impl_cnt_msg_stats;
  implP|impl_off_msg_stats;
  implP|impl_cnt_msg_stats;
  PUP::detail::TemporaryObjectHolder<int> N;
  implP|N;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  int *msg_stats=(int *)(impl_buf+impl_off_msg_stats);
  if (implDestP.hasComments()) implDestP.comment("msg_stats");
  implDestP.synchronize(PUP::sync_begin_array);
  for (int impl_i=0;impl_i*(sizeof(*msg_stats))<impl_cnt_msg_stats;impl_i++) {
    implDestP.synchronize(PUP::sync_item);
    implDestP|msg_stats[impl_i];
  }
  implDestP.synchronize(PUP::sync_end_array);
  if (implDestP.hasComments()) implDestP.comment("N");
  implDestP|N;
}
PUPable_def(SINGLE_ARG(Closure_Main::check_buffer_done_5_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
void CkIndex_Main::__register(const char *s, size_t size) {
  __idx = CkRegisterChare(s, size, TypeMainChare);
  CkRegisterBase(__idx, CkIndex_Chare::__idx);
  // REG: Main(CkArgMsg* impl_msg);
  idx_Main_CkArgMsg();
  CkRegisterMainChare(__idx, idx_Main_CkArgMsg());

  // REG: void begin(int result);
  idx_begin_marshall2();
  idx_redn_wrapper_begin_marshall2();

  // REG: void print();
  idx_print_void();

  // REG: void done(int result);
  idx_done_marshall4();
  idx_redn_wrapper_done_marshall4();

  // REG: void check_buffer_done(const int *msg_stats, int N);
  idx_check_buffer_done_marshall5();
  idx_redn_wrapper_check_buffer_done_marshall5();

}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: array WeightedArray: ArrayElement{
WeightedArray();
void initiate_pointers();
void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
void update_distances(const std::pair<int,int> &new_vertex_and_distance);
void check_buffer();
void keep_going();
void print_distances();
WeightedArray(CkMigrateMessage* impl_msg);
};
 */
#ifndef CK_TEMPLATES_ONLY
 int CkIndex_WeightedArray::__idx=0;
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
void CProxySection_WeightedArray::contribute(CkSectionInfo &sid, int userData, int fragSize)
{
   CkArray *ckarr = CProxy_CkArray(sid.get_aid()).ckLocalBranch();
   CkMulticastMgr *mCastGrp = CProxy_CkMulticastMgr(ckarr->getmCastMgr()).ckLocalBranch();
   mCastGrp->contribute(sid, userData, fragSize);
}

void CProxySection_WeightedArray::contribute(int dataSize,void *data,CkReduction::reducerType type, CkSectionInfo &sid, int userData, int fragSize)
{
   CkArray *ckarr = CProxy_CkArray(sid.get_aid()).ckLocalBranch();
   CkMulticastMgr *mCastGrp = CProxy_CkMulticastMgr(ckarr->getmCastMgr()).ckLocalBranch();
   mCastGrp->contribute(dataSize, data, type, sid, userData, fragSize);
}

template <typename T>
void CProxySection_WeightedArray::contribute(std::vector<T> &data, CkReduction::reducerType type, CkSectionInfo &sid, int userData, int fragSize)
{
   CkArray *ckarr = CProxy_CkArray(sid.get_aid()).ckLocalBranch();
   CkMulticastMgr *mCastGrp = CProxy_CkMulticastMgr(ckarr->getmCastMgr()).ckLocalBranch();
   mCastGrp->contribute(data, type, sid, userData, fragSize);
}

void CProxySection_WeightedArray::contribute(CkSectionInfo &sid, const CkCallback &cb, int userData, int fragSize)
{
   CkArray *ckarr = CProxy_CkArray(sid.get_aid()).ckLocalBranch();
   CkMulticastMgr *mCastGrp = CProxy_CkMulticastMgr(ckarr->getmCastMgr()).ckLocalBranch();
   mCastGrp->contribute(sid, cb, userData, fragSize);
}

void CProxySection_WeightedArray::contribute(int dataSize,void *data,CkReduction::reducerType type, CkSectionInfo &sid, const CkCallback &cb, int userData, int fragSize)
{
   CkArray *ckarr = CProxy_CkArray(sid.get_aid()).ckLocalBranch();
   CkMulticastMgr *mCastGrp = CProxy_CkMulticastMgr(ckarr->getmCastMgr()).ckLocalBranch();
   mCastGrp->contribute(dataSize, data, type, sid, cb, userData, fragSize);
}

template <typename T>
void CProxySection_WeightedArray::contribute(std::vector<T> &data, CkReduction::reducerType type, CkSectionInfo &sid, const CkCallback &cb, int userData, int fragSize)
{
   CkArray *ckarr = CProxy_CkArray(sid.get_aid()).ckLocalBranch();
   CkMulticastMgr *mCastGrp = CProxy_CkMulticastMgr(ckarr->getmCastMgr()).ckLocalBranch();
   mCastGrp->contribute(data, type, sid, cb, userData, fragSize);
}

#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
/* DEFS: WeightedArray();
 */
void CProxyElement_WeightedArray::insert(int onPE, const CkEntryOptions *impl_e_opts)
{ 
   void *impl_msg = CkAllocSysMsg(impl_e_opts);
   UsrToEnv(impl_msg)->setMsgtype(ArrayEltInitMsg);
   ckInsert((CkArrayMessage *)impl_msg,CkIndex_WeightedArray::idx_WeightedArray_void(),onPE);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void initiate_pointers();
 */
void CProxyElement_WeightedArray::initiate_pointers(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_initiate_pointers_void(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
 */
void CProxyElement_WeightedArray::get_graph(const LongEdge *edges, int E, const int *partition, int dividers, const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  //Marshall: const LongEdge *edges, int E, const int *partition, int dividers
  int impl_off=0;
  int impl_arrstart=0;
  int impl_off_edges, impl_cnt_edges;
  impl_off_edges=impl_off=CK_ALIGN(impl_off,sizeof(LongEdge));
  impl_off+=(impl_cnt_edges=sizeof(LongEdge)*(E));
  int impl_off_partition, impl_cnt_partition;
  impl_off_partition=impl_off=CK_ALIGN(impl_off,sizeof(int));
  impl_off+=(impl_cnt_partition=sizeof(int)*(dividers));
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    implP|impl_off_edges;
    implP|impl_cnt_edges;
    implP|E;
    implP|impl_off_partition;
    implP|impl_cnt_partition;
    implP|dividers;
    impl_arrstart=CK_ALIGN(implP.size(),16);
    impl_off+=impl_arrstart;
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    implP|impl_off_edges;
    implP|impl_cnt_edges;
    implP|E;
    implP|impl_off_partition;
    implP|impl_cnt_partition;
    implP|dividers;
  }
  char *impl_buf=impl_msg->msgBuf+impl_arrstart;
  memcpy(impl_buf+impl_off_edges,edges,impl_cnt_edges);
  memcpy(impl_buf+impl_off_partition,partition,impl_cnt_partition);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_get_graph_marshall3(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void update_distances(const std::pair<int,int> &new_vertex_and_distance);
 */
void CProxyElement_WeightedArray::update_distances(const std::pair<int,int> &new_vertex_and_distance, const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  //Marshall: const std::pair<int,int> &new_vertex_and_distance
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)new_vertex_and_distance;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)new_vertex_and_distance;
  }
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_update_distances_marshall4(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void check_buffer();
 */
void CProxyElement_WeightedArray::check_buffer(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_check_buffer_void(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void keep_going();
 */
void CProxyElement_WeightedArray::keep_going(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_keep_going_void(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void print_distances();
 */
void CProxyElement_WeightedArray::print_distances(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_print_distances_void(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: WeightedArray(CkMigrateMessage* impl_msg);
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: WeightedArray();
 */
CkArrayID CProxy_WeightedArray::ckNew(const CkArrayOptions &opts, const CkEntryOptions *impl_e_opts)
{
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ArrayEltInitMsg);
  CkArrayID gId = ckCreateArray((CkArrayMessage *)impl_msg, CkIndex_WeightedArray::idx_WeightedArray_void(), opts);
  return gId;
}
void CProxy_WeightedArray::ckNew(const CkArrayOptions &opts, CkCallback _ck_array_creation_cb, const CkEntryOptions *impl_e_opts)
{
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ArrayEltInitMsg);
  CkSendAsyncCreateArray(CkIndex_WeightedArray::idx_WeightedArray_void(), _ck_array_creation_cb, opts, impl_msg);
}
CkArrayID CProxy_WeightedArray::ckNew(const int s1, const CkEntryOptions *impl_e_opts)
{
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  CkArrayOptions opts(s1);
  UsrToEnv(impl_msg)->setMsgtype(ArrayEltInitMsg);
  CkArrayID gId = ckCreateArray((CkArrayMessage *)impl_msg, CkIndex_WeightedArray::idx_WeightedArray_void(), opts);
  return gId;
}
void CProxy_WeightedArray::ckNew(const int s1, CkCallback _ck_array_creation_cb, const CkEntryOptions *impl_e_opts)
{
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  CkArrayOptions opts(s1);
  UsrToEnv(impl_msg)->setMsgtype(ArrayEltInitMsg);
  CkSendAsyncCreateArray(CkIndex_WeightedArray::idx_WeightedArray_void(), _ck_array_creation_cb, opts, impl_msg);
}

// Entry point registration function
int CkIndex_WeightedArray::reg_WeightedArray_void() {
  int epidx = CkRegisterEp("WeightedArray()",
      reinterpret_cast<CkCallFnPtr>(_call_WeightedArray_void), 0, __idx, 0);
  return epidx;
}

void CkIndex_WeightedArray::_call_WeightedArray_void(void* impl_msg, void* impl_obj_void)
{
  WeightedArray* impl_obj = static_cast<WeightedArray*>(impl_obj_void);
  new (impl_obj_void) WeightedArray();
  if(UsrToEnv(impl_msg)->isVarSysMsg() == 0)
    CkFreeSysMsg(impl_msg);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void initiate_pointers();
 */
void CProxy_WeightedArray::initiate_pointers(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckBroadcast(impl_amsg, CkIndex_WeightedArray::idx_initiate_pointers_void(),0);
}

// Entry point registration function
int CkIndex_WeightedArray::reg_initiate_pointers_void() {
  int epidx = CkRegisterEp("initiate_pointers()",
      reinterpret_cast<CkCallFnPtr>(_call_initiate_pointers_void), 0, __idx, 0);
  return epidx;
}

void CkIndex_WeightedArray::_call_initiate_pointers_void(void* impl_msg, void* impl_obj_void)
{
  WeightedArray* impl_obj = static_cast<WeightedArray*>(impl_obj_void);
  impl_obj->initiate_pointers();
  if(UsrToEnv(impl_msg)->isVarSysMsg() == 0)
    CkFreeSysMsg(impl_msg);
}
PUPable_def(SINGLE_ARG(Closure_WeightedArray::initiate_pointers_2_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
 */
void CProxy_WeightedArray::get_graph(const LongEdge *edges, int E, const int *partition, int dividers, const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  //Marshall: const LongEdge *edges, int E, const int *partition, int dividers
  int impl_off=0;
  int impl_arrstart=0;
  int impl_off_edges, impl_cnt_edges;
  impl_off_edges=impl_off=CK_ALIGN(impl_off,sizeof(LongEdge));
  impl_off+=(impl_cnt_edges=sizeof(LongEdge)*(E));
  int impl_off_partition, impl_cnt_partition;
  impl_off_partition=impl_off=CK_ALIGN(impl_off,sizeof(int));
  impl_off+=(impl_cnt_partition=sizeof(int)*(dividers));
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    implP|impl_off_edges;
    implP|impl_cnt_edges;
    implP|E;
    implP|impl_off_partition;
    implP|impl_cnt_partition;
    implP|dividers;
    impl_arrstart=CK_ALIGN(implP.size(),16);
    impl_off+=impl_arrstart;
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    implP|impl_off_edges;
    implP|impl_cnt_edges;
    implP|E;
    implP|impl_off_partition;
    implP|impl_cnt_partition;
    implP|dividers;
  }
  char *impl_buf=impl_msg->msgBuf+impl_arrstart;
  memcpy(impl_buf+impl_off_edges,edges,impl_cnt_edges);
  memcpy(impl_buf+impl_off_partition,partition,impl_cnt_partition);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckBroadcast(impl_amsg, CkIndex_WeightedArray::idx_get_graph_marshall3(),0);
}

// Entry point registration function
int CkIndex_WeightedArray::reg_get_graph_marshall3() {
  int epidx = CkRegisterEp("get_graph(const LongEdge *edges, int E, const int *partition, int dividers)",
      reinterpret_cast<CkCallFnPtr>(_call_get_graph_marshall3), CkMarshallMsg::__idx, __idx, 0+CK_EP_NOKEEP);
  CkRegisterMarshallUnpackFn(epidx, _callmarshall_get_graph_marshall3);
  CkRegisterMessagePupFn(epidx, _marshallmessagepup_get_graph_marshall3);

  return epidx;
}

void CkIndex_WeightedArray::_call_get_graph_marshall3(void* impl_msg, void* impl_obj_void)
{
  WeightedArray* impl_obj = static_cast<WeightedArray*>(impl_obj_void);
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const LongEdge *edges, int E, const int *partition, int dividers*/
  PUP::fromMem implP(impl_buf);
  int impl_off_edges, impl_cnt_edges;
  implP|impl_off_edges;
  implP|impl_cnt_edges;
  PUP::detail::TemporaryObjectHolder<int> E;
  implP|E;
  int impl_off_partition, impl_cnt_partition;
  implP|impl_off_partition;
  implP|impl_cnt_partition;
  PUP::detail::TemporaryObjectHolder<int> dividers;
  implP|dividers;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  LongEdge *edges=(LongEdge *)(impl_buf+impl_off_edges);
  int *partition=(int *)(impl_buf+impl_off_partition);
  impl_obj->get_graph(edges, std::move(E.t), partition, std::move(dividers.t));
}
int CkIndex_WeightedArray::_callmarshall_get_graph_marshall3(char* impl_buf, void* impl_obj_void) {
  WeightedArray* impl_obj = static_cast<WeightedArray*>(impl_obj_void);
  envelope *env = UsrToEnv(impl_buf);
  /*Unmarshall pup'd fields: const LongEdge *edges, int E, const int *partition, int dividers*/
  PUP::fromMem implP(impl_buf);
  int impl_off_edges, impl_cnt_edges;
  implP|impl_off_edges;
  implP|impl_cnt_edges;
  PUP::detail::TemporaryObjectHolder<int> E;
  implP|E;
  int impl_off_partition, impl_cnt_partition;
  implP|impl_off_partition;
  implP|impl_cnt_partition;
  PUP::detail::TemporaryObjectHolder<int> dividers;
  implP|dividers;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  LongEdge *edges=(LongEdge *)(impl_buf+impl_off_edges);
  int *partition=(int *)(impl_buf+impl_off_partition);
  impl_obj->get_graph(edges, std::move(E.t), partition, std::move(dividers.t));
  return implP.size();
}
void CkIndex_WeightedArray::_marshallmessagepup_get_graph_marshall3(PUP::er &implDestP,void *impl_msg) {
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const LongEdge *edges, int E, const int *partition, int dividers*/
  PUP::fromMem implP(impl_buf);
  int impl_off_edges, impl_cnt_edges;
  implP|impl_off_edges;
  implP|impl_cnt_edges;
  PUP::detail::TemporaryObjectHolder<int> E;
  implP|E;
  int impl_off_partition, impl_cnt_partition;
  implP|impl_off_partition;
  implP|impl_cnt_partition;
  PUP::detail::TemporaryObjectHolder<int> dividers;
  implP|dividers;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  LongEdge *edges=(LongEdge *)(impl_buf+impl_off_edges);
  int *partition=(int *)(impl_buf+impl_off_partition);
  if (implDestP.hasComments()) implDestP.comment("edges");
  implDestP.synchronize(PUP::sync_begin_array);
  for (int impl_i=0;impl_i*(sizeof(*edges))<impl_cnt_edges;impl_i++) {
    implDestP.synchronize(PUP::sync_item);
    implDestP|edges[impl_i];
  }
  implDestP.synchronize(PUP::sync_end_array);
  if (implDestP.hasComments()) implDestP.comment("E");
  implDestP|E;
  if (implDestP.hasComments()) implDestP.comment("partition");
  implDestP.synchronize(PUP::sync_begin_array);
  for (int impl_i=0;impl_i*(sizeof(*partition))<impl_cnt_partition;impl_i++) {
    implDestP.synchronize(PUP::sync_item);
    implDestP|partition[impl_i];
  }
  implDestP.synchronize(PUP::sync_end_array);
  if (implDestP.hasComments()) implDestP.comment("dividers");
  implDestP|dividers;
}
PUPable_def(SINGLE_ARG(Closure_WeightedArray::get_graph_3_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void update_distances(const std::pair<int,int> &new_vertex_and_distance);
 */
void CProxy_WeightedArray::update_distances(const std::pair<int,int> &new_vertex_and_distance, const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  //Marshall: const std::pair<int,int> &new_vertex_and_distance
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)new_vertex_and_distance;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)new_vertex_and_distance;
  }
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckBroadcast(impl_amsg, CkIndex_WeightedArray::idx_update_distances_marshall4(),0);
}

// Entry point registration function
int CkIndex_WeightedArray::reg_update_distances_marshall4() {
  int epidx = CkRegisterEp("update_distances(const std::pair<int,int> &new_vertex_and_distance)",
      reinterpret_cast<CkCallFnPtr>(_call_update_distances_marshall4), CkMarshallMsg::__idx, __idx, 0+CK_EP_NOKEEP);
  CkRegisterMarshallUnpackFn(epidx, _callmarshall_update_distances_marshall4);
  CkRegisterMessagePupFn(epidx, _marshallmessagepup_update_distances_marshall4);

  return epidx;
}

void CkIndex_WeightedArray::_call_update_distances_marshall4(void* impl_msg, void* impl_obj_void)
{
  WeightedArray* impl_obj = static_cast<WeightedArray*>(impl_obj_void);
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const std::pair<int,int> &new_vertex_and_distance*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<std::pair<int,int>> new_vertex_and_distance;
  implP|new_vertex_and_distance;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->update_distances(std::move(new_vertex_and_distance.t));
}
int CkIndex_WeightedArray::_callmarshall_update_distances_marshall4(char* impl_buf, void* impl_obj_void) {
  WeightedArray* impl_obj = static_cast<WeightedArray*>(impl_obj_void);
  envelope *env = UsrToEnv(impl_buf);
  /*Unmarshall pup'd fields: const std::pair<int,int> &new_vertex_and_distance*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<std::pair<int,int>> new_vertex_and_distance;
  implP|new_vertex_and_distance;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->update_distances(std::move(new_vertex_and_distance.t));
  return implP.size();
}
void CkIndex_WeightedArray::_marshallmessagepup_update_distances_marshall4(PUP::er &implDestP,void *impl_msg) {
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const std::pair<int,int> &new_vertex_and_distance*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<std::pair<int,int>> new_vertex_and_distance;
  implP|new_vertex_and_distance;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  if (implDestP.hasComments()) implDestP.comment("new_vertex_and_distance");
  implDestP|new_vertex_and_distance;
}
PUPable_def(SINGLE_ARG(Closure_WeightedArray::update_distances_4_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void check_buffer();
 */
void CProxy_WeightedArray::check_buffer(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckBroadcast(impl_amsg, CkIndex_WeightedArray::idx_check_buffer_void(),0);
}

// Entry point registration function
int CkIndex_WeightedArray::reg_check_buffer_void() {
  int epidx = CkRegisterEp("check_buffer()",
      reinterpret_cast<CkCallFnPtr>(_call_check_buffer_void), 0, __idx, 0);
  return epidx;
}

void CkIndex_WeightedArray::_call_check_buffer_void(void* impl_msg, void* impl_obj_void)
{
  WeightedArray* impl_obj = static_cast<WeightedArray*>(impl_obj_void);
  impl_obj->check_buffer();
  if(UsrToEnv(impl_msg)->isVarSysMsg() == 0)
    CkFreeSysMsg(impl_msg);
}
PUPable_def(SINGLE_ARG(Closure_WeightedArray::check_buffer_5_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void keep_going();
 */
void CProxy_WeightedArray::keep_going(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckBroadcast(impl_amsg, CkIndex_WeightedArray::idx_keep_going_void(),0);
}

// Entry point registration function
int CkIndex_WeightedArray::reg_keep_going_void() {
  int epidx = CkRegisterEp("keep_going()",
      reinterpret_cast<CkCallFnPtr>(_call_keep_going_void), 0, __idx, 0);
  return epidx;
}

void CkIndex_WeightedArray::_call_keep_going_void(void* impl_msg, void* impl_obj_void)
{
  WeightedArray* impl_obj = static_cast<WeightedArray*>(impl_obj_void);
  impl_obj->keep_going();
  if(UsrToEnv(impl_msg)->isVarSysMsg() == 0)
    CkFreeSysMsg(impl_msg);
}
PUPable_def(SINGLE_ARG(Closure_WeightedArray::keep_going_6_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void print_distances();
 */
void CProxy_WeightedArray::print_distances(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckBroadcast(impl_amsg, CkIndex_WeightedArray::idx_print_distances_void(),0);
}

// Entry point registration function
int CkIndex_WeightedArray::reg_print_distances_void() {
  int epidx = CkRegisterEp("print_distances()",
      reinterpret_cast<CkCallFnPtr>(_call_print_distances_void), 0, __idx, 0);
  return epidx;
}

void CkIndex_WeightedArray::_call_print_distances_void(void* impl_msg, void* impl_obj_void)
{
  WeightedArray* impl_obj = static_cast<WeightedArray*>(impl_obj_void);
  impl_obj->print_distances();
  if(UsrToEnv(impl_msg)->isVarSysMsg() == 0)
    CkFreeSysMsg(impl_msg);
}
PUPable_def(SINGLE_ARG(Closure_WeightedArray::print_distances_7_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: WeightedArray(CkMigrateMessage* impl_msg);
 */

// Entry point registration function
int CkIndex_WeightedArray::reg_WeightedArray_CkMigrateMessage() {
  int epidx = CkRegisterEp("WeightedArray(CkMigrateMessage* impl_msg)",
      reinterpret_cast<CkCallFnPtr>(_call_WeightedArray_CkMigrateMessage), 0, __idx, 0);
  return epidx;
}

void CkIndex_WeightedArray::_call_WeightedArray_CkMigrateMessage(void* impl_msg, void* impl_obj_void)
{
  call_migration_constructor<WeightedArray> c = impl_obj_void;
  c((CkMigrateMessage*)impl_msg);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: WeightedArray();
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void initiate_pointers();
 */
void CProxySection_WeightedArray::initiate_pointers(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_initiate_pointers_void(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
 */
void CProxySection_WeightedArray::get_graph(const LongEdge *edges, int E, const int *partition, int dividers, const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  //Marshall: const LongEdge *edges, int E, const int *partition, int dividers
  int impl_off=0;
  int impl_arrstart=0;
  int impl_off_edges, impl_cnt_edges;
  impl_off_edges=impl_off=CK_ALIGN(impl_off,sizeof(LongEdge));
  impl_off+=(impl_cnt_edges=sizeof(LongEdge)*(E));
  int impl_off_partition, impl_cnt_partition;
  impl_off_partition=impl_off=CK_ALIGN(impl_off,sizeof(int));
  impl_off+=(impl_cnt_partition=sizeof(int)*(dividers));
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    implP|impl_off_edges;
    implP|impl_cnt_edges;
    implP|E;
    implP|impl_off_partition;
    implP|impl_cnt_partition;
    implP|dividers;
    impl_arrstart=CK_ALIGN(implP.size(),16);
    impl_off+=impl_arrstart;
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    implP|impl_off_edges;
    implP|impl_cnt_edges;
    implP|E;
    implP|impl_off_partition;
    implP|impl_cnt_partition;
    implP|dividers;
  }
  char *impl_buf=impl_msg->msgBuf+impl_arrstart;
  memcpy(impl_buf+impl_off_edges,edges,impl_cnt_edges);
  memcpy(impl_buf+impl_off_partition,partition,impl_cnt_partition);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_get_graph_marshall3(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void update_distances(const std::pair<int,int> &new_vertex_and_distance);
 */
void CProxySection_WeightedArray::update_distances(const std::pair<int,int> &new_vertex_and_distance, const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  //Marshall: const std::pair<int,int> &new_vertex_and_distance
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)new_vertex_and_distance;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)new_vertex_and_distance;
  }
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_update_distances_marshall4(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void check_buffer();
 */
void CProxySection_WeightedArray::check_buffer(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_check_buffer_void(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void keep_going();
 */
void CProxySection_WeightedArray::keep_going(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_keep_going_void(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void print_distances();
 */
void CProxySection_WeightedArray::print_distances(const CkEntryOptions *impl_e_opts) 
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(ForArrayEltMsg);
  CkArrayMessage *impl_amsg=(CkArrayMessage *)impl_msg;
  impl_amsg->array_setIfNotThere(CkArray_IfNotThere_buffer);
  ckSend(impl_amsg, CkIndex_WeightedArray::idx_print_distances_void(),0);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: WeightedArray(CkMigrateMessage* impl_msg);
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
void CkIndex_WeightedArray::__register(const char *s, size_t size) {
  __idx = CkRegisterChare(s, size, TypeArray);
  CkRegisterArrayDimensions(__idx, 1);
  CkRegisterBase(__idx, CkIndex_ArrayElement::__idx);
  // REG: WeightedArray();
  idx_WeightedArray_void();
  CkRegisterDefaultCtor(__idx, idx_WeightedArray_void());

  // REG: void initiate_pointers();
  idx_initiate_pointers_void();

  // REG: void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
  idx_get_graph_marshall3();

  // REG: void update_distances(const std::pair<int,int> &new_vertex_and_distance);
  idx_update_distances_marshall4();

  // REG: void check_buffer();
  idx_check_buffer_void();

  // REG: void keep_going();
  idx_keep_going_void();

  // REG: void print_distances();
  idx_print_distances_void();

  // REG: WeightedArray(CkMigrateMessage* impl_msg);
  idx_WeightedArray_CkMigrateMessage();
  CkRegisterMigCtor(__idx, idx_WeightedArray_CkMigrateMessage());

}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
void _registerweighted_htram_smp(void)
{
  static int _done = 0; if(_done) return; _done = 1;

  _registerhtram_group();

  CkRegisterReadonly("mainProxy","CProxy_Main",sizeof(mainProxy),(void *) &mainProxy,__xlater_roPup_mainProxy);

  CkRegisterReadonly("arr","CProxy_WeightedArray",sizeof(arr),(void *) &arr,__xlater_roPup_arr);

  CkRegisterReadonly("N","int",sizeof(N),(void *) &N,__xlater_roPup_N);

  CkRegisterReadonly("V","int",sizeof(V),(void *) &V,__xlater_roPup_V);

  CkRegisterReadonly("imax","int",sizeof(imax),(void *) &imax,__xlater_roPup_imax);

  CkRegisterReadonly("average","int",sizeof(average),(void *) &average,__xlater_roPup_average);

  CkRegisterReadonly("buffer_size","int",sizeof(buffer_size),(void *) &buffer_size,__xlater_roPup_buffer_size);

  CkRegisterReadonly("flush_timer","double",sizeof(flush_timer),(void *) &flush_timer,__xlater_roPup_flush_timer);

  CkRegisterReadonly("enable_buffer_flushing","bool",sizeof(enable_buffer_flushing),(void *) &enable_buffer_flushing,__xlater_roPup_enable_buffer_flushing);

/* REG: mainchare Main: Chare{
Main(CkArgMsg* impl_msg);
void begin(int result);
void print();
void done(int result);
void check_buffer_done(const int *msg_stats, int N);
};
*/
  CkIndex_Main::__register("Main", sizeof(Main));

/* REG: array WeightedArray: ArrayElement{
WeightedArray();
void initiate_pointers();
void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
void update_distances(const std::pair<int,int> &new_vertex_and_distance);
void check_buffer();
void keep_going();
void print_distances();
WeightedArray(CkMigrateMessage* impl_msg);
};
*/
  CkIndex_WeightedArray::__register("WeightedArray", sizeof(WeightedArray));

}
extern "C" void CkRegisterMainModule(void) {
  _registerweighted_htram_smp();
}
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
template <>
void CBase_Main::virtual_pup(PUP::er &p) {
    recursive_pup<Main>(dynamic_cast<Main*>(this), p);
}
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
template <>
void CBase_WeightedArray::virtual_pup(PUP::er &p) {
    recursive_pup<WeightedArray>(dynamic_cast<WeightedArray*>(this), p);
}
#endif /* CK_TEMPLATES_ONLY */
