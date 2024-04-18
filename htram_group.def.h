




/* ---------------- method closures -------------- */
#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_HTram::insertValue_3_closure : public SDAG::Closure {
            std::pair<int,int> value;
            int destPE;


      insertValue_3_closure() {
        init();
      }
      insertValue_3_closure(CkMigrateMessage*) {
        init();
      }
            std::pair<int,int> & getP0() { return value;}
            int & getP1() { return destPE;}
      void pup(PUP::er& __p) {
        __p | value;
        __p | destPE;
        packClosure(__p);
      }
      virtual ~insertValue_3_closure() {
      }
      PUPable_decl(SINGLE_ARG(insertValue_3_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY

    struct Closure_HTram::tflush_4_closure : public SDAG::Closure {
      

      tflush_4_closure() {
        init();
      }
      tflush_4_closure(CkMigrateMessage*) {
        init();
      }
            void pup(PUP::er& __p) {
        packClosure(__p);
      }
      virtual ~tflush_4_closure() {
      }
      PUPable_decl(SINGLE_ARG(tflush_4_closure));
    };
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */


/* ---------------- method closures -------------- */
#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */


/* ---------------- method closures -------------- */
#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */


/* DEFS: readonly CProxy_HTram tram_proxy;
 */
extern CProxy_HTram tram_proxy;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_tram_proxy(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|tram_proxy;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly CProxy_HTramRecv nodeGrpProxy;
 */
extern CProxy_HTramRecv nodeGrpProxy;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_nodeGrpProxy(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|nodeGrpProxy;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: readonly CProxy_HTramNodeGrp srcNodeGrpProxy;
 */
extern CProxy_HTramNodeGrp srcNodeGrpProxy;
#ifndef CK_TEMPLATES_ONLY
extern "C" void __xlater_roPup_srcNodeGrpProxy(void *_impl_pup_er) {
  PUP::er &_impl_p=*(PUP::er *)_impl_pup_er;
  _impl_p|srcNodeGrpProxy;
}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: message HTramMessage{
itemT* buffer;
int next;
}
;
 */
#ifndef CK_TEMPLATES_ONLY
void *CMessage_HTramMessage::operator new(size_t s){
  return HTramMessage::alloc(__idx, s, 0, 0, GroupDepNum{});
}
void *CMessage_HTramMessage::operator new(size_t s, int* sz){
  return HTramMessage::alloc(__idx, s, sz, 0, GroupDepNum{});
}
void *CMessage_HTramMessage::operator new(size_t s, int* sz,const int pb){
  return HTramMessage::alloc(__idx, s, sz, pb, GroupDepNum{});
}
void *CMessage_HTramMessage::operator new(size_t s, int* sz,const int pb, const GroupDepNum groupDepNum){
  return HTramMessage::alloc(__idx, s, sz, pb, groupDepNum);
}
void *CMessage_HTramMessage::operator new(size_t s, const int p) {
  return HTramMessage::alloc(__idx, s, 0, p, GroupDepNum{});
}
void *CMessage_HTramMessage::operator new(size_t s, const int p, const GroupDepNum groupDepNum) {
  return HTramMessage::alloc(__idx, s, 0, p, groupDepNum);
}
void* CMessage_HTramMessage::alloc(int msgnum, size_t sz, int *sizes, int pb, GroupDepNum groupDepNum) {
  CkpvAccess(_offsets)[0] = ALIGN_DEFAULT(sz);
  return CkAllocMsg(msgnum, CkpvAccess(_offsets)[0], pb, groupDepNum);
}
CMessage_HTramMessage::CMessage_HTramMessage() {
HTramMessage *newmsg = (HTramMessage *)this;
}
void CMessage_HTramMessage::dealloc(void *p) {
  CkFreeMsg(p);
}
void* CMessage_HTramMessage::pack(HTramMessage *msg) {
  return (void *) msg;
}
HTramMessage* CMessage_HTramMessage::unpack(void* buf) {
  HTramMessage *msg = (HTramMessage *) buf;
  return msg;
}
int CMessage_HTramMessage::__idx=0;
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: message HTramNodeMessage{
std::pair<int,int>* buffer;
int* offset;
}
;
 */
#ifndef CK_TEMPLATES_ONLY
void *CMessage_HTramNodeMessage::operator new(size_t s){
  return HTramNodeMessage::alloc(__idx, s, 0, 0, GroupDepNum{});
}
void *CMessage_HTramNodeMessage::operator new(size_t s, int* sz){
  return HTramNodeMessage::alloc(__idx, s, sz, 0, GroupDepNum{});
}
void *CMessage_HTramNodeMessage::operator new(size_t s, int* sz,const int pb){
  return HTramNodeMessage::alloc(__idx, s, sz, pb, GroupDepNum{});
}
void *CMessage_HTramNodeMessage::operator new(size_t s, int* sz,const int pb, const GroupDepNum groupDepNum){
  return HTramNodeMessage::alloc(__idx, s, sz, pb, groupDepNum);
}
void *CMessage_HTramNodeMessage::operator new(size_t s, const int p) {
  return HTramNodeMessage::alloc(__idx, s, 0, p, GroupDepNum{});
}
void *CMessage_HTramNodeMessage::operator new(size_t s, const int p, const GroupDepNum groupDepNum) {
  return HTramNodeMessage::alloc(__idx, s, 0, p, groupDepNum);
}
void* CMessage_HTramNodeMessage::alloc(int msgnum, size_t sz, int *sizes, int pb, GroupDepNum groupDepNum) {
  CkpvAccess(_offsets)[0] = ALIGN_DEFAULT(sz);
  return CkAllocMsg(msgnum, CkpvAccess(_offsets)[0], pb, groupDepNum);
}
CMessage_HTramNodeMessage::CMessage_HTramNodeMessage() {
HTramNodeMessage *newmsg = (HTramNodeMessage *)this;
}
void CMessage_HTramNodeMessage::dealloc(void *p) {
  CkFreeMsg(p);
}
void* CMessage_HTramNodeMessage::pack(HTramNodeMessage *msg) {
  return (void *) msg;
}
HTramNodeMessage* CMessage_HTramNodeMessage::unpack(void* buf) {
  HTramNodeMessage *msg = (HTramNodeMessage *) buf;
  return msg;
}
int CMessage_HTramNodeMessage::__idx=0;
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: group HTram: IrrGroup{
HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
HTram(const CkGroupID &gid, const CkCallback &cb);
void insertValue(const std::pair<int,int> &value, int destPE);
void tflush();
void receivePerPE(HTramNodeMessage* impl_msg);
};
 */
#ifndef CK_TEMPLATES_ONLY
 int CkIndex_HTram::__idx=0;
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTram(const CkGroupID &gid, const CkCallback &cb);
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void insertValue(const std::pair<int,int> &value, int destPE);
 */
void CProxyElement_HTram::insertValue(const std::pair<int,int> &value, int destPE, const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  //Marshall: const std::pair<int,int> &value, int destPE
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
  }
  if (ckIsDelegated()) {
     CkGroupMsgPrep(CkIndex_HTram::idx_insertValue_marshall3(), impl_msg, ckGetGroupID());
     ckDelegatedTo()->GroupSend(ckDelegatedPtr(),CkIndex_HTram::idx_insertValue_marshall3(), impl_msg, ckGetGroupPe(), ckGetGroupID());
  } else {
    CkSendMsgBranch(CkIndex_HTram::idx_insertValue_marshall3(), impl_msg, ckGetGroupPe(), ckGetGroupID(),0);
  }
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void tflush();
 */
void CProxyElement_HTram::tflush(const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  if (ckIsDelegated()) {
     CkGroupMsgPrep(CkIndex_HTram::idx_tflush_void(), impl_msg, ckGetGroupID());
     ckDelegatedTo()->GroupSend(ckDelegatedPtr(),CkIndex_HTram::idx_tflush_void(), impl_msg, ckGetGroupPe(), ckGetGroupID());
  } else {
    CkSendMsgBranch(CkIndex_HTram::idx_tflush_void(), impl_msg, ckGetGroupPe(), ckGetGroupID(),0);
  }
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void receivePerPE(HTramNodeMessage* impl_msg);
 */
void CProxyElement_HTram::receivePerPE(HTramNodeMessage* impl_msg)
{
  ckCheck();
  if (ckIsDelegated()) {
     CkGroupMsgPrep(CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg, ckGetGroupID());
     ckDelegatedTo()->GroupSend(ckDelegatedPtr(),CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg, ckGetGroupPe(), ckGetGroupID());
  } else {
    CkSendMsgBranch(CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg, ckGetGroupPe(), ckGetGroupID(),0);
  }
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
 */
CkGroupID CProxy_HTram::ckNew(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3, const CkEntryOptions *impl_e_opts)
{
  //Marshall: const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkGroupID>::type>::type &)impl_noname_0;
    implP|impl_noname_1;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<bool>::type>::type &)impl_noname_2;
    implP|impl_noname_3;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkGroupID>::type>::type &)impl_noname_0;
    implP|impl_noname_1;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<bool>::type>::type &)impl_noname_2;
    implP|impl_noname_3;
  }
  UsrToEnv(impl_msg)->setMsgtype(BocInitMsg);
  CkGroupID gId = CkCreateGroup(CkIndex_HTram::__idx, CkIndex_HTram::idx_HTram_marshall1(), impl_msg);
  return gId;
}
  CProxy_HTram::CProxy_HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3, const CkEntryOptions *impl_e_opts)
{
  //Marshall: const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkGroupID>::type>::type &)impl_noname_0;
    implP|impl_noname_1;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<bool>::type>::type &)impl_noname_2;
    implP|impl_noname_3;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkGroupID>::type>::type &)impl_noname_0;
    implP|impl_noname_1;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<bool>::type>::type &)impl_noname_2;
    implP|impl_noname_3;
  }
  UsrToEnv(impl_msg)->setMsgtype(BocInitMsg);
  ckSetGroupID(CkCreateGroup(CkIndex_HTram::__idx, CkIndex_HTram::idx_HTram_marshall1(), impl_msg));
}

