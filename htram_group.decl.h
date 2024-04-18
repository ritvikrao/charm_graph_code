#ifndef _DECL_htram_group_H_
#define _DECL_htram_group_H_
#include "charm++.h"
#include "envelope.h"
#include <memory>
#include "sdag.h"
/* DECLS: readonly CProxy_HTram tram_proxy;
 */

/* DECLS: readonly CProxy_HTramRecv nodeGrpProxy;
 */

/* DECLS: readonly CProxy_HTramNodeGrp srcNodeGrpProxy;
 */

#ifndef GROUPDEPNUM_DECLARED
# define GROUPDEPNUM_DECLARED
struct GroupDepNum
{
  int groupDepNum;
  explicit GroupDepNum(int g = 0) : groupDepNum{g} { }
  operator int() const { return groupDepNum; }
};
#endif
/* DECLS: message HTramMessage{
itemT* buffer;
int next;
}
;
 */
class HTramMessage;
class CMessage_HTramMessage:public CkMessage{
  public:
    static int __idx;
    void* operator new(size_t, void*p) { return p; }
    void* operator new(size_t);
    void* operator new(size_t, int*, const int);
    void* operator new(size_t, int*, const int, const GroupDepNum);
    void* operator new(size_t, int*);
#if CMK_MULTIPLE_DELETE
    void operator delete(void*p, void*){dealloc(p);}
    void operator delete(void*p){dealloc(p);}
    void operator delete(void*p, int*, const int){dealloc(p);}
    void operator delete(void*p, int*, const int, const GroupDepNum){dealloc(p);}
    void operator delete(void*p, int*){dealloc(p);}
#endif
    void operator delete(void*p, size_t){dealloc(p);}
    static void* alloc(int,size_t, int*, int, GroupDepNum);
    static void dealloc(void *p);
    CMessage_HTramMessage();
    static void *pack(HTramMessage *p);
    static HTramMessage* unpack(void* p);
    void *operator new(size_t, const int);
    void *operator new(size_t, const int, const GroupDepNum);
#if CMK_MULTIPLE_DELETE
    void operator delete(void *p, const int){dealloc(p);}
    void operator delete(void *p, const int, const GroupDepNum){dealloc(p);}
#endif
    static void __register(const char *s, size_t size, CkPackFnPtr pack, CkUnpackFnPtr unpack) {
      __idx = CkRegisterMsg(s, pack, unpack, dealloc, size);
    }
};

#ifndef GROUPDEPNUM_DECLARED
# define GROUPDEPNUM_DECLARED
struct GroupDepNum
{
  int groupDepNum;
  explicit GroupDepNum(int g = 0) : groupDepNum{g} { }
  operator int() const { return groupDepNum; }
};
#endif
/* DECLS: message HTramNodeMessage{
std::pair<int,int>* buffer;
int* offset;
}
;
 */
class HTramNodeMessage;
class CMessage_HTramNodeMessage:public CkMessage{
  public:
    static int __idx;
    void* operator new(size_t, void*p) { return p; }
    void* operator new(size_t);
    void* operator new(size_t, int*, const int);
    void* operator new(size_t, int*, const int, const GroupDepNum);
    void* operator new(size_t, int*);
#if CMK_MULTIPLE_DELETE
    void operator delete(void*p, void*){dealloc(p);}
    void operator delete(void*p){dealloc(p);}
    void operator delete(void*p, int*, const int){dealloc(p);}
    void operator delete(void*p, int*, const int, const GroupDepNum){dealloc(p);}
    void operator delete(void*p, int*){dealloc(p);}
#endif
    void operator delete(void*p, size_t){dealloc(p);}
    static void* alloc(int,size_t, int*, int, GroupDepNum);
    static void dealloc(void *p);
    CMessage_HTramNodeMessage();
    static void *pack(HTramNodeMessage *p);
    static HTramNodeMessage* unpack(void* p);
    void *operator new(size_t, const int);
    void *operator new(size_t, const int, const GroupDepNum);
#if CMK_MULTIPLE_DELETE
    void operator delete(void *p, const int){dealloc(p);}
    void operator delete(void *p, const int, const GroupDepNum){dealloc(p);}
#endif
    static void __register(const char *s, size_t size, CkPackFnPtr pack, CkUnpackFnPtr unpack) {
      __idx = CkRegisterMsg(s, pack, unpack, dealloc, size);
    }
};

/* DECLS: group HTram: IrrGroup{
HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
HTram(const CkGroupID &gid, const CkCallback &cb);
void insertValue(const std::pair<int,int> &value, int destPE);
void tflush();
void receivePerPE(HTramNodeMessage* impl_msg);
};
 */
 class HTram;
 class CkIndex_HTram;
 class CProxy_HTram;
 class CProxyElement_HTram;
 class CProxySection_HTram;
/* --------------- index object ------------------ */
class CkIndex_HTram:public CkIndex_IrrGroup{
  public:
    typedef HTram local_t;
    typedef CkIndex_HTram index_t;
    typedef CProxy_HTram proxy_t;
    typedef CProxyElement_HTram element_t;
    typedef CProxySection_HTram section_t;

    static int __idx;
    static void __register(const char *s, size_t size);
    /* DECLS: HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
     */
    // Entry point registration at startup
    
