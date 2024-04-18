#ifndef _DECL_weighted_htram_smp_H_
#define _DECL_weighted_htram_smp_H_
#include "charm++.h"
#include "envelope.h"
#include <memory>
#include "sdag.h"
#include "weighted_node_struct.h"

#include "htram_group.decl.h"

/* DECLS: readonly CProxy_Main mainProxy;
 */

/* DECLS: readonly CProxy_WeightedArray arr;
 */

/* DECLS: readonly int N;
 */

/* DECLS: readonly int V;
 */

/* DECLS: readonly int imax;
 */

/* DECLS: readonly int average;
 */

/* DECLS: readonly int buffer_size;
 */

/* DECLS: readonly double flush_timer;
 */

/* DECLS: readonly bool enable_buffer_flushing;
 */

/* DECLS: mainchare Main: Chare{
Main(CkArgMsg* impl_msg);
void begin(int result);
void print();
void done(int result);
void check_buffer_done(const int *msg_stats, int N);
};
 */
 class Main;
 class CkIndex_Main;
 class CProxy_Main;
/* --------------- index object ------------------ */
class CkIndex_Main:public CkIndex_Chare{
  public:
    typedef Main local_t;
    typedef CkIndex_Main index_t;
    typedef CProxy_Main proxy_t;
    typedef CProxy_Main element_t;

    static int __idx;
    static void __register(const char *s, size_t size);
    /* DECLS: Main(CkArgMsg* impl_msg);
     */
    // Entry point registration at startup
    
    static int reg_Main_CkArgMsg();
    // Entry point index lookup
    
    inline static int idx_Main_CkArgMsg() {
      static int epidx = reg_Main_CkArgMsg();
      return epidx;
    }

    
    static int ckNew(CkArgMsg* impl_msg) { return idx_Main_CkArgMsg(); }
    