// Entry point registration function
int CkIndex_HTram::reg_HTram_marshall1() {
  int epidx = CkRegisterEp("HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3)",
      reinterpret_cast<CkCallFnPtr>(_call_HTram_marshall1), CkMarshallMsg::__idx, __idx, 0+CK_EP_NOKEEP);
  CkRegisterMarshallUnpackFn(epidx, _callmarshall_HTram_marshall1);
  CkRegisterMessagePupFn(epidx, _marshallmessagepup_HTram_marshall1);

  return epidx;
}

void CkIndex_HTram::_call_HTram_marshall1(void* impl_msg, void* impl_obj_void)
{
  HTram* impl_obj = static_cast<HTram*>(impl_obj_void);
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<CkGroupID> impl_noname_0;
  implP|impl_noname_0;
  PUP::detail::TemporaryObjectHolder<int> impl_noname_1;
  implP|impl_noname_1;
  PUP::detail::TemporaryObjectHolder<bool> impl_noname_2;
  implP|impl_noname_2;
  PUP::detail::TemporaryObjectHolder<double> impl_noname_3;
  implP|impl_noname_3;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  new (impl_obj_void) HTram(std::move(impl_noname_0.t), std::move(impl_noname_1.t), std::move(impl_noname_2.t), std::move(impl_noname_3.t));
}
int CkIndex_HTram::_callmarshall_HTram_marshall1(char* impl_buf, void* impl_obj_void) {
  HTram* impl_obj = static_cast<HTram*>(impl_obj_void);
  envelope *env = UsrToEnv(impl_buf);
  /*Unmarshall pup'd fields: const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<CkGroupID> impl_noname_0;
  implP|impl_noname_0;
  PUP::detail::TemporaryObjectHolder<int> impl_noname_1;
  implP|impl_noname_1;
  PUP::detail::TemporaryObjectHolder<bool> impl_noname_2;
  implP|impl_noname_2;
  PUP::detail::TemporaryObjectHolder<double> impl_noname_3;
  implP|impl_noname_3;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  new (impl_obj_void) HTram(std::move(impl_noname_0.t), std::move(impl_noname_1.t), std::move(impl_noname_2.t), std::move(impl_noname_3.t));
  return implP.size();
}
void CkIndex_HTram::_marshallmessagepup_HTram_marshall1(PUP::er &implDestP,void *impl_msg) {
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<CkGroupID> impl_noname_0;
  implP|impl_noname_0;
  PUP::detail::TemporaryObjectHolder<int> impl_noname_1;
  implP|impl_noname_1;
  PUP::detail::TemporaryObjectHolder<bool> impl_noname_2;
  implP|impl_noname_2;
  PUP::detail::TemporaryObjectHolder<double> impl_noname_3;
  implP|impl_noname_3;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  if (implDestP.hasComments()) implDestP.comment("impl_noname_0");
  implDestP|impl_noname_0;
  if (implDestP.hasComments()) implDestP.comment("impl_noname_1");
  implDestP|impl_noname_1;
  if (implDestP.hasComments()) implDestP.comment("impl_noname_2");
  implDestP|impl_noname_2;
  if (implDestP.hasComments()) implDestP.comment("impl_noname_3");
  implDestP|impl_noname_3;
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTram(const CkGroupID &gid, const CkCallback &cb);
 */
CkGroupID CProxy_HTram::ckNew(const CkGroupID &gid, const CkCallback &cb, const CkEntryOptions *impl_e_opts)
{
  //Marshall: const CkGroupID &gid, const CkCallback &cb
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkGroupID>::type>::type &)gid;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkCallback>::type>::type &)cb;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkGroupID>::type>::type &)gid;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkCallback>::type>::type &)cb;
  }
  UsrToEnv(impl_msg)->setMsgtype(BocInitMsg);
  CkGroupID gId = CkCreateGroup(CkIndex_HTram::__idx, CkIndex_HTram::idx_HTram_marshall2(), impl_msg);
  return gId;
}
  CProxy_HTram::CProxy_HTram(const CkGroupID &gid, const CkCallback &cb, const CkEntryOptions *impl_e_opts)
{
  //Marshall: const CkGroupID &gid, const CkCallback &cb
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkGroupID>::type>::type &)gid;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkCallback>::type>::type &)cb;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkGroupID>::type>::type &)gid;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<CkCallback>::type>::type &)cb;
  }
  UsrToEnv(impl_msg)->setMsgtype(BocInitMsg);
  ckSetGroupID(CkCreateGroup(CkIndex_HTram::__idx, CkIndex_HTram::idx_HTram_marshall2(), impl_msg));
}