    static int reg_HTram_marshall1();
    // Entry point index lookup
    
    inline static int idx_HTram_marshall1() {
      static int epidx = reg_HTram_marshall1();
      return epidx;
    }

    
    static int ckNew(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3) { return idx_HTram_marshall1(); }
    
    static void _call_HTram_marshall1(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_HTram_marshall1(void* impl_msg, void* impl_obj);
    
    static int _callmarshall_HTram_marshall1(char* impl_buf, void* impl_obj_void);
    
    static void _marshallmessagepup_HTram_marshall1(PUP::er &p,void *msg);
    /* DECLS: HTram(const CkGroupID &gid, const CkCallback &cb);
     */
    // Entry point registration at startup
    
    static int reg_HTram_marshall2();
    // Entry point index lookup
    
    inline static int idx_HTram_marshall2() {
      static int epidx = reg_HTram_marshall2();
      return epidx;
    }

    
    static int ckNew(const CkGroupID &gid, const CkCallback &cb) { return idx_HTram_marshall2(); }
    
    static void _call_HTram_marshall2(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_HTram_marshall2(void* impl_msg, void* impl_obj);
    
    static int _callmarshall_HTram_marshall2(char* impl_buf, void* impl_obj_void);
    
    static void _marshallmessagepup_HTram_marshall2(PUP::er &p,void *msg);
    /* DECLS: void insertValue(const std::pair<int,int> &value, int destPE);
     */
    // Entry point registration at startup
    
    static int reg_insertValue_marshall3();
    // Entry point index lookup
    
    inline static int idx_insertValue_marshall3() {
      static int epidx = reg_insertValue_marshall3();
      return epidx;
    }

    
    inline static int idx_insertValue(void (HTram::*)(const std::pair<int,int> &value, int destPE) ) {
      return idx_insertValue_marshall3();
    }


    
    static int insertValue(const std::pair<int,int> &value, int destPE) { return idx_insertValue_marshall3(); }
    
    static void _call_insertValue_marshall3(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_insertValue_marshall3(void* impl_msg, void* impl_obj);
    
    static int _callmarshall_insertValue_marshall3(char* impl_buf, void* impl_obj_void);
    
    static void _marshallmessagepup_insertValue_marshall3(PUP::er &p,void *msg);
    /* DECLS: void tflush();
     */
    // Entry point registration at startup
    
    static int reg_tflush_void();
    // Entry point index lookup
    
    inline static int idx_tflush_void() {
      static int epidx = reg_tflush_void();
      return epidx;
    }

    
    inline static int idx_tflush(void (HTram::*)() ) {
      return idx_tflush_void();
    }


    
    static int tflush() { return idx_tflush_void(); }
    
    static void _call_tflush_void(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_tflush_void(void* impl_msg, void* impl_obj);
    /* DECLS: void receivePerPE(HTramNodeMessage* impl_msg);
     */
    // Entry point registration at startup
    
    static int reg_receivePerPE_HTramNodeMessage();
    // Entry point index lookup
    
    inline static int idx_receivePerPE_HTramNodeMessage() {
      static int epidx = reg_receivePerPE_HTramNodeMessage();
      return epidx;
    }

    
    inline static int idx_receivePerPE(void (HTram::*)(HTramNodeMessage* impl_msg) ) {
      return idx_receivePerPE_HTramNodeMessage();
    }


    
    static int receivePerPE(HTramNodeMessage* impl_msg) { return idx_receivePerPE_HTramNodeMessage(); }
    
    static void _call_receivePerPE_HTramNodeMessage(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_receivePerPE_HTramNodeMessage(void* impl_msg, void* impl_obj);
};
/* --------------- element proxy ------------------ */
class CProxyElement_HTram: public CProxyElement_IrrGroup{
  public:
    typedef HTram local_t;
    typedef CkIndex_HTram index_t;
    typedef CProxy_HTram proxy_t;
    typedef CProxyElement_HTram element_t;
    typedef CProxySection_HTram section_t;


    /* TRAM aggregators */

    CProxyElement_HTram(void) {
    }
    CProxyElement_HTram(const IrrGroup *g) : CProxyElement_IrrGroup(g){
    }
    CProxyElement_HTram(CkGroupID _gid,int _onPE,CK_DELCTOR_PARAM) : CProxyElement_IrrGroup(_gid,_onPE,CK_DELCTOR_ARGS){
    }
    CProxyElement_HTram(CkGroupID _gid,int _onPE) : CProxyElement_IrrGroup(_gid,_onPE){
    }

    int ckIsDelegated(void) const
    { return CProxyElement_IrrGroup::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxyElement_IrrGroup::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxyElement_IrrGroup::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxyElement_IrrGroup::ckDelegatedIdx(); }
inline void ckCheck(void) const {CProxyElement_IrrGroup::ckCheck();}
CkChareID ckGetChareID(void) const
   {return CProxyElement_IrrGroup::ckGetChareID();}
CkGroupID ckGetGroupID(void) const
   {return CProxyElement_IrrGroup::ckGetGroupID();}
operator CkGroupID () const { return ckGetGroupID(); }

    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxyElement_IrrGroup::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxyElement_IrrGroup::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxyElement_IrrGroup::ckSetReductionClient(cb); }
int ckGetGroupPe(void) const
{return CProxyElement_IrrGroup::ckGetGroupPe();}

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxyElement_IrrGroup::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxyElement_IrrGroup::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxyElement_IrrGroup::pup(p);
    }
    void ckSetGroupID(CkGroupID g) {
      CProxyElement_IrrGroup::ckSetGroupID(g);
    }
    HTram* ckLocalBranch(void) const {
      return ckLocalBranch(ckGetGroupID());
    }
    static HTram* ckLocalBranch(CkGroupID gID) {
      return (HTram*)CkLocalBranch(gID);
    }
/* DECLS: HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
 */
    

/* DECLS: HTram(const CkGroupID &gid, const CkCallback &cb);
 */
    

/* DECLS: void insertValue(const std::pair<int,int> &value, int destPE);
 */
    
    void insertValue(const std::pair<int,int> &value, int destPE, const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void tflush();
 */
    
    void tflush(const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void receivePerPE(HTramNodeMessage* impl_msg);
 */
    
    void receivePerPE(HTramNodeMessage* impl_msg);

};
/* ---------------- collective proxy -------------- */
class CProxy_HTram: public CProxy_IrrGroup{
  public:
    typedef HTram local_t;
    typedef CkIndex_HTram index_t;
    typedef CProxy_HTram proxy_t;
    typedef CProxyElement_HTram element_t;
    typedef CProxySection_HTram section_t;

    CProxy_HTram(void) {
    }
    CProxy_HTram(const IrrGroup *g) : CProxy_IrrGroup(g){
    }
    CProxy_HTram(CkGroupID _gid,CK_DELCTOR_PARAM) : CProxy_IrrGroup(_gid,CK_DELCTOR_ARGS){  }
    CProxy_HTram(CkGroupID _gid) : CProxy_IrrGroup(_gid){  }
    CProxyElement_HTram operator[](int onPE) const
      {return CProxyElement_HTram(ckGetGroupID(),onPE,CK_DELCTOR_CALL);}

    int ckIsDelegated(void) const
    { return CProxy_IrrGroup::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxy_IrrGroup::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxy_IrrGroup::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxy_IrrGroup::ckDelegatedIdx(); }
inline void ckCheck(void) const {CProxy_IrrGroup::ckCheck();}
CkChareID ckGetChareID(void) const
   {return CProxy_IrrGroup::ckGetChareID();}
CkGroupID ckGetGroupID(void) const
   {return CProxy_IrrGroup::ckGetGroupID();}
operator CkGroupID () const { return ckGetGroupID(); }

    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxy_IrrGroup::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxy_IrrGroup::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxy_IrrGroup::ckSetReductionClient(cb); }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxy_IrrGroup::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxy_IrrGroup::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxy_IrrGroup::pup(p);
    }
    void ckSetGroupID(CkGroupID g) {
      CProxy_IrrGroup::ckSetGroupID(g);
    }
    HTram* ckLocalBranch(void) const {
      return ckLocalBranch(ckGetGroupID());
    }
    static HTram* ckLocalBranch(CkGroupID gID) {
      return (HTram*)CkLocalBranch(gID);
    }
/* DECLS: HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
 */
    
    static CkGroupID ckNew(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3, const CkEntryOptions *impl_e_opts=NULL);
    CProxy_HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3, const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: HTram(const CkGroupID &gid, const CkCallback &cb);
 */
    
    static CkGroupID ckNew(const CkGroupID &gid, const CkCallback &cb, const CkEntryOptions *impl_e_opts=NULL);
    CProxy_HTram(const CkGroupID &gid, const CkCallback &cb, const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void insertValue(const std::pair<int,int> &value, int destPE);
 */
    
    void insertValue(const std::pair<int,int> &value, int destPE, const CkEntryOptions *impl_e_opts=NULL);
    
    void insertValue(const std::pair<int,int> &value, int destPE, int npes, int *pes, const CkEntryOptions *impl_e_opts=NULL);
    
    void insertValue(const std::pair<int,int> &value, int destPE, CmiGroup &grp, const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void tflush();
 */
    
    void tflush(const CkEntryOptions *impl_e_opts=NULL);
    
    void tflush(int npes, int *pes, const CkEntryOptions *impl_e_opts=NULL);
    
    void tflush(CmiGroup &grp, const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void receivePerPE(HTramNodeMessage* impl_msg);
 */
    
    void receivePerPE(HTramNodeMessage* impl_msg);
    
    void receivePerPE(HTramNodeMessage* impl_msg, int npes, int *pes);
    
    void receivePerPE(HTramNodeMessage* impl_msg, CmiGroup &grp);

};
/* ---------------- section proxy -------------- */
class CProxySection_HTram: public CProxySection_IrrGroup{
  public:
    typedef HTram local_t;
    typedef CkIndex_HTram index_t;
    typedef CProxy_HTram proxy_t;
    typedef CProxyElement_HTram element_t;
    typedef CProxySection_HTram section_t;

    CProxySection_HTram(void) {
    }
    CProxySection_HTram(const IrrGroup *g) : CProxySection_IrrGroup(g){
    }
    CProxySection_HTram(const CkGroupID &_gid,const int *_pelist,int _npes, CK_DELCTOR_PARAM) : CProxySection_IrrGroup(_gid,_pelist,_npes,CK_DELCTOR_ARGS){  }
    CProxySection_HTram(const CkGroupID &_gid,const int *_pelist,int _npes, int factor = USE_DEFAULT_BRANCH_FACTOR) : CProxySection_IrrGroup(_gid,_pelist,_npes,factor){  }
    CProxySection_HTram(int n,const CkGroupID *_gid, int const * const *_pelist,const int *_npes, int factor = USE_DEFAULT_BRANCH_FACTOR) : CProxySection_IrrGroup(n,_gid,_pelist,_npes,factor){  }
    CProxySection_HTram(int n,const CkGroupID *_gid, int const * const *_pelist,const int *_npes, CK_DELCTOR_PARAM) : CProxySection_IrrGroup(n,_gid,_pelist,_npes,CK_DELCTOR_ARGS){  }

    int ckIsDelegated(void) const
    { return CProxySection_IrrGroup::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxySection_IrrGroup::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxySection_IrrGroup::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxySection_IrrGroup::ckDelegatedIdx(); }
inline void ckCheck(void) const {CProxySection_IrrGroup::ckCheck();}
CkChareID ckGetChareID(void) const
   {return CProxySection_IrrGroup::ckGetChareID();}
CkGroupID ckGetGroupID(void) const
   {return CProxySection_IrrGroup::ckGetGroupID();}
operator CkGroupID () const { return ckGetGroupID(); }

    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxySection_IrrGroup::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxySection_IrrGroup::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxySection_IrrGroup::ckSetReductionClient(cb); }
inline int ckGetNumSections() const
{ return CProxySection_IrrGroup::ckGetNumSections(); }
inline CkSectionInfo &ckGetSectionInfo()
{ return CProxySection_IrrGroup::ckGetSectionInfo(); }
inline CkSectionID *ckGetSectionIDs()
{ return CProxySection_IrrGroup::ckGetSectionIDs(); }
inline CkSectionID &ckGetSectionID()
{ return CProxySection_IrrGroup::ckGetSectionID(); }
inline CkSectionID &ckGetSectionID(int i)
{ return CProxySection_IrrGroup::ckGetSectionID(i); }
inline CkGroupID ckGetGroupIDn(int i) const
{ return CProxySection_IrrGroup::ckGetGroupIDn(i); }
inline const int *ckGetElements() const
{ return CProxySection_IrrGroup::ckGetElements(); }
inline const int *ckGetElements(int i) const
{ return CProxySection_IrrGroup::ckGetElements(i); }
inline int ckGetNumElements() const
{ return CProxySection_IrrGroup::ckGetNumElements(); } 
inline int ckGetNumElements(int i) const
{ return CProxySection_IrrGroup::ckGetNumElements(i); }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxySection_IrrGroup::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxySection_IrrGroup::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxySection_IrrGroup::pup(p);
    }
    void ckSetGroupID(CkGroupID g) {
      CProxySection_IrrGroup::ckSetGroupID(g);
    }
    HTram* ckLocalBranch(void) const {
      return ckLocalBranch(ckGetGroupID());
    }
    static HTram* ckLocalBranch(CkGroupID gID) {
      return (HTram*)CkLocalBranch(gID);
    }
/* DECLS: HTram(const CkGroupID &impl_noname_0, int impl_noname_1, const bool &impl_noname_2, double impl_noname_3);
 */
    

/* DECLS: HTram(const CkGroupID &gid, const CkCallback &cb);
 */
    

/* DECLS: void insertValue(const std::pair<int,int> &value, int destPE);
 */
    
    void insertValue(const std::pair<int,int> &value, int destPE, const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void tflush();
 */
    
    void tflush(const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void receivePerPE(HTramNodeMessage* impl_msg);
 */
    
    void receivePerPE(HTramNodeMessage* impl_msg);

};
#define HTram_SDAG_CODE 
typedef CBaseT1<Group, CProxy_HTram>CBase_HTram;

/* DECLS: nodegroup HTramNodeGrp: NodeGroup{
HTramNodeGrp();
};
 */
 class HTramNodeGrp;
 class CkIndex_HTramNodeGrp;
 class CProxy_HTramNodeGrp;
 class CProxyElement_HTramNodeGrp;
 class CProxySection_HTramNodeGrp;
/* --------------- index object ------------------ */
class CkIndex_HTramNodeGrp:public CkIndex_NodeGroup{
  public:
    typedef HTramNodeGrp local_t;
    typedef CkIndex_HTramNodeGrp index_t;
    typedef CProxy_HTramNodeGrp proxy_t;
    typedef CProxyElement_HTramNodeGrp element_t;
    typedef CProxySection_HTramNodeGrp section_t;

    static int __idx;
    static void __register(const char *s, size_t size);
    /* DECLS: HTramNodeGrp();
     */
    // Entry point registration at startup
    
    static int reg_HTramNodeGrp_void();
    // Entry point index lookup
    
    inline static int idx_HTramNodeGrp_void() {
      static int epidx = reg_HTramNodeGrp_void();
      return epidx;
    }

    
    static int ckNew() { return idx_HTramNodeGrp_void(); }
    
    static void _call_HTramNodeGrp_void(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_HTramNodeGrp_void(void* impl_msg, void* impl_obj);
};
/* --------------- element proxy ------------------ */
class CProxyElement_HTramNodeGrp: public CProxyElement_NodeGroup{
  public:
    typedef HTramNodeGrp local_t;
    typedef CkIndex_HTramNodeGrp index_t;
    typedef CProxy_HTramNodeGrp proxy_t;
    typedef CProxyElement_HTramNodeGrp element_t;
    typedef CProxySection_HTramNodeGrp section_t;


    /* TRAM aggregators */

    CProxyElement_HTramNodeGrp(void) {
    }
    CProxyElement_HTramNodeGrp(const IrrGroup *g) : CProxyElement_NodeGroup(g){
    }
    CProxyElement_HTramNodeGrp(CkGroupID _gid,int _onPE,CK_DELCTOR_PARAM) : CProxyElement_NodeGroup(_gid,_onPE,CK_DELCTOR_ARGS){
    }
    CProxyElement_HTramNodeGrp(CkGroupID _gid,int _onPE) : CProxyElement_NodeGroup(_gid,_onPE){
    }

    int ckIsDelegated(void) const
    { return CProxyElement_NodeGroup::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxyElement_NodeGroup::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxyElement_NodeGroup::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxyElement_NodeGroup::ckDelegatedIdx(); }
inline void ckCheck(void) const {CProxyElement_NodeGroup::ckCheck();}
CkChareID ckGetChareID(void) const
   {return CProxyElement_NodeGroup::ckGetChareID();}
CkGroupID ckGetGroupID(void) const
   {return CProxyElement_NodeGroup::ckGetGroupID();}
operator CkGroupID () const { return ckGetGroupID(); }

    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxyElement_NodeGroup::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxyElement_NodeGroup::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxyElement_NodeGroup::ckSetReductionClient(cb); }
int ckGetGroupPe(void) const
{return CProxyElement_NodeGroup::ckGetGroupPe();}

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxyElement_NodeGroup::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxyElement_NodeGroup::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxyElement_NodeGroup::pup(p);
    }
    void ckSetGroupID(CkGroupID g) {
      CProxyElement_NodeGroup::ckSetGroupID(g);
    }
    HTramNodeGrp* ckLocalBranch(void) const {
      return ckLocalBranch(ckGetGroupID());
    }
    static HTramNodeGrp* ckLocalBranch(CkGroupID gID) {
      return (HTramNodeGrp*)CkLocalNodeBranch(gID);
    }
/* DECLS: HTramNodeGrp();
 */
    

};
/* ---------------- collective proxy -------------- */
class CProxy_HTramNodeGrp: public CProxy_NodeGroup{
  public:
    typedef HTramNodeGrp local_t;
    typedef CkIndex_HTramNodeGrp index_t;
    typedef CProxy_HTramNodeGrp proxy_t;
    typedef CProxyElement_HTramNodeGrp element_t;
    typedef CProxySection_HTramNodeGrp section_t;

    CProxy_HTramNodeGrp(void) {
    }
    CProxy_HTramNodeGrp(const IrrGroup *g) : CProxy_NodeGroup(g){
    }
    CProxy_HTramNodeGrp(CkGroupID _gid,CK_DELCTOR_PARAM) : CProxy_NodeGroup(_gid,CK_DELCTOR_ARGS){  }
    CProxy_HTramNodeGrp(CkGroupID _gid) : CProxy_NodeGroup(_gid){  }
    CProxyElement_HTramNodeGrp operator[](int onPE) const
      {return CProxyElement_HTramNodeGrp(ckGetGroupID(),onPE,CK_DELCTOR_CALL);}

    int ckIsDelegated(void) const
    { return CProxy_NodeGroup::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxy_NodeGroup::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxy_NodeGroup::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxy_NodeGroup::ckDelegatedIdx(); }
inline void ckCheck(void) const {CProxy_NodeGroup::ckCheck();}
CkChareID ckGetChareID(void) const
   {return CProxy_NodeGroup::ckGetChareID();}
CkGroupID ckGetGroupID(void) const
   {return CProxy_NodeGroup::ckGetGroupID();}
operator CkGroupID () const { return ckGetGroupID(); }

    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxy_NodeGroup::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxy_NodeGroup::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxy_NodeGroup::ckSetReductionClient(cb); }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxy_NodeGroup::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxy_NodeGroup::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxy_NodeGroup::pup(p);
    }
    void ckSetGroupID(CkGroupID g) {
      CProxy_NodeGroup::ckSetGroupID(g);
    }
    HTramNodeGrp* ckLocalBranch(void) const {
      return ckLocalBranch(ckGetGroupID());
    }
    static HTramNodeGrp* ckLocalBranch(CkGroupID gID) {
      return (HTramNodeGrp*)CkLocalNodeBranch(gID);
    }
/* DECLS: HTramNodeGrp();
 */
    
    static CkGroupID ckNew(const CkEntryOptions *impl_e_opts=NULL);

};
/* ---------------- section proxy -------------- */
class CProxySection_HTramNodeGrp: public CProxySection_NodeGroup{
  public:
    typedef HTramNodeGrp local_t;
    typedef CkIndex_HTramNodeGrp index_t;
    typedef CProxy_HTramNodeGrp proxy_t;
    typedef CProxyElement_HTramNodeGrp element_t;
    typedef CProxySection_HTramNodeGrp section_t;

    CProxySection_HTramNodeGrp(void) {
    }
    CProxySection_HTramNodeGrp(const IrrGroup *g) : CProxySection_NodeGroup(g){
    }
    CProxySection_HTramNodeGrp(const CkGroupID &_gid,const int *_pelist,int _npes, CK_DELCTOR_PARAM) : CProxySection_NodeGroup(_gid,_pelist,_npes,CK_DELCTOR_ARGS){  }
    CProxySection_HTramNodeGrp(const CkGroupID &_gid,const int *_pelist,int _npes, int factor = USE_DEFAULT_BRANCH_FACTOR) : CProxySection_NodeGroup(_gid,_pelist,_npes,factor){  }
    CProxySection_HTramNodeGrp(int n,const CkGroupID *_gid, int const * const *_pelist,const int *_npes, int factor = USE_DEFAULT_BRANCH_FACTOR) : CProxySection_NodeGroup(n,_gid,_pelist,_npes,factor){  }
    CProxySection_HTramNodeGrp(int n,const CkGroupID *_gid, int const * const *_pelist,const int *_npes, CK_DELCTOR_PARAM) : CProxySection_NodeGroup(n,_gid,_pelist,_npes,CK_DELCTOR_ARGS){  }

    int ckIsDelegated(void) const
    { return CProxySection_NodeGroup::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxySection_NodeGroup::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxySection_NodeGroup::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxySection_NodeGroup::ckDelegatedIdx(); }
inline void ckCheck(void) const {CProxySection_NodeGroup::ckCheck();}
CkChareID ckGetChareID(void) const
   {return CProxySection_NodeGroup::ckGetChareID();}
CkGroupID ckGetGroupID(void) const
   {return CProxySection_NodeGroup::ckGetGroupID();}
operator CkGroupID () const { return ckGetGroupID(); }

    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxySection_NodeGroup::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxySection_NodeGroup::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxySection_NodeGroup::ckSetReductionClient(cb); }
inline int ckGetNumSections() const
{ return CProxySection_NodeGroup::ckGetNumSections(); }
inline CkSectionInfo &ckGetSectionInfo()
{ return CProxySection_NodeGroup::ckGetSectionInfo(); }
inline CkSectionID *ckGetSectionIDs()
{ return CProxySection_NodeGroup::ckGetSectionIDs(); }
inline CkSectionID &ckGetSectionID()
{ return CProxySection_NodeGroup::ckGetSectionID(); }
inline CkSectionID &ckGetSectionID(int i)
{ return CProxySection_NodeGroup::ckGetSectionID(i); }
inline CkGroupID ckGetGroupIDn(int i) const
{ return CProxySection_NodeGroup::ckGetGroupIDn(i); }
inline const int *ckGetElements() const
{ return CProxySection_NodeGroup::ckGetElements(); }
inline const int *ckGetElements(int i) const
{ return CProxySection_NodeGroup::ckGetElements(i); }
inline int ckGetNumElements() const
{ return CProxySection_NodeGroup::ckGetNumElements(); } 
inline int ckGetNumElements(int i) const
{ return CProxySection_NodeGroup::ckGetNumElements(i); }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxySection_NodeGroup::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxySection_NodeGroup::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxySection_NodeGroup::pup(p);
    }
    void ckSetGroupID(CkGroupID g) {
      CProxySection_NodeGroup::ckSetGroupID(g);
    }
    HTramNodeGrp* ckLocalBranch(void) const {
      return ckLocalBranch(ckGetGroupID());
    }
    static HTramNodeGrp* ckLocalBranch(CkGroupID gID) {
      return (HTramNodeGrp*)CkLocalNodeBranch(gID);
    }
/* DECLS: HTramNodeGrp();
 */
    

};
#define HTramNodeGrp_SDAG_CODE 
typedef CBaseT1<NodeGroup, CProxy_HTramNodeGrp>CBase_HTramNodeGrp;

/* DECLS: nodegroup HTramRecv: NodeGroup{
HTramRecv();
void receive(HTramMessage* impl_msg);
};
 */
 class HTramRecv;
 class CkIndex_HTramRecv;
 class CProxy_HTramRecv;
 class CProxyElement_HTramRecv;
 class CProxySection_HTramRecv;
/* --------------- index object ------------------ */
class CkIndex_HTramRecv:public CkIndex_NodeGroup{
  public:
    typedef HTramRecv local_t;
    typedef CkIndex_HTramRecv index_t;
    typedef CProxy_HTramRecv proxy_t;
    typedef CProxyElement_HTramRecv element_t;
    typedef CProxySection_HTramRecv section_t;