    static void _call_Main_CkArgMsg(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_Main_CkArgMsg(void* impl_msg, void* impl_obj);
    /* DECLS: void begin(int result);
     */
    // Entry point registration at startup
    
    static int reg_begin_marshall2();
    // Entry point index lookup
    
    inline static int idx_begin_marshall2() {
      static int epidx = reg_begin_marshall2();
      return epidx;
    }

    
    inline static int idx_begin(void (Main::*)(int result) ) {
      return idx_begin_marshall2();
    }


    
    static int begin(int result) { return idx_begin_marshall2(); }
    // Entry point registration at startup
    
    static int reg_redn_wrapper_begin_marshall2();
    // Entry point index lookup
    
    inline static int idx_redn_wrapper_begin_marshall2() {
      static int epidx = reg_redn_wrapper_begin_marshall2();
      return epidx;
    }
    
    static int redn_wrapper_begin(CkReductionMsg* impl_msg) { return idx_redn_wrapper_begin_marshall2(); }
    
    static void _call_redn_wrapper_begin_marshall2(void* impl_msg, void* impl_obj_void);
    
    static void _call_begin_marshall2(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_begin_marshall2(void* impl_msg, void* impl_obj);
    
    static int _callmarshall_begin_marshall2(char* impl_buf, void* impl_obj_void);
    
    static void _marshallmessagepup_begin_marshall2(PUP::er &p,void *msg);
    /* DECLS: void print();
     */
    // Entry point registration at startup
    
    static int reg_print_void();
    // Entry point index lookup
    
    inline static int idx_print_void() {
      static int epidx = reg_print_void();
      return epidx;
    }

    
    inline static int idx_print(void (Main::*)() ) {
      return idx_print_void();
    }


    
    static int print() { return idx_print_void(); }
    
    static void _call_print_void(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_print_void(void* impl_msg, void* impl_obj);
    /* DECLS: void done(int result);
     */
    // Entry point registration at startup
    
    static int reg_done_marshall4();
    // Entry point index lookup
    
    inline static int idx_done_marshall4() {
      static int epidx = reg_done_marshall4();
      return epidx;
    }

    
    inline static int idx_done(void (Main::*)(int result) ) {
      return idx_done_marshall4();
    }


    
    static int done(int result) { return idx_done_marshall4(); }
    // Entry point registration at startup
    
    static int reg_redn_wrapper_done_marshall4();
    // Entry point index lookup
    
    inline static int idx_redn_wrapper_done_marshall4() {
      static int epidx = reg_redn_wrapper_done_marshall4();
      return epidx;
    }
    
    static int redn_wrapper_done(CkReductionMsg* impl_msg) { return idx_redn_wrapper_done_marshall4(); }
    
    static void _call_redn_wrapper_done_marshall4(void* impl_msg, void* impl_obj_void);
    
    static void _call_done_marshall4(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_done_marshall4(void* impl_msg, void* impl_obj);
    
    static int _callmarshall_done_marshall4(char* impl_buf, void* impl_obj_void);
    
    static void _marshallmessagepup_done_marshall4(PUP::er &p,void *msg);
    /* DECLS: void check_buffer_done(const int *msg_stats, int N);
     */
    // Entry point registration at startup
    
    static int reg_check_buffer_done_marshall5();
    // Entry point index lookup
    
    inline static int idx_check_buffer_done_marshall5() {
      static int epidx = reg_check_buffer_done_marshall5();
      return epidx;
    }

    
    inline static int idx_check_buffer_done(void (Main::*)(const int *msg_stats, int N) ) {
      return idx_check_buffer_done_marshall5();
    }


    
    static int check_buffer_done(const int *msg_stats, int N) { return idx_check_buffer_done_marshall5(); }
    // Entry point registration at startup
    
    static int reg_redn_wrapper_check_buffer_done_marshall5();
    // Entry point index lookup
    
    inline static int idx_redn_wrapper_check_buffer_done_marshall5() {
      static int epidx = reg_redn_wrapper_check_buffer_done_marshall5();
      return epidx;
    }
    
    static int redn_wrapper_check_buffer_done(CkReductionMsg* impl_msg) { return idx_redn_wrapper_check_buffer_done_marshall5(); }
    
    static void _call_redn_wrapper_check_buffer_done_marshall5(void* impl_msg, void* impl_obj_void);
    
    static void _call_check_buffer_done_marshall5(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_check_buffer_done_marshall5(void* impl_msg, void* impl_obj);
    
    static int _callmarshall_check_buffer_done_marshall5(char* impl_buf, void* impl_obj_void);
    
    static void _marshallmessagepup_check_buffer_done_marshall5(PUP::er &p,void *msg);
};
/* --------------- element proxy ------------------ */
class CProxy_Main:public CProxy_Chare{
  public:
    typedef Main local_t;
    typedef CkIndex_Main index_t;
    typedef CProxy_Main proxy_t;
    typedef CProxy_Main element_t;

    CProxy_Main(void) {};
    CProxy_Main(CkChareID __cid) : CProxy_Chare(__cid){  }
    CProxy_Main(const Chare *c) : CProxy_Chare(c){  }

    int ckIsDelegated(void) const
    { return CProxy_Chare::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxy_Chare::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxy_Chare::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxy_Chare::ckDelegatedIdx(); }

    inline void ckCheck(void) const
    { CProxy_Chare::ckCheck(); }
    const CkChareID &ckGetChareID(void) const
    { return CProxy_Chare::ckGetChareID(); }
    operator const CkChareID &(void) const
    { return ckGetChareID(); }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxy_Chare::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxy_Chare::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxy_Chare::pup(p);
    }

    void ckSetChareID(const CkChareID &c)
    {      CProxy_Chare::ckSetChareID(c); }
    Main *ckLocal(void) const
    { return (Main *)CkLocalChare(&ckGetChareID()); }
/* DECLS: Main(CkArgMsg* impl_msg);
 */
    static CkChareID ckNew(CkArgMsg* impl_msg, int onPE=CK_PE_ANY);
    static void ckNew(CkArgMsg* impl_msg, CkChareID* pcid, int onPE=CK_PE_ANY);

/* DECLS: void begin(int result);
 */
    
    void begin(int result, const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void print();
 */
    
    void print(const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void done(int result);
 */
    
    void done(int result, const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void check_buffer_done(const int *msg_stats, int N);
 */
    
    void check_buffer_done(const int *msg_stats, int N, const CkEntryOptions *impl_e_opts=NULL);

};
#define Main_SDAG_CODE 
typedef CBaseT1<Chare, CProxy_Main>CBase_Main;

/* DECLS: array WeightedArray: ArrayElement{
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
 class WeightedArray;
 class CkIndex_WeightedArray;
 class CProxy_WeightedArray;
 class CProxyElement_WeightedArray;
 class CProxySection_WeightedArray;
/* --------------- index object ------------------ */
class CkIndex_WeightedArray:public CkIndex_ArrayElement{
  public:
    typedef WeightedArray local_t;
    typedef CkIndex_WeightedArray index_t;
    typedef CProxy_WeightedArray proxy_t;
    typedef CProxyElement_WeightedArray element_t;
    typedef CProxySection_WeightedArray section_t;

    static int __idx;
    static void __register(const char *s, size_t size);
    /* DECLS: WeightedArray();
     */
    // Entry point registration at startup
    
    static int reg_WeightedArray_void();
    // Entry point index lookup
    
    inline static int idx_WeightedArray_void() {
      static int epidx = reg_WeightedArray_void();
      return epidx;
    }

    
    static int ckNew() { return idx_WeightedArray_void(); }
    
    static void _call_WeightedArray_void(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_WeightedArray_void(void* impl_msg, void* impl_obj);
    /* DECLS: void initiate_pointers();
     */
    // Entry point registration at startup
    
    static int reg_initiate_pointers_void();
    // Entry point index lookup
    
    inline static int idx_initiate_pointers_void() {
      static int epidx = reg_initiate_pointers_void();
      return epidx;
    }

    
    inline static int idx_initiate_pointers(void (WeightedArray::*)() ) {
      return idx_initiate_pointers_void();
    }


    
    static int initiate_pointers() { return idx_initiate_pointers_void(); }
    
    static void _call_initiate_pointers_void(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_initiate_pointers_void(void* impl_msg, void* impl_obj);
    /* DECLS: void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
     */
    // Entry point registration at startup
    
    static int reg_get_graph_marshall3();
    // Entry point index lookup
    
    inline static int idx_get_graph_marshall3() {
      static int epidx = reg_get_graph_marshall3();
      return epidx;
    }

    
    inline static int idx_get_graph(void (WeightedArray::*)(const LongEdge *edges, int E, const int *partition, int dividers) ) {
      return idx_get_graph_marshall3();
    }


    
    static int get_graph(const LongEdge *edges, int E, const int *partition, int dividers) { return idx_get_graph_marshall3(); }
    
    static void _call_get_graph_marshall3(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_get_graph_marshall3(void* impl_msg, void* impl_obj);
    
    static int _callmarshall_get_graph_marshall3(char* impl_buf, void* impl_obj_void);
    
    static void _marshallmessagepup_get_graph_marshall3(PUP::er &p,void *msg);
    /* DECLS: void update_distances(const std::pair<int,int> &new_vertex_and_distance);
     */
    // Entry point registration at startup
    
    static int reg_update_distances_marshall4();
    // Entry point index lookup
    
    inline static int idx_update_distances_marshall4() {
      static int epidx = reg_update_distances_marshall4();
      return epidx;
    }

    
    inline static int idx_update_distances(void (WeightedArray::*)(const std::pair<int,int> &new_vertex_and_distance) ) {
      return idx_update_distances_marshall4();
    }


    
    static int update_distances(const std::pair<int,int> &new_vertex_and_distance) { return idx_update_distances_marshall4(); }
    
    static void _call_update_distances_marshall4(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_update_distances_marshall4(void* impl_msg, void* impl_obj);
    
    static int _callmarshall_update_distances_marshall4(char* impl_buf, void* impl_obj_void);
    
    static void _marshallmessagepup_update_distances_marshall4(PUP::er &p,void *msg);
    /* DECLS: void check_buffer();
     */
    // Entry point registration at startup
    
    static int reg_check_buffer_void();
    // Entry point index lookup
    
    inline static int idx_check_buffer_void() {
      static int epidx = reg_check_buffer_void();
      return epidx;
    }

    
    inline static int idx_check_buffer(void (WeightedArray::*)() ) {
      return idx_check_buffer_void();
    }


    
    static int check_buffer() { return idx_check_buffer_void(); }
    
    static void _call_check_buffer_void(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_check_buffer_void(void* impl_msg, void* impl_obj);
    /* DECLS: void keep_going();
     */
    // Entry point registration at startup
    
    static int reg_keep_going_void();
    // Entry point index lookup
    
    inline static int idx_keep_going_void() {
      static int epidx = reg_keep_going_void();
      return epidx;
    }

    
    inline static int idx_keep_going(void (WeightedArray::*)() ) {
      return idx_keep_going_void();
    }


    
    static int keep_going() { return idx_keep_going_void(); }
    
    static void _call_keep_going_void(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_keep_going_void(void* impl_msg, void* impl_obj);
    /* DECLS: void print_distances();
     */
    // Entry point registration at startup
    
    static int reg_print_distances_void();
    // Entry point index lookup
    
    inline static int idx_print_distances_void() {
      static int epidx = reg_print_distances_void();
      return epidx;
    }

    
    inline static int idx_print_distances(void (WeightedArray::*)() ) {
      return idx_print_distances_void();
    }


    
    static int print_distances() { return idx_print_distances_void(); }
    
    static void _call_print_distances_void(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_print_distances_void(void* impl_msg, void* impl_obj);
    /* DECLS: WeightedArray(CkMigrateMessage* impl_msg);
     */
    // Entry point registration at startup
    
    static int reg_WeightedArray_CkMigrateMessage();
    // Entry point index lookup
    
    inline static int idx_WeightedArray_CkMigrateMessage() {
      static int epidx = reg_WeightedArray_CkMigrateMessage();
      return epidx;
    }

    
    static int ckNew(CkMigrateMessage* impl_msg) { return idx_WeightedArray_CkMigrateMessage(); }
    
    static void _call_WeightedArray_CkMigrateMessage(void* impl_msg, void* impl_obj);
    
    static void _call_sdag_WeightedArray_CkMigrateMessage(void* impl_msg, void* impl_obj);
};
/* --------------- element proxy ------------------ */
 class CProxyElement_WeightedArray : public CProxyElement_ArrayElement{
  public:
    typedef WeightedArray local_t;
    typedef CkIndex_WeightedArray index_t;
    typedef CProxy_WeightedArray proxy_t;
    typedef CProxyElement_WeightedArray element_t;
    typedef CProxySection_WeightedArray section_t;

    using array_index_t = CkArrayIndex1D;

    /* TRAM aggregators */

    CProxyElement_WeightedArray(void) {
    }
    CProxyElement_WeightedArray(const ArrayElement *e) : CProxyElement_ArrayElement(e){
    }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxyElement_ArrayElement::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxyElement_ArrayElement::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxyElement_ArrayElement::pup(p);
    }

    int ckIsDelegated(void) const
    { return CProxyElement_ArrayElement::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxyElement_ArrayElement::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxyElement_ArrayElement::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxyElement_ArrayElement::ckDelegatedIdx(); }

    inline void ckCheck(void) const
    { CProxyElement_ArrayElement::ckCheck(); }
    inline operator CkArrayID () const
    { return ckGetArrayID(); }
    inline CkArrayID ckGetArrayID(void) const
    { return CProxyElement_ArrayElement::ckGetArrayID(); }
    inline CkArray *ckLocalBranch(void) const
    { return CProxyElement_ArrayElement::ckLocalBranch(); }
    inline CkLocMgr *ckLocMgr(void) const
    { return CProxyElement_ArrayElement::ckLocMgr(); }

    inline static CkArrayID ckCreateEmptyArray(CkArrayOptions opts = CkArrayOptions())
    { return CProxyElement_ArrayElement::ckCreateEmptyArray(opts); }
    inline static void ckCreateEmptyArrayAsync(CkCallback cb, CkArrayOptions opts = CkArrayOptions())
    { CProxyElement_ArrayElement::ckCreateEmptyArrayAsync(cb, opts); }
    inline static CkArrayID ckCreateArray(CkArrayMessage *m,int ctor,const CkArrayOptions &opts)
    { return CProxyElement_ArrayElement::ckCreateArray(m,ctor,opts); }
    inline void ckInsertIdx(CkArrayMessage *m,int ctor,int onPe,const CkArrayIndex &idx)
    { CProxyElement_ArrayElement::ckInsertIdx(m,ctor,onPe,idx); }
    inline void doneInserting(void)
    { CProxyElement_ArrayElement::doneInserting(); }

    inline void ckBroadcast(CkArrayMessage *m, int ep, int opts=0) const
    { CProxyElement_ArrayElement::ckBroadcast(m,ep,opts); }
    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxyElement_ArrayElement::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxyElement_ArrayElement::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxyElement_ArrayElement::ckSetReductionClient(cb); }

    inline void ckInsert(CkArrayMessage *m,int ctor,int onPe)
    { CProxyElement_ArrayElement::ckInsert(m,ctor,onPe); }
    inline void ckSend(CkArrayMessage *m, int ep, int opts = 0) const
    { CProxyElement_ArrayElement::ckSend(m,ep,opts); }
    inline void *ckSendSync(CkArrayMessage *m, int ep) const
    { return CProxyElement_ArrayElement::ckSendSync(m,ep); }
    inline const CkArrayIndex &ckGetIndex() const
    { return CProxyElement_ArrayElement::ckGetIndex(); }

    WeightedArray *ckLocal(void) const
    { return (WeightedArray *)CProxyElement_ArrayElement::ckLocal(); }

    CProxyElement_WeightedArray(const CkArrayID &aid,const CkArrayIndex1D &idx,CK_DELCTOR_PARAM)
        :CProxyElement_ArrayElement(aid,idx,CK_DELCTOR_ARGS)
    {
}
    CProxyElement_WeightedArray(const CkArrayID &aid,const CkArrayIndex1D &idx)
        :CProxyElement_ArrayElement(aid,idx)
    {
}

    CProxyElement_WeightedArray(const CkArrayID &aid,const CkArrayIndex &idx,CK_DELCTOR_PARAM)
        :CProxyElement_ArrayElement(aid,idx,CK_DELCTOR_ARGS)
    {
}
    CProxyElement_WeightedArray(const CkArrayID &aid,const CkArrayIndex &idx)
        :CProxyElement_ArrayElement(aid,idx)
    {
}
/* DECLS: WeightedArray();
 */
    
    void insert(int onPE=-1, const CkEntryOptions *impl_e_opts=NULL);
/* DECLS: void initiate_pointers();
 */
    
    void initiate_pointers(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
 */
    
    void get_graph(const LongEdge *edges, int E, const int *partition, int dividers, const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void update_distances(const std::pair<int,int> &new_vertex_and_distance);
 */
    
    void update_distances(const std::pair<int,int> &new_vertex_and_distance, const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void check_buffer();
 */
    
    void check_buffer(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void keep_going();
 */
    
    void keep_going(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void print_distances();
 */
    
    void print_distances(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: WeightedArray(CkMigrateMessage* impl_msg);
 */

};
/* ---------------- collective proxy -------------- */
 class CProxy_WeightedArray : public CProxy_ArrayElement{
  public:
    typedef WeightedArray local_t;
    typedef CkIndex_WeightedArray index_t;
    typedef CProxy_WeightedArray proxy_t;
    typedef CProxyElement_WeightedArray element_t;
    typedef CProxySection_WeightedArray section_t;

    using array_index_t = CkArrayIndex1D;
    CProxy_WeightedArray(void) {
    }
    CProxy_WeightedArray(const ArrayElement *e) : CProxy_ArrayElement(e){
    }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxy_ArrayElement::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxy_ArrayElement::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxy_ArrayElement::pup(p);
    }

    int ckIsDelegated(void) const
    { return CProxy_ArrayElement::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxy_ArrayElement::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxy_ArrayElement::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxy_ArrayElement::ckDelegatedIdx(); }

    inline void ckCheck(void) const
    { CProxy_ArrayElement::ckCheck(); }
    inline operator CkArrayID () const
    { return ckGetArrayID(); }
    inline CkArrayID ckGetArrayID(void) const
    { return CProxy_ArrayElement::ckGetArrayID(); }
    inline CkArray *ckLocalBranch(void) const
    { return CProxy_ArrayElement::ckLocalBranch(); }
    inline CkLocMgr *ckLocMgr(void) const
    { return CProxy_ArrayElement::ckLocMgr(); }

    inline static CkArrayID ckCreateEmptyArray(CkArrayOptions opts = CkArrayOptions())
    { return CProxy_ArrayElement::ckCreateEmptyArray(opts); }
    inline static void ckCreateEmptyArrayAsync(CkCallback cb, CkArrayOptions opts = CkArrayOptions())
    { CProxy_ArrayElement::ckCreateEmptyArrayAsync(cb, opts); }
    inline static CkArrayID ckCreateArray(CkArrayMessage *m,int ctor,const CkArrayOptions &opts)
    { return CProxy_ArrayElement::ckCreateArray(m,ctor,opts); }
    inline void ckInsertIdx(CkArrayMessage *m,int ctor,int onPe,const CkArrayIndex &idx)
    { CProxy_ArrayElement::ckInsertIdx(m,ctor,onPe,idx); }
    inline void doneInserting(void)
    { CProxy_ArrayElement::doneInserting(); }

    inline void ckBroadcast(CkArrayMessage *m, int ep, int opts=0) const
    { CProxy_ArrayElement::ckBroadcast(m,ep,opts); }
    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxy_ArrayElement::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxy_ArrayElement::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxy_ArrayElement::ckSetReductionClient(cb); }

    // Generalized array indexing:
    CProxyElement_WeightedArray operator [] (const CkArrayIndex1D &idx) const
    { return CProxyElement_WeightedArray(ckGetArrayID(), idx, CK_DELCTOR_CALL); }
    CProxyElement_WeightedArray operator() (const CkArrayIndex1D &idx) const
    { return CProxyElement_WeightedArray(ckGetArrayID(), idx, CK_DELCTOR_CALL); }
    CProxyElement_WeightedArray operator [] (int idx) const 
        {return CProxyElement_WeightedArray(ckGetArrayID(), CkArrayIndex1D(idx), CK_DELCTOR_CALL);}
    CProxyElement_WeightedArray operator () (int idx) const 
        {return CProxyElement_WeightedArray(ckGetArrayID(), CkArrayIndex1D(idx), CK_DELCTOR_CALL);}
    CProxy_WeightedArray(const CkArrayID &aid,CK_DELCTOR_PARAM) 
        :CProxy_ArrayElement(aid,CK_DELCTOR_ARGS) {}
    CProxy_WeightedArray(const CkArrayID &aid) 
        :CProxy_ArrayElement(aid) {}
/* DECLS: WeightedArray();
 */
    
    static CkArrayID ckNew(const CkArrayOptions &opts = CkArrayOptions(), const CkEntryOptions *impl_e_opts=NULL);
    static void      ckNew(const CkArrayOptions &opts, CkCallback _ck_array_creation_cb, const CkEntryOptions *impl_e_opts=NULL);
    static CkArrayID ckNew(const int s1, const CkEntryOptions *impl_e_opts=NULL);
    static void ckNew(const int s1, CkCallback _ck_array_creation_cb, const CkEntryOptions *impl_e_opts=NULL);

/* DECLS: void initiate_pointers();
 */
    
    void initiate_pointers(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
 */
    
    void get_graph(const LongEdge *edges, int E, const int *partition, int dividers, const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void update_distances(const std::pair<int,int> &new_vertex_and_distance);
 */
    
    void update_distances(const std::pair<int,int> &new_vertex_and_distance, const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void check_buffer();
 */
    
    void check_buffer(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void keep_going();
 */
    
    void keep_going(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void print_distances();
 */
    
    void print_distances(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: WeightedArray(CkMigrateMessage* impl_msg);
 */

};
/* ---------------- section proxy -------------- */
 class CProxySection_WeightedArray : public CProxySection_ArrayElement{
  public:
    typedef WeightedArray local_t;
    typedef CkIndex_WeightedArray index_t;
    typedef CProxy_WeightedArray proxy_t;
    typedef CProxyElement_WeightedArray element_t;
    typedef CProxySection_WeightedArray section_t;

    using array_index_t = CkArrayIndex1D;
    CProxySection_WeightedArray(void) {
    }

    void ckDelegate(CkDelegateMgr *dTo,CkDelegateData *dPtr=NULL)
    {       CProxySection_ArrayElement::ckDelegate(dTo,dPtr); }
    void ckUndelegate(void)
    {       CProxySection_ArrayElement::ckUndelegate(); }
    void pup(PUP::er &p)
    {       CProxySection_ArrayElement::pup(p);
    }

    int ckIsDelegated(void) const
    { return CProxySection_ArrayElement::ckIsDelegated(); }
    inline CkDelegateMgr *ckDelegatedTo(void) const
    { return CProxySection_ArrayElement::ckDelegatedTo(); }
    inline CkDelegateData *ckDelegatedPtr(void) const
    { return CProxySection_ArrayElement::ckDelegatedPtr(); }
    CkGroupID ckDelegatedIdx(void) const
    { return CProxySection_ArrayElement::ckDelegatedIdx(); }

    inline void ckCheck(void) const
    { CProxySection_ArrayElement::ckCheck(); }
    inline operator CkArrayID () const
    { return ckGetArrayID(); }
    inline CkArrayID ckGetArrayID(void) const
    { return CProxySection_ArrayElement::ckGetArrayID(); }
    inline CkArray *ckLocalBranch(void) const
    { return CProxySection_ArrayElement::ckLocalBranch(); }
    inline CkLocMgr *ckLocMgr(void) const
    { return CProxySection_ArrayElement::ckLocMgr(); }

    inline static CkArrayID ckCreateEmptyArray(CkArrayOptions opts = CkArrayOptions())
    { return CProxySection_ArrayElement::ckCreateEmptyArray(opts); }
    inline static void ckCreateEmptyArrayAsync(CkCallback cb, CkArrayOptions opts = CkArrayOptions())
    { CProxySection_ArrayElement::ckCreateEmptyArrayAsync(cb, opts); }
    inline static CkArrayID ckCreateArray(CkArrayMessage *m,int ctor,const CkArrayOptions &opts)
    { return CProxySection_ArrayElement::ckCreateArray(m,ctor,opts); }
    inline void ckInsertIdx(CkArrayMessage *m,int ctor,int onPe,const CkArrayIndex &idx)
    { CProxySection_ArrayElement::ckInsertIdx(m,ctor,onPe,idx); }
    inline void doneInserting(void)
    { CProxySection_ArrayElement::doneInserting(); }

    inline void ckBroadcast(CkArrayMessage *m, int ep, int opts=0) const
    { CProxySection_ArrayElement::ckBroadcast(m,ep,opts); }
    inline void setReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxySection_ArrayElement::setReductionClient(fn,param); }
    inline void ckSetReductionClient(CkReductionClientFn fn,void *param=NULL) const
    { CProxySection_ArrayElement::ckSetReductionClient(fn,param); }
    inline void ckSetReductionClient(CkCallback *cb) const
    { CProxySection_ArrayElement::ckSetReductionClient(cb); }

    inline void ckSend(CkArrayMessage *m, int ep, int opts = 0)
    { CProxySection_ArrayElement::ckSend(m,ep,opts); }
    inline CkSectionInfo &ckGetSectionInfo()
    { return CProxySection_ArrayElement::ckGetSectionInfo(); }
    inline CkSectionID *ckGetSectionIDs()
    { return CProxySection_ArrayElement::ckGetSectionIDs(); }
    inline CkSectionID &ckGetSectionID()
    { return CProxySection_ArrayElement::ckGetSectionID(); }
    inline CkSectionID &ckGetSectionID(int i)
    { return CProxySection_ArrayElement::ckGetSectionID(i); }
    inline CkArrayID ckGetArrayIDn(int i) const
    { return CProxySection_ArrayElement::ckGetArrayIDn(i); } 
    inline CkArrayIndex *ckGetArrayElements() const
    { return CProxySection_ArrayElement::ckGetArrayElements(); }
    inline CkArrayIndex *ckGetArrayElements(int i) const
    { return CProxySection_ArrayElement::ckGetArrayElements(i); }
    inline int ckGetNumElements() const
    { return CProxySection_ArrayElement::ckGetNumElements(); } 
    inline int ckGetNumElements(int i) const
    { return CProxySection_ArrayElement::ckGetNumElements(i); }    // Generalized array indexing:
    CProxyElement_WeightedArray operator [] (const CkArrayIndex1D &idx) const
        {return CProxyElement_WeightedArray(ckGetArrayID(), idx, CK_DELCTOR_CALL);}
    CProxyElement_WeightedArray operator() (const CkArrayIndex1D &idx) const
        {return CProxyElement_WeightedArray(ckGetArrayID(), idx, CK_DELCTOR_CALL);}
    CProxyElement_WeightedArray operator [] (int idx) const 
        {return CProxyElement_WeightedArray(ckGetArrayID(), *(CkArrayIndex1D*)&ckGetArrayElements()[idx], CK_DELCTOR_CALL);}
    CProxyElement_WeightedArray operator () (int idx) const 
        {return CProxyElement_WeightedArray(ckGetArrayID(), *(CkArrayIndex1D*)&ckGetArrayElements()[idx], CK_DELCTOR_CALL);}
    static CkSectionID ckNew(const CkArrayID &aid, CkArrayIndex1D *elems, int nElems, int factor=USE_DEFAULT_BRANCH_FACTOR) {
      return CkSectionID(aid, elems, nElems, factor);
    } 
    static CkSectionID ckNew(const CkArrayID &aid, const std::vector<CkArrayIndex1D> &elems, int factor=USE_DEFAULT_BRANCH_FACTOR) {
      return CkSectionID(aid, elems, factor);
    } 
    static CkSectionID ckNew(const CkArrayID &aid, int l, int u, int s, int factor=USE_DEFAULT_BRANCH_FACTOR) {
      std::vector<CkArrayIndex1D> al;
      for (int i=l; i<=u; i+=s) al.emplace_back(i);
      return CkSectionID(aid, al, factor);
    } 
    CProxySection_WeightedArray(const CkArrayID &aid, CkArrayIndex *elems, int nElems, CK_DELCTOR_PARAM) 
        :CProxySection_ArrayElement(aid,elems,nElems,CK_DELCTOR_ARGS) {}
    CProxySection_WeightedArray(const CkArrayID &aid, const std::vector<CkArrayIndex> &elems, CK_DELCTOR_PARAM) 
        :CProxySection_ArrayElement(aid,elems,CK_DELCTOR_ARGS) {}
    CProxySection_WeightedArray(const CkArrayID &aid, CkArrayIndex *elems, int nElems, int factor=USE_DEFAULT_BRANCH_FACTOR) 
        :CProxySection_ArrayElement(aid,elems,nElems, factor) {}
    CProxySection_WeightedArray(const CkArrayID &aid, const std::vector<CkArrayIndex> &elems, int factor=USE_DEFAULT_BRANCH_FACTOR) 
        :CProxySection_ArrayElement(aid,elems, factor) { ckAutoDelegate(); }
    CProxySection_WeightedArray(const CkSectionID &sid)  
        :CProxySection_ArrayElement(sid) { ckAutoDelegate(); }
    CProxySection_WeightedArray(int n, const CkArrayID *aid, CkArrayIndex const * const *elems, const int *nElems, CK_DELCTOR_PARAM) 
        :CProxySection_ArrayElement(n,aid,elems,nElems,CK_DELCTOR_ARGS) {}
    CProxySection_WeightedArray(const std::vector<CkArrayID> &aid, const std::vector<std::vector<CkArrayIndex> > &elems, CK_DELCTOR_PARAM) 
        :CProxySection_ArrayElement(aid,elems,CK_DELCTOR_ARGS) {}
    CProxySection_WeightedArray(int n, const CkArrayID *aid, CkArrayIndex const * const *elems, const int *nElems) 
        :CProxySection_ArrayElement(n,aid,elems,nElems) { ckAutoDelegate(); }
    CProxySection_WeightedArray(const std::vector<CkArrayID> &aid, const std::vector<std::vector<CkArrayIndex> > &elems) 
        :CProxySection_ArrayElement(aid,elems) { ckAutoDelegate(); }
    CProxySection_WeightedArray(int n, const CkArrayID *aid, CkArrayIndex const * const *elems, const int *nElems, int factor) 
        :CProxySection_ArrayElement(n,aid,elems,nElems, factor) { ckAutoDelegate(); }
    CProxySection_WeightedArray(const std::vector<CkArrayID> &aid, const std::vector<std::vector<CkArrayIndex> > &elems, int factor) 
        :CProxySection_ArrayElement(aid,elems, factor) { ckAutoDelegate(); }
    static CkSectionID ckNew(const CkArrayID &aid, CkArrayIndex *elems, int nElems) {
      return CkSectionID(aid, elems, nElems);
    } 
    static CkSectionID ckNew(const CkArrayID &aid, const std::vector<CkArrayIndex> &elems) {
       return CkSectionID(aid, elems);
    } 
    static CkSectionID ckNew(const CkArrayID &aid, CkArrayIndex *elems, int nElems, int factor) {
      return CkSectionID(aid, elems, nElems, factor);
    } 
    static CkSectionID ckNew(const CkArrayID &aid, const std::vector<CkArrayIndex> &elems, int factor) {
      return CkSectionID(aid, elems, factor);
    } 
    void ckAutoDelegate(int opts=1) {
      if(ckIsDelegated()) return;
      CProxySection_ArrayElement::ckAutoDelegate(opts);
    } 
    void setReductionClient(CkCallback *cb) {
      CProxySection_ArrayElement::setReductionClient(cb);
    } 
    void resetSection() {
      CProxySection_ArrayElement::resetSection();
    } 
    static void contribute(CkSectionInfo &sid, int userData=-1, int fragSize=-1);
    static void contribute(int dataSize,void *data,CkReduction::reducerType type, CkSectionInfo &sid, int userData=-1, int fragSize=-1);
    template <typename T>
    static void contribute(std::vector<T> &data, CkReduction::reducerType type, CkSectionInfo &sid, int userData=-1, int fragSize=-1);
    static void contribute(CkSectionInfo &sid, const CkCallback &cb, int userData=-1, int fragSize=-1);
    static void contribute(int dataSize,void *data,CkReduction::reducerType type, CkSectionInfo &sid, const CkCallback &cb, int userData=-1, int fragSize=-1);
    template <typename T>
    static void contribute(std::vector<T> &data, CkReduction::reducerType type, CkSectionInfo &sid, const CkCallback &cb, int userData=-1, int fragSize=-1);
/* DECLS: WeightedArray();
 */
    

/* DECLS: void initiate_pointers();
 */
    
    void initiate_pointers(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void get_graph(const LongEdge *edges, int E, const int *partition, int dividers);
 */
    
    void get_graph(const LongEdge *edges, int E, const int *partition, int dividers, const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void update_distances(const std::pair<int,int> &new_vertex_and_distance);
 */
    
    void update_distances(const std::pair<int,int> &new_vertex_and_distance, const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void check_buffer();
 */
    
    void check_buffer(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void keep_going();
 */
    
    void keep_going(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: void print_distances();
 */
    
    void print_distances(const CkEntryOptions *impl_e_opts=NULL) ;

/* DECLS: WeightedArray(CkMigrateMessage* impl_msg);
 */

};
#define WeightedArray_SDAG_CODE 
typedef CBaseT1<ArrayElementT<CkIndex1D>, CProxy_WeightedArray>CBase_WeightedArray;












/* ---------------- method closures -------------- */
class Closure_Main {
  public:


    struct begin_2_closure;


    struct print_3_closure;


    struct done_4_closure;


    struct check_buffer_done_5_closure;

};

/* ---------------- method closures -------------- */
class Closure_WeightedArray {
  public:


    struct initiate_pointers_2_closure;


    struct get_graph_3_closure;


    struct update_distances_4_closure;


    struct check_buffer_5_closure;


    struct keep_going_6_closure;


    struct print_distances_7_closure;


};

extern void _registerweighted_htram_smp(void);
extern "C" void CkRegisterMainModule(void);
#endif