// Entry point registration function
int CkIndex_HTram::reg_HTram_marshall2() {
  int epidx = CkRegisterEp("HTram(const CkGroupID &gid, const CkCallback &cb)",
      reinterpret_cast<CkCallFnPtr>(_call_HTram_marshall2), CkMarshallMsg::__idx, __idx, 0+CK_EP_NOKEEP);
  CkRegisterMarshallUnpackFn(epidx, _callmarshall_HTram_marshall2);
  CkRegisterMessagePupFn(epidx, _marshallmessagepup_HTram_marshall2);

  return epidx;
}

void CkIndex_HTram::_call_HTram_marshall2(void* impl_msg, void* impl_obj_void)
{
  HTram* impl_obj = static_cast<HTram*>(impl_obj_void);
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const CkGroupID &gid, const CkCallback &cb*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<CkGroupID> gid;
  implP|gid;
  PUP::detail::TemporaryObjectHolder<CkCallback> cb;
  implP|cb;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  new (impl_obj_void) HTram(std::move(gid.t), std::move(cb.t));
}
int CkIndex_HTram::_callmarshall_HTram_marshall2(char* impl_buf, void* impl_obj_void) {
  HTram* impl_obj = static_cast<HTram*>(impl_obj_void);
  envelope *env = UsrToEnv(impl_buf);
  /*Unmarshall pup'd fields: const CkGroupID &gid, const CkCallback &cb*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<CkGroupID> gid;
  implP|gid;
  PUP::detail::TemporaryObjectHolder<CkCallback> cb;
  implP|cb;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  new (impl_obj_void) HTram(std::move(gid.t), std::move(cb.t));
  return implP.size();
}
void CkIndex_HTram::_marshallmessagepup_HTram_marshall2(PUP::er &implDestP,void *impl_msg) {
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const CkGroupID &gid, const CkCallback &cb*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<CkGroupID> gid;
  implP|gid;
  PUP::detail::TemporaryObjectHolder<CkCallback> cb;
  implP|cb;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  if (implDestP.hasComments()) implDestP.comment("gid");
  implDestP|gid;
  if (implDestP.hasComments()) implDestP.comment("cb");
  implDestP|cb;
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void insertValue(const std::pair<int,int> &value, int destPE);
 */
void CProxy_HTram::insertValue(const std::pair<int,int> &value, int destPE, const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  //Marshall: const std::pair<int,int> &value, int destPE
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
  }
  if (ckIsDelegated()) {
     CkGroupMsgPrep(CkIndex_HTram::idx_insertValue_marshall3(), impl_msg, ckGetGroupID());
     ckDelegatedTo()->GroupBroadcast(ckDelegatedPtr(),CkIndex_HTram::idx_insertValue_marshall3(), impl_msg, ckGetGroupID());
  } else CkBroadcastMsgBranch(CkIndex_HTram::idx_insertValue_marshall3(), impl_msg, ckGetGroupID(),0);
}
void CProxy_HTram::insertValue(const std::pair<int,int> &value, int destPE, int npes, int *pes, const CkEntryOptions *impl_e_opts) {
  //Marshall: const std::pair<int,int> &value, int destPE
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
  }
  CkSendMsgBranchMulti(CkIndex_HTram::idx_insertValue_marshall3(), impl_msg, ckGetGroupID(), npes, pes,0);
}
void CProxy_HTram::insertValue(const std::pair<int,int> &value, int destPE, CmiGroup &grp, const CkEntryOptions *impl_e_opts) {
  //Marshall: const std::pair<int,int> &value, int destPE
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
  }
  CkSendMsgBranchGroup(CkIndex_HTram::idx_insertValue_marshall3(), impl_msg, ckGetGroupID(), grp,0);
}