    static int __idx;
    static void __register(const char *s, size_t size);
    /* DECLS: HTramRecv();
     */
    // Entry point registration at startup
    
    static int reg_HTramRecv_void();
    // Entry point index lookup
    
    inline static int idx_HTramRecv_void() {
      static int epidx = reg_HTramRecv_void();
      return epidx;
    }

    
    static int ckNew() { return idx_HTramRecv_void(); }
    
    static void _call_HTramRecv_void(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_HTramRecv_void(void* impl_msg, void* impl_obj);
    /* DECLS: void receive(HTramMessage* impl_msg);
     */
    // Entry point registration at startup
    
    static int reg_receive_HTramMessage();
    // Entry point index lookup
    
    inline static int idx_receive_HTramMessage() {
      static int epidx = reg_receive_HTramMessage();
      return epidx;
    }

    
    inline static int idx_receive(void (HTramRecv::*)(HTramMessage* impl_msg) ) {
      return idx_receive_HTramMessage();
    }


    
    static int receive(HTramMessage* impl_msg) { return idx_receive_HTramMessage(); }
    
    static void _call_receive_HTramMessage(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_receive_HTramMessage(void* impl_msg, void* impl_obj);
};
/* --------------- element proxy ------------------ */
class CProxyElement_HTramRecv: public CProxyElement_NodeGroup{
  public:
    typedef HTramRecv local_t;
    typedef CkIndex_HTramRecv index_t;
    typedef CProxy_HTramRecv proxy_t;
    typedef CProxyElement_HTramRecv element_t;
    typedef CProxySection_HTramRecv section_t;


    /* TRAM aggregators */

    CProxyElement_HTramRecv(void) {
    }
    CProxyElement_HTramRecv(const IrrGroup *g) : CProxyElement_NodeGroup(g){
    }
    CProxyElement_HTramRecv(CkGroupID _gid,int _onPE,CK_DELCTOR_PARAM) : CProxyElement_NodeGroup(_gid,_onPE,CK_DELCTOR_ARGS){
    }
    CProxyElement_HTramRecv(CkGroupID _gid,int _onPE) : CProxyElement_NodeGroup(_gid,_onPE){
    }

    int ckIsDelegated(void) const
    { return CProxyElement_NodeGroup::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxyElement_NodeGroup::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxyElement_NodeGroup::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxyElement_NodeGroup::ckDelegatedIdx(); }
inline void ckCheck(void) const {CProxyElement_NodeGroup::ckCheck();}
CkChareID ckGetChareID(void) const
   {return CProxyElement_NodeGroup::ckGetChareID();}
CkGroupID ckGetGroupID(void) const
   {return CProxyElement_NodeGroup::ckGetGroupID();}
operator CkGroupID () const { return ckGetGroupID(); }

    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxyElement_NodeGroup::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxyElement_NodeGroup::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxyElement_NodeGroup::ckSetReductionClient(cb); }
int ckGetGroupPe(void) const
{return CProxyElement_NodeGroup::ckGetGroupPe();}

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxyElement_NodeGroup::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxyElement_NodeGroup::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxyElement_NodeGroup::pup(p);
    }
    void ckSetGroupID(CkGroupID g) {
      CProxyElement_NodeGroup::ckSetGroupID(g);
    }
    HTramRecv* ckLocalBranch(void) const {
      return ckLocalBranch(ckGetGroupID());
    }
    static HTramRecv* ckLocalBranch(CkGroupID gID) {
      return (HTramRecv*)CkLocalNodeBranch(gID);
    }
/* DECLS: HTramRecv();
 */
    

/* DECLS: void receive(HTramMessage* impl_msg);
 */
    