// Entry point registration function
int CkIndex_HTram::reg_insertValue_marshall3() {
  int epidx = CkRegisterEp("insertValue(const std::pair<int,int> &value, int destPE)",
      reinterpret_cast<CkCallFnPtr>(_call_insertValue_marshall3), CkMarshallMsg::__idx, __idx, 0+CK_EP_NOKEEP);
  CkRegisterMarshallUnpackFn(epidx, _callmarshall_insertValue_marshall3);
  CkRegisterMessagePupFn(epidx, _marshallmessagepup_insertValue_marshall3);

  return epidx;
}

void CkIndex_HTram::_call_insertValue_marshall3(void* impl_msg, void* impl_obj_void)
{
  HTram* impl_obj = static_cast<HTram*>(impl_obj_void);
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const std::pair<int,int> &value, int destPE*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<std::pair<int,int>> value;
  implP|value;
  PUP::detail::TemporaryObjectHolder<int> destPE;
  implP|destPE;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->insertValue(std::move(value.t), std::move(destPE.t));
}
int CkIndex_HTram::_callmarshall_insertValue_marshall3(char* impl_buf, void* impl_obj_void) {
  HTram* impl_obj = static_cast<HTram*>(impl_obj_void);
  envelope *env = UsrToEnv(impl_buf);
  /*Unmarshall pup'd fields: const std::pair<int,int> &value, int destPE*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<std::pair<int,int>> value;
  implP|value;
  PUP::detail::TemporaryObjectHolder<int> destPE;
  implP|destPE;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  impl_obj->insertValue(std::move(value.t), std::move(destPE.t));
  return implP.size();
}
void CkIndex_HTram::_marshallmessagepup_insertValue_marshall3(PUP::er &implDestP,void *impl_msg) {
  CkMarshallMsg *impl_msg_typed=(CkMarshallMsg *)impl_msg;
  char *impl_buf=impl_msg_typed->msgBuf;
  envelope *env = UsrToEnv(impl_msg_typed);
  /*Unmarshall pup'd fields: const std::pair<int,int> &value, int destPE*/
  PUP::fromMem implP(impl_buf);
  PUP::detail::TemporaryObjectHolder<std::pair<int,int>> value;
  implP|value;
  PUP::detail::TemporaryObjectHolder<int> destPE;
  implP|destPE;
  impl_buf+=CK_ALIGN(implP.size(),16);
  /*Unmarshall arrays:*/
  if (implDestP.hasComments()) implDestP.comment("value");
  implDestP|value;
  if (implDestP.hasComments()) implDestP.comment("destPE");
  implDestP|destPE;
}
PUPable_def(SINGLE_ARG(Closure_HTram::insertValue_3_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void tflush();
 */
void CProxy_HTram::tflush(const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  if (ckIsDelegated()) {
     CkGroupMsgPrep(CkIndex_HTram::idx_tflush_void(), impl_msg, ckGetGroupID());
     ckDelegatedTo()->GroupBroadcast(ckDelegatedPtr(),CkIndex_HTram::idx_tflush_void(), impl_msg, ckGetGroupID());
  } else CkBroadcastMsgBranch(CkIndex_HTram::idx_tflush_void(), impl_msg, ckGetGroupID(),0);
}
void CProxy_HTram::tflush(int npes, int *pes, const CkEntryOptions *impl_e_opts) {
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  CkSendMsgBranchMulti(CkIndex_HTram::idx_tflush_void(), impl_msg, ckGetGroupID(), npes, pes,0);
}
void CProxy_HTram::tflush(CmiGroup &grp, const CkEntryOptions *impl_e_opts) {
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  CkSendMsgBranchGroup(CkIndex_HTram::idx_tflush_void(), impl_msg, ckGetGroupID(), grp,0);
}

// Entry point registration function
int CkIndex_HTram::reg_tflush_void() {
  int epidx = CkRegisterEp("tflush()",
      reinterpret_cast<CkCallFnPtr>(_call_tflush_void), 0, __idx, 0);
  return epidx;
}

void CkIndex_HTram::_call_tflush_void(void* impl_msg, void* impl_obj_void)
{
  HTram* impl_obj = static_cast<HTram*>(impl_obj_void);
  impl_obj->tflush();
  if(UsrToEnv(impl_msg)->isVarSysMsg() == 0)
    CkFreeSysMsg(impl_msg);
}
PUPable_def(SINGLE_ARG(Closure_HTram::tflush_4_closure))
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void receivePerPE(HTramNodeMessage* impl_msg);
 */
void CProxy_HTram::receivePerPE(HTramNodeMessage* impl_msg)
{
  ckCheck();
  if (ckIsDelegated()) {
     CkGroupMsgPrep(CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg, ckGetGroupID());
     ckDelegatedTo()->GroupBroadcast(ckDelegatedPtr(),CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg, ckGetGroupID());
  } else CkBroadcastMsgBranch(CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg, ckGetGroupID(),0);
}
void CProxy_HTram::receivePerPE(HTramNodeMessage* impl_msg, int npes, int *pes) {
  CkSendMsgBranchMulti(CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg, ckGetGroupID(), npes, pes,0);
}
void CProxy_HTram::receivePerPE(HTramNodeMessage* impl_msg, CmiGroup &grp) {
  CkSendMsgBranchGroup(CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg, ckGetGroupID(), grp,0);
}

// Entry point registration function
int CkIndex_HTram::reg_receivePerPE_HTramNodeMessage() {
  int epidx = CkRegisterEp("receivePerPE(HTramNodeMessage* impl_msg)",
      reinterpret_cast<CkCallFnPtr>(_call_receivePerPE_HTramNodeMessage), CMessage_HTramNodeMessage::__idx, __idx, 0);
  CkRegisterMessagePupFn(epidx, (CkMessagePupFn)HTramNodeMessage::ckDebugPup);
  return epidx;
}

void CkIndex_HTram::_call_receivePerPE_HTramNodeMessage(void* impl_msg, void* impl_obj_void)
{
  HTram* impl_obj = static_cast<HTram*>(impl_obj_void);
  impl_obj->receivePerPE((HTramNodeMessage*)impl_msg);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTram(const CkGroupID &gid, const CkCallback &cb);
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void insertValue(const std::pair<int,int> &value, int destPE);
 */
void CProxySection_HTram::insertValue(const std::pair<int,int> &value, int destPE, const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  //Marshall: const std::pair<int,int> &value, int destPE
  int impl_off=0;
  { //Find the size of the PUP'd data
    PUP::sizer implP;
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
    impl_off+=implP.size();
  }
  CkMarshallMsg *impl_msg=CkAllocateMarshallMsg(impl_off,impl_e_opts);
  { //Copy over the PUP'd data
    PUP::toMem implP((void *)impl_msg->msgBuf);
    //Have to cast away const-ness to get pup routine
    implP|(typename std::remove_cv<typename std::remove_reference<std::pair<int,int>>::type>::type &)value;
    implP|destPE;
  }
  if (ckIsDelegated()) {
     ckDelegatedTo()->GroupSectionSend(ckDelegatedPtr(),CkIndex_HTram::idx_insertValue_marshall3(), impl_msg, ckGetNumSections(), ckGetSectionIDs());
  } else {
    void *impl_msg_tmp;
    for (int i=0; i<ckGetNumSections(); ++i) {
       impl_msg_tmp= (i<ckGetNumSections()-1) ? CkCopyMsg((void **) &impl_msg):impl_msg;
       CkSendMsgBranchMulti(CkIndex_HTram::idx_insertValue_marshall3(), impl_msg_tmp, ckGetGroupIDn(i), ckGetNumElements(i), ckGetElements(i),0);
    }
  }
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void tflush();
 */
void CProxySection_HTram::tflush(const CkEntryOptions *impl_e_opts)
{
  ckCheck();
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  if (ckIsDelegated()) {
     ckDelegatedTo()->GroupSectionSend(ckDelegatedPtr(),CkIndex_HTram::idx_tflush_void(), impl_msg, ckGetNumSections(), ckGetSectionIDs());
  } else {
    void *impl_msg_tmp;
    for (int i=0; i<ckGetNumSections(); ++i) {
       impl_msg_tmp= (i<ckGetNumSections()-1) ? CkCopyMsg((void **) &impl_msg):impl_msg;
       CkSendMsgBranchMulti(CkIndex_HTram::idx_tflush_void(), impl_msg_tmp, ckGetGroupIDn(i), ckGetNumElements(i), ckGetElements(i),0);
    }
  }
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void receivePerPE(HTramNodeMessage* impl_msg);
 */
void CProxySection_HTram::receivePerPE(HTramNodeMessage* impl_msg)
{
  ckCheck();
  if (ckIsDelegated()) {
     ckDelegatedTo()->GroupSectionSend(ckDelegatedPtr(),CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg, ckGetNumSections(), ckGetSectionIDs());
  } else {
    void *impl_msg_tmp;
    for (int i=0; i<ckGetNumSections(); ++i) {
       impl_msg_tmp= (i<ckGetNumSections()-1) ? CkCopyMsg((void **) &impl_msg):impl_msg;
       CkSendMsgBranchMulti(CkIndex_HTram::idx_receivePerPE_HTramNodeMessage(), impl_msg_tmp, ckGetGroupIDn(i), ckGetNumElements(i), ckGetElements(i),0);
    }
  }
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
void CkIndex_HTram::__register(const char *s, size_t size) {
  __idx = CkRegisterChare(s, size, TypeGroup);
  CkRegisterBase(__idx, CkIndex_IrrGroup::__idx);
   CkRegisterGroupIrr(__idx,HTram::isIrreducible());
  // REG: HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
  idx_HTram_marshall1();

  // REG: HTram(const CkGroupID &gid, const CkCallback &cb);
  idx_HTram_marshall2();

  // REG: void insertValue(const std::pair<int,int> &value, int destPE);
  idx_insertValue_marshall3();

  // REG: void tflush();
  idx_tflush_void();

  // REG: void receivePerPE(HTramNodeMessage* impl_msg);
  idx_receivePerPE_HTramNodeMessage();

}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: nodegroup HTramNodeGrp: NodeGroup{
HTramNodeGrp();
};
 */
#ifndef CK_TEMPLATES_ONLY
 int CkIndex_HTramNodeGrp::__idx=0;
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTramNodeGrp();
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTramNodeGrp();
 */
CkGroupID CProxy_HTramNodeGrp::ckNew(const CkEntryOptions *impl_e_opts)
{
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(NodeBocInitMsg);
  CkGroupID gId = CkCreateNodeGroup(CkIndex_HTramNodeGrp::__idx, CkIndex_HTramNodeGrp::idx_HTramNodeGrp_void(), impl_msg);
  return gId;
}

// Entry point registration function
int CkIndex_HTramNodeGrp::reg_HTramNodeGrp_void() {
  int epidx = CkRegisterEp("HTramNodeGrp()",
      reinterpret_cast<CkCallFnPtr>(_call_HTramNodeGrp_void), 0, __idx, 0);
  return epidx;
}

void CkIndex_HTramNodeGrp::_call_HTramNodeGrp_void(void* impl_msg, void* impl_obj_void)
{
  HTramNodeGrp* impl_obj = static_cast<HTramNodeGrp*>(impl_obj_void);
  new (impl_obj_void) HTramNodeGrp();
  if(UsrToEnv(impl_msg)->isVarSysMsg() == 0)
    CkFreeSysMsg(impl_msg);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTramNodeGrp();
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
void CkIndex_HTramNodeGrp::__register(const char *s, size_t size) {
  __idx = CkRegisterChare(s, size, TypeGroup);
  CkRegisterBase(__idx, CkIndex_NodeGroup::__idx);
   CkRegisterGroupIrr(__idx,HTramNodeGrp::isIrreducible());
  // REG: HTramNodeGrp();
  idx_HTramNodeGrp_void();
  CkRegisterDefaultCtor(__idx, idx_HTramNodeGrp_void());

}
#endif /* CK_TEMPLATES_ONLY */

/* DEFS: nodegroup HTramRecv: NodeGroup{
HTramRecv();
void receive(HTramMessage* impl_msg);
};
 */
#ifndef CK_TEMPLATES_ONLY
 int CkIndex_HTramRecv::__idx=0;
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTramRecv();
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void receive(HTramMessage* impl_msg);
 */
void CProxyElement_HTramRecv::receive(HTramMessage* impl_msg)
{
  ckCheck();
  if (ckIsDelegated()) {
     CkNodeGroupMsgPrep(CkIndex_HTramRecv::idx_receive_HTramMessage(), impl_msg, ckGetGroupID());
     ckDelegatedTo()->NodeGroupSend(ckDelegatedPtr(),CkIndex_HTramRecv::idx_receive_HTramMessage(), impl_msg, ckGetGroupPe(), ckGetGroupID());
  } else {
    CkSendMsgNodeBranch(CkIndex_HTramRecv::idx_receive_HTramMessage(), impl_msg, ckGetGroupPe(), ckGetGroupID(),0);
  }
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTramRecv();
 */
CkGroupID CProxy_HTramRecv::ckNew(const CkEntryOptions *impl_e_opts)
{
  void *impl_msg = CkAllocSysMsg(impl_e_opts);
  UsrToEnv(impl_msg)->setMsgtype(NodeBocInitMsg);
  CkGroupID gId = CkCreateNodeGroup(CkIndex_HTramRecv::__idx, CkIndex_HTramRecv::idx_HTramRecv_void(), impl_msg);
  return gId;
}

// Entry point registration function
int CkIndex_HTramRecv::reg_HTramRecv_void() {
  int epidx = CkRegisterEp("HTramRecv()",
      reinterpret_cast<CkCallFnPtr>(_call_HTramRecv_void), 0, __idx, 0);
  return epidx;
}

void CkIndex_HTramRecv::_call_HTramRecv_void(void* impl_msg, void* impl_obj_void)
{
  HTramRecv* impl_obj = static_cast<HTramRecv*>(impl_obj_void);
  new (impl_obj_void) HTramRecv();
  if(UsrToEnv(impl_msg)->isVarSysMsg() == 0)
    CkFreeSysMsg(impl_msg);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void receive(HTramMessage* impl_msg);
 */
void CProxy_HTramRecv::receive(HTramMessage* impl_msg)
{
  ckCheck();
  if (ckIsDelegated()) {
     CkNodeGroupMsgPrep(CkIndex_HTramRecv::idx_receive_HTramMessage(), impl_msg, ckGetGroupID());
     ckDelegatedTo()->NodeGroupBroadcast(ckDelegatedPtr(),CkIndex_HTramRecv::idx_receive_HTramMessage(), impl_msg, ckGetGroupID());
  } else CkBroadcastMsgNodeBranch(CkIndex_HTramRecv::idx_receive_HTramMessage(), impl_msg, ckGetGroupID(),0);
}

// Entry point registration function
int CkIndex_HTramRecv::reg_receive_HTramMessage() {
  int epidx = CkRegisterEp("receive(HTramMessage* impl_msg)",
      reinterpret_cast<CkCallFnPtr>(_call_receive_HTramMessage), CMessage_HTramMessage::__idx, __idx, 0);
  CkRegisterMessagePupFn(epidx, (CkMessagePupFn)HTramMessage::ckDebugPup);
  return epidx;
}

void CkIndex_HTramRecv::_call_receive_HTramMessage(void* impl_msg, void* impl_obj_void)
{
  HTramRecv* impl_obj = static_cast<HTramRecv*>(impl_obj_void);
  impl_obj->receive((HTramMessage*)impl_msg);
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: HTramRecv();
 */
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
/* DEFS: void receive(HTramMessage* impl_msg);
 */
void CProxySection_HTramRecv::receive(HTramMessage* impl_msg)
{
  ckCheck();
  if (ckIsDelegated()) {
     ckDelegatedTo()->NodeGroupSectionSend(ckDelegatedPtr(),CkIndex_HTramRecv::idx_receive_HTramMessage(), impl_msg, ckGetNumSections(), ckGetSectionIDs());
  } else {
    void *impl_msg_tmp;
    for (int i=0; i<ckGetNumSections(); ++i) {
       impl_msg_tmp= (i<ckGetNumSections()-1) ? CkCopyMsg((void **) &impl_msg):impl_msg;
       CkSendMsgNodeBranchMulti(CkIndex_HTramRecv::idx_receive_HTramMessage(), impl_msg_tmp, ckGetGroupIDn(i), ckGetNumElements(i), ckGetElements(i),0);
    }
  }
}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
void CkIndex_HTramRecv::__register(const char *s, size_t size) {
  __idx = CkRegisterChare(s, size, TypeGroup);
  CkRegisterBase(__idx, CkIndex_NodeGroup::__idx);
   CkRegisterGroupIrr(__idx,HTramRecv::isIrreducible());
  // REG: HTramRecv();
  idx_HTramRecv_void();
  CkRegisterDefaultCtor(__idx, idx_HTramRecv_void());

  // REG: void receive(HTramMessage* impl_msg);
  idx_receive_HTramMessage();

}
#endif /* CK_TEMPLATES_ONLY */

#ifndef CK_TEMPLATES_ONLY
void _registerhtram_group(void)
{
  static int _done = 0; if(_done) return; _done = 1;
  CkRegisterReadonly("tram_proxy","CProxy_HTram",sizeof(tram_proxy),(void *) &tram_proxy,__xlater_roPup_tram_proxy);

  CkRegisterReadonly("nodeGrpProxy","CProxy_HTramRecv",sizeof(nodeGrpProxy),(void *) &nodeGrpProxy,__xlater_roPup_nodeGrpProxy);

  CkRegisterReadonly("srcNodeGrpProxy","CProxy_HTramNodeGrp",sizeof(srcNodeGrpProxy),(void *) &srcNodeGrpProxy,__xlater_roPup_srcNodeGrpProxy);

/* REG: message HTramMessage{
itemT* buffer;
int next;
}
;
*/
CMessage_HTramMessage::__register("HTramMessage", sizeof(HTramMessage),(CkPackFnPtr) HTramMessage::pack,(CkUnpackFnPtr) HTramMessage::unpack);

/* REG: message HTramNodeMessage{
std::pair<int,int>* buffer;
int* offset;
}
;
*/
CMessage_HTramNodeMessage::__register("HTramNodeMessage", sizeof(HTramNodeMessage),(CkPackFnPtr) HTramNodeMessage::pack,(CkUnpackFnPtr) HTramNodeMessage::unpack);

/* REG: group HTram: IrrGroup{
HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
HTram(const CkGroupID &gid, const CkCallback &cb);
void insertValue(const std::pair<int,int> &value, int destPE);
void tflush();
void receivePerPE(HTramNodeMessage* impl_msg);
};
*/
  CkIndex_HTram::__register("HTram", sizeof(HTram));

/* REG: nodegroup HTramNodeGrp: NodeGroup{
HTramNodeGrp();
};
*/
  CkIndex_HTramNodeGrp::__register("HTramNodeGrp", sizeof(HTramNodeGrp));

/* REG: nodegroup HTramRecv: NodeGroup{
HTramRecv();
void receive(HTramMessage* impl_msg);
};
*/
  CkIndex_HTramRecv::__register("HTramRecv", sizeof(HTramRecv));

}
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
template <>
void CBase_HTram::virtual_pup(PUP::er &p) {
    recursive_pup<HTram>(dynamic_cast<HTram*>(this), p);
}
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
template <>
void CBase_HTramNodeGrp::virtual_pup(PUP::er &p) {
    recursive_pup<HTramNodeGrp>(dynamic_cast<HTramNodeGrp*>(this), p);
}
#endif /* CK_TEMPLATES_ONLY */
#ifndef CK_TEMPLATES_ONLY
template <>
void CBase_HTramRecv::virtual_pup(PUP::er &p) {
    recursive_pup<HTramRecv>(dynamic_cast<HTramRecv*>(this), p);
}
#endif /* CK_TEMPLATES_ONLY */