    void receive(HTramMessage* impl_msg);

};
/* ---------------- collective proxy -------------- */
class CProxy_HTramRecv: public CProxy_NodeGroup{
  public:
    typedef HTramRecv local_t;
    typedef CkIndex_HTramRecv index_t;
    typedef CProxy_HTramRecv proxy_t;
    typedef CProxyElement_HTramRecv element_t;
    typedef CProxySection_HTramRecv section_t;

    CProxy_HTramRecv(void) {
    }
    CProxy_HTramRecv(const IrrGroup *g) : CProxy_NodeGroup(g){
    }
    CProxy_HTramRecv(CkGroupID _gid,CK_DELCTOR_PARAM) : CProxy_NodeGroup(_gid,CK_DELCTOR_ARGS){  }
    CProxy_HTramRecv(CkGroupID _gid) : CProxy_NodeGroup(_gid){  }
    CProxyElement_HTramRecv operator[](int onPE) const
      {return CProxyElement_HTramRecv(ckGetGroupID(),onPE,CK_DELCTOR_CALL);}

    int ckIsDelegated(void) const
    { return CProxy_NodeGroup::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxy_NodeGroup::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxy_NodeGroup::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxy_NodeGroup::ckDelegatedIdx(); }
inline void ckCheck(void) const {CProxy_NodeGroup::ckCheck();}
CkChareID ckGetChareID(void) const
   {return CProxy_NodeGroup::ckGetChareID();}
CkGroupID ckGetGroupID(void) const
   {return CProxy_NodeGroup::ckGetGroupID();}
operator CkGroupID () const { return ckGetGroupID(); }

    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxy_NodeGroup::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxy_NodeGroup::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxy_NodeGroup::ckSetReductionClient(cb); }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxy_NodeGroup::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxy_NodeGroup::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxy_NodeGroup::pup(p);
    }
    void ckSetGroupID(CkGroupID g) {
      CProxy_NodeGroup::ckSetGroupID(g);
    }
    HTramRecv* ckLocalBranch(void) const {
      return ckLocalBranch(ckGetGroupID());
    }
    static HTramRecv* ckLocalBranch(CkGroupID gID) {
      return (HTramRecv*)CkLocalNodeBranch(gID);
    }
/* DECLS: HTramRecv();
 */
    
    static CkGroupID ckNew(const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void receive(HTramMessage* impl_msg);
 */
    
    void receive(HTramMessage* impl_msg);

};
/* ---------------- section proxy -------------- */
class CProxySection_HTramRecv: public CProxySection_NodeGroup{
  public:
    typedef HTramRecv local_t;
    typedef CkIndex_HTramRecv index_t;
    typedef CProxy_HTramRecv proxy_t;
    typedef CProxyElement_HTramRecv element_t;
    typedef CProxySection_HTramRecv section_t;

    CProxySection_HTramRecv(void) {
    }
    CProxySection_HTramRecv(const IrrGroup *g) : CProxySection_NodeGroup(g){
    }
    CProxySection_HTramRecv(const CkGroupID &_gid,const int *_pelist,int _npes, CK_DELCTOR_PARAM) : CProxySection_NodeGroup(_gid,_pelist,_npes,CK_DELCTOR_ARGS){  }
    CProxySection_HTramRecv(const CkGroupID &_gid,const int *_pelist,int _npes, int factor = USE_DEFAULT_BRANCH_FACTOR) : CProxySection_NodeGroup(_gid,_pelist,_npes,factor){  }
    CProxySection_HTramRecv(int n,const CkGroupID *_gid, int const * const *_pelist,const int *_npes, int factor = USE_DEFAULT_BRANCH_FACTOR) : CProxySection_NodeGroup(n,_gid,_pelist,_npes,factor){  }
    CProxySection_HTramRecv(int n,const CkGroupID *_gid, int const * const *_pelist,const int *_npes, CK_DELCTOR_PARAM) : CProxySection_NodeGroup(n,_gid,_pelist,_npes,CK_DELCTOR_ARGS){  }

    int ckIsDelegated(void) const
    { return CProxySection_NodeGroup::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxySection_NodeGroup::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxySection_NodeGroup::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxySection_NodeGroup::ckDelegatedIdx(); }
inline void ckCheck(void) const {CProxySection_NodeGroup::ckCheck();}
CkChareID ckGetChareID(void) const
   {return CProxySection_NodeGroup::ckGetChareID();}
CkGroupID ckGetGroupID(void) const
   {return CProxySection_NodeGroup::ckGetGroupID();}
operator CkGroupID () const { return ckGetGroupID(); }

    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxySection_NodeGroup::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxySection_NodeGroup::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxySection_NodeGroup::ckSetReductionClient(cb); }
inline int ckGetNumSections() const
{ return CProxySection_NodeGroup::ckGetNumSections(); }
inline CkSectionInfo &ckGetSectionInfo()
{ return CProxySection_NodeGroup::ckGetSectionInfo(); }
inline CkSectionID *ckGetSectionIDs()
{ return CProxySection_NodeGroup::ckGetSectionIDs(); }
inline CkSectionID &ckGetSectionID()
{ return CProxySection_NodeGroup::ckGetSectionID(); }
inline CkSectionID &ckGetSectionID(int i)
{ return CProxySection_NodeGroup::ckGetSectionID(i); }
inline CkGroupID ckGetGroupIDn(int i) const
{ return CProxySection_NodeGroup::ckGetGroupIDn(i); }
inline const int *ckGetElements() const
{ return CProxySection_NodeGroup::ckGetElements(); }
inline const int *ckGetElements(int i) const
{ return CProxySection_NodeGroup::ckGetElements(i); }
inline int ckGetNumElements() const
{ return CProxySection_NodeGroup::ckGetNumElements(); } 
inline int ckGetNumElements(int i) const
{ return CProxySection_NodeGroup::ckGetNumElements(i); }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxySection_NodeGroup::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxySection_NodeGroup::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxySection_NodeGroup::pup(p);
    }
    void ckSetGroupID(CkGroupID g) {
      CProxySection_NodeGroup::ckSetGroupID(g);
    }
    HTramRecv* ckLocalBranch(void) const {
      return ckLocalBranch(ckGetGroupID());
    }
    static HTramRecv* ckLocalBranch(CkGroupID gID) {
      return (HTramRecv*)CkLocalNodeBranch(gID);
    }
/* DECLS: HTramRecv();
 */
    

/* DECLS: void receive(HTramMessage* impl_msg);
 */
    
    void receive(HTramMessage* impl_msg);

};
#define HTramRecv_SDAG_CODE 
typedef CBaseT1<NodeGroup, CProxy_HTramRecv>CBase_HTramRecv;






/* ---------------- method closures -------------- */
class Closure_HTram {
  public:



    struct insertValue_3_closure;


    struct tflush_4_closure;


};

/* ---------------- method closures -------------- */
class Closure_HTramNodeGrp {
  public:

};

/* ---------------- method closures -------------- */
class Closure_HTramRecv {
  public:


};

extern void _registerhtram_group(void);
#endif
