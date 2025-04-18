/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_internal.c $
 * Description: GASNet implementation of internal helpers
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_am.h>

#include <gasnet_tools.h>

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h> /* time, ctime */

#if HAVE_MALLOC_H && !PLATFORM_OS_OPENBSD /* OpenBSD warns that malloc.h is obsolete */
  #include <malloc.h>
#endif

/* set to non-zero for verbose error reporting */
int gasneti_VerboseErrors = 1;

/* ------------------------------------------------------------------------------------ */
/* generic atomics support */
#if defined(GASNETI_BUILD_GENERIC_ATOMIC32) || defined(GASNETI_BUILD_GENERIC_ATOMIC64)
  #ifdef GASNETI_ATOMIC_LOCK_TBL_DEFNS
    GASNETI_ATOMIC_LOCK_TBL_DEFNS(gasneti_malloc)
  #endif
  #ifdef GASNETI_GENATOMIC32_DEFN
    GASNETI_GENATOMIC32_DEFN
  #endif
  #ifdef GASNETI_GENATOMIC64_DEFN
    GASNETI_GENATOMIC64_DEFN
  #endif
#endif

/* ------------------------------------------------------------------------------------ */

#if GASNETI_THROTTLE_POLLERS
  gasneti_atomic_t gasneti_throttle_haveusefulwork = gasneti_atomic_init(0);
  gasneti_mutex_t gasneti_throttle_spinpoller = GASNETI_MUTEX_INITIALIZER;
#endif
#if GASNET_DEBUG
  GASNETI_THREADKEY_DEFINE(gasneti_throttledebug_key);
#endif

#define GEX_VERSION_STR  _STRINGIFY(GEX_SPEC_VERSION_MAJOR) "."  _STRINGIFY(GEX_SPEC_VERSION_MINOR)
GASNETI_IDENT(gasneti_IdentString_EXAPIVersion, "$GASNetEXAPIVersion: " GEX_VERSION_STR " $");

#define GASNET_VERSION_STR  _STRINGIFY(GASNETI_SPEC_VERSION_MAJOR)
GASNETI_IDENT(gasneti_IdentString_APIVersion, "$GASNetAPIVersion: " GASNET_VERSION_STR " $");

#define GASNETI_THREAD_MODEL_STR _STRINGIFY(GASNETI_THREAD_MODEL)
GASNETI_IDENT(gasneti_IdentString_ThreadModel, "$GASNetThreadModel: GASNET_" GASNETI_THREAD_MODEL_STR " $");

#define GASNETI_SEGMENT_CONFIG_STR _STRINGIFY(GASNETI_SEGMENT_CONFIG)
GASNETI_IDENT(gasneti_IdentString_SegConfig, "$GASNetSegment: GASNET_SEGMENT_" GASNETI_SEGMENT_CONFIG_STR " $");

#ifdef GASNETI_BUG1389_WORKAROUND
  GASNETI_IDENT(gasneti_IdentString_ConservativeLocalCopy, "$GASNetConservativeLocalCopy: 1 $");
#endif

#if GASNETI_CLIENT_THREADS
  GASNETI_IDENT(gasneti_IdentString_ThreadInfoOpt, "$GASNetThreadInfoOpt: " _STRINGIFY(GASNETI_THREADINFO_OPT) " $");
#endif

#if GASNETI_SWIZZLE
  GASNETI_IDENT(gasneti_IdentString_Swizzle, "$GASNetSwizzle: 1 $");
#endif

/* embed a string with complete configuration info to support versioning checks */
GASNETI_IDENT(gasneti_IdentString_libraryConfig, "$GASNetConfig: (libgasnet.a) " GASNET_CONFIG_STRING " $");
/* the canonical conduit name */
GASNETI_IDENT(gasneti_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CONDUIT_NAME_STR " $");

int gasneti_init_done = 0; /*  true after init */
int gasneti_attach_done = 0; /*  true after attach */
extern void gasneti_checkinit(void) {
  if (!gasneti_init_done)
    gasneti_fatalerror("Illegal call to GASNet before library initialization. Please use gex_Client_Init() to initialize GASNet.");
}
extern void gasneti_checkattach(void) {
   gasneti_checkinit();
   if (!gasneti_attach_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_attach() initialization");
}

void (*gasnet_client_attach_hook)(void *, uintptr_t) = NULL;

int gasneti_wait_mode = GASNET_WAIT_SPIN;

int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(RELEASE_MAJOR_,GASNET_RELEASE_VERSION_MAJOR)) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(RELEASE_MINOR_,GASNET_RELEASE_VERSION_MINOR)) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(RELEASE_PATCH_,GASNET_RELEASE_VERSION_PATCH)) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_THREAD_MODEL) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_SEGMENT_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_DEBUG_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_TRACE_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_STATS_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_MALLOC_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_SRCLINES_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_ALIGN_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_PSHM_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_PTR_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_TIMER_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_MEMBAR_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_ATOMIC_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_ATOMIC32_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_ATOMIC64_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_TIOPT_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_MK_CLASS_CUDA_UVA_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_MK_CLASS_HIP_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_MK_CLASS_ZE_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(HIDDEN_AM_CONCUR_,GASNET_HIDDEN_AM_CONCURRENCY_LEVEL)) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(CACHE_LINE_BYTES_,GASNETI_CACHE_LINE_BYTES)) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(GASNETI_TM0_ALIGN_,GASNETI_TM0_ALIGN)) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(CORE_,GASNET_CORE_NAME)) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(EXTENDED_,GASNET_EXTENDED_NAME)) = 1;
#if GASNET_CONDUIT_OFI
  int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(OFI_PROVIDER_,GASNETC_OFI_PROVIDER_IDENT)) = 1;
#endif

/* global definitions of GASNet-wide internal variables
   not subject to override */
gex_Rank_t gasneti_mynode = (gex_Rank_t)-1;
gex_Rank_t gasneti_nodes = 0;

// a necessary evil - see the declaration in gasnet_help.h
gasneti_TM_t gasneti_thing_that_goes_thunk_in_the_dark = NULL;

/* Default global definitions of GASNet-wide internal variables
   if conduits override one of these, they must
   still provide variable or macro definitions for these tokens */
#if defined(_GASNET_GETMAXSEGMENTSIZE_DEFAULT)
  uintptr_t gasneti_MaxLocalSegmentSize = 0;
  uintptr_t gasneti_MaxGlobalSegmentSize = 0;
#endif

#ifdef _GASNETI_PROGRESSFNS_DEFAULT
  GASNETI_PROGRESSFNS_LIST(_GASNETI_PROGRESSFNS_DEFINE_FLAGS)
#endif

#if GASNET_DEBUG
  static void gasneti_disabled_progressfn(void) {
    gasneti_fatalerror("Called a disabled progress function");
  }
  gasneti_progressfn_t gasneti_debug_progressfn_bool = gasneti_disabled_progressfn;
  gasneti_progressfn_t gasneti_debug_progressfn_counted = gasneti_disabled_progressfn;
#endif

void gasneti_empty_pf(void) {}

gasnet_seginfo_t *gasneti_seginfo = NULL;
gasnet_seginfo_t *gasneti_seginfo_aux = NULL;

// TODO: this is proof-of-concept and not a scalable final solution (bug 4088)
// Note that (gasneti_seginfo_tbl[0] == gasneti_seginfo) to simplify some logic.
gasnet_seginfo_t *gasneti_seginfo_tbl[GASNET_MAXEPS] = {NULL, };

/* ------------------------------------------------------------------------------------ */
/* conduit-independent sanity checks */
extern void gasneti_check_config_preinit(void) {
  gasneti_static_assert(sizeof(int8_t) == 1);
  gasneti_static_assert(sizeof(uint8_t) == 1);
  gasneti_static_assert(sizeof(gasnete_anytype8_t) == 1);
  #ifndef INTTYPES_16BIT_MISSING
    gasneti_static_assert(sizeof(int16_t) == 2);
    gasneti_static_assert(sizeof(uint16_t) == 2);
    gasneti_static_assert(sizeof(gasnete_anytype16_t) == 2);
  #endif
  gasneti_static_assert(sizeof(int32_t) == 4);
  gasneti_static_assert(sizeof(uint32_t) == 4);
  gasneti_static_assert(sizeof(gasnete_anytype32_t) == 4);
  gasneti_static_assert(sizeof(int64_t) == 8);
  gasneti_static_assert(sizeof(uint64_t) == 8);
  gasneti_static_assert(sizeof(gasnete_anytype64_t) == 8);

  gasneti_static_assert(sizeof(uintptr_t) >= sizeof(void *));

  #define CHECK_DT(id, type) do { \
      gasneti_assert_always(gasneti_dt_valid(id)); \
      gasneti_assert_always_uint(gasneti_dt_size(id) ,==, sizeof(type)); \
      gasneti_assert_always(!!gasneti_dt_int(id) == !gasneti_dt_fp(id)); \
    } while (0)
  #define CHECK_INT_DT(id, type, sign) do { \
      CHECK_DT(id, type); \
      gasneti_assert_always(gasneti_dt_int(id)); \
      gasneti_assert_always(gasneti_dt_##sign(id)); \
      gasneti_assert_always(!!gasneti_dt_signed(id) == !gasneti_dt_unsigned(id)); \
    } while (0)
  #define CHECK_FP_DT(id, type) do { \
      CHECK_DT(id, type); \
      gasneti_assert_always(gasneti_dt_fp(id)); \
    } while (0)

  CHECK_INT_DT(GEX_DT_I32,  int32_t,   signed);
  CHECK_INT_DT(GEX_DT_U32, uint32_t, unsigned);
  CHECK_INT_DT(GEX_DT_I64,  int64_t,   signed);
  CHECK_INT_DT(GEX_DT_U64, uint64_t, unsigned);

  CHECK_FP_DT(GEX_DT_FLT,  float);
  CHECK_FP_DT(GEX_DT_DBL, double);

  gasneti_assert_always(gasneti_dt_valid_reduce(GEX_DT_USER));
  gasneti_assert_always(!gasneti_dt_valid_atomic(GEX_DT_USER));
  gasneti_assert_always(!gasneti_dt_int(GEX_DT_USER));
  gasneti_assert_always(!gasneti_dt_fp(GEX_DT_USER));
  gasneti_assert_always(!gasneti_dt_signed(GEX_DT_USER));
  gasneti_assert_always(!gasneti_dt_unsigned(GEX_DT_USER));

  #undef CHECK_DT
  #undef CHECK_INT_DT
  #undef CHECK_FP_DT

  #define _CHECK_OP(id, pred1, pred2, pred3) do { \
      gasneti_assert_always(gasneti_op_atomic(id)); \
      gasneti_assert_always(gasneti_op_int(id)); \
      gasneti_assert_always(!!gasneti_op_0arg(id) + \
                            !!gasneti_op_1arg(id) + \
                            !!gasneti_op_2arg(id) == 1); \
      gasneti_assert_always(gasneti_op_##pred1(id)); \
      gasneti_assert_always(gasneti_op_##pred2(id)); \
      gasneti_assert_always(gasneti_op_##pred3(id)); \
    } while (0)
  #define CHECK_ARITH_OP(stem, reduce_pred, fp_pred) do { \
      gasneti_assert_always(!gasneti_op_fetch(GEX_OP_##stem)); \
      _CHECK_OP(GEX_OP_##stem, reduce_pred, fp_pred, valid); \
      gasneti_assert_always(gasneti_op_fetch(GEX_OP_F##stem)); \
      _CHECK_OP(GEX_OP_F##stem, not_reduce, fp_pred, valid); \
    } while (0)
  #define CHECK_ACCESSOR(stem, pred) do { \
      gasneti_assert_always(gasneti_op_valid(GEX_OP_##stem)); \
      _CHECK_OP(GEX_OP_##stem, fp, not_reduce, pred); \
    } while (0)
  #define CHECK_USER(stem) do { \
      gasneti_assert_always(gasneti_op_valid(GEX_OP_##stem)); \
      gasneti_assert_always(gasneti_op_valid_reduce(GEX_OP_##stem)); \
      gasneti_assert_always(!gasneti_op_valid_atomic(GEX_OP_##stem)); \
      gasneti_assert_always(gasneti_op_int(GEX_OP_##stem)); \
      gasneti_assert_always(gasneti_op_fp(GEX_OP_##stem)); \
    } while (0)

  #define gasneti_op_not_reduce !gasneti_op_reduce
  #define gasneti_op_not_fetch  !gasneti_op_fetch
  #define gasneti_op_not_fp     !gasneti_op_fp

  CHECK_ARITH_OP(AND,  reduce, not_fp);
  CHECK_ARITH_OP(OR,   reduce, not_fp);
  CHECK_ARITH_OP(XOR,  reduce, not_fp);
  CHECK_ARITH_OP(ADD,  reduce,     fp);
  CHECK_ARITH_OP(SUB,  not_reduce, fp);
  CHECK_ARITH_OP(MULT, reduce,     fp);
  CHECK_ARITH_OP(MIN,  reduce,     fp);
  CHECK_ARITH_OP(MAX,  reduce,     fp);
  CHECK_ARITH_OP(INC,  not_reduce, fp);
  CHECK_ARITH_OP(DEC,  not_reduce, fp);

  CHECK_ACCESSOR(SET,   not_fetch);
  CHECK_ACCESSOR(CAS,   not_fetch);
  CHECK_ACCESSOR(GET,   fetch);
  CHECK_ACCESSOR(SWAP,  fetch);
  CHECK_ACCESSOR(FCAS,  fetch);

  CHECK_USER(USER);
  CHECK_USER(USER_NC);

  #undef _CHECK_OP
  #undef CHECK_ARITH_OP
  #undef CHECK_ACCESSOR
  #undef CHECK_USER
  #undef gasneti_op_not_reduce
  #undef gasneti_op_not_fetch
  #undef gasneti_op_not_fp

  #if WORDS_BIGENDIAN
    #if PLATFORM_ARCH_LITTLE_ENDIAN
      #error endianness disagreement: PLATFORM_ARCH_LITTLE_ENDIAN and WORDS_BIGENDIAN are both set
    #endif
    gasneti_assert_always(!gasneti_isLittleEndian());
  #else
    #if PLATFORM_ARCH_BIG_ENDIAN
      #error endianness disagreement: PLATFORM_ARCH_BIG_ENDIAN and !WORDS_BIGENDIAN
    #endif
    gasneti_assert_always(gasneti_isLittleEndian());
  #endif

  /* check GASNET_PAGESIZE is a power of 2 and > 0 */
  gasneti_static_assert(GASNET_PAGESIZE > 0);
  gasneti_static_assert(GASNETI_POWEROFTWO(GASNET_PAGESIZE));

  gasneti_static_assert(SIZEOF_GEX_RMA_VALUE_T == sizeof(gex_RMA_Value_t));
  gasneti_static_assert(SIZEOF_GEX_RMA_VALUE_T >= sizeof(int));
  gasneti_static_assert(SIZEOF_GEX_RMA_VALUE_T >= sizeof(void *));

  #if    PLATFORM_ARCH_32 && !PLATFORM_ARCH_64
    gasneti_static_assert(sizeof(void*) == 4);
  #elif !PLATFORM_ARCH_32 &&  PLATFORM_ARCH_64
    gasneti_static_assert(sizeof(void*) == 8);
  #else
    #error must #define exactly one of PLATFORM_ARCH_32 or PLATFORM_ARCH_64
  #endif

  #if defined(GASNETI_UNI_BUILD)
    if (gasneti_cpu_count() > 1) 
      gasneti_fatalerror("GASNet was built in uniprocessor (non-SMP-safe) configuration, "
        "but executed on an SMP. Please re-run GASNet configure with --enable-smp-safe and rebuild");
  #endif

  { static int firstcall = 1;
    if (firstcall) { /* miscellaneous conduit-independent initializations */
      firstcall = 0;
      #if GASNET_DEBUG && GASNETI_THREADS
        gasneti_threadkey_init(gasneti_throttledebug_key);
      #endif
      gasneti_memcheck_all();
    }
  }
}

static void gasneti_check_portable_conduit(void);
static void gasneti_check_architecture(void);
int gasneti_malloc_munmap_disabled = 0;
extern void gasneti_check_config_postattach(void) {
  gasneti_check_config_preinit();

  /*  verify sanity of the core interface */
  gasneti_assert_always_uint(gex_AM_MaxArgs() ,>=, 2*MAX(sizeof(int),sizeof(void*)));      
  gasneti_assert_always_uint(gex_AM_LUBRequestMedium() ,>=, 512);
  gasneti_assert_always_uint(gex_AM_LUBReplyMedium() ,>=, 512);
  gasneti_assert_always_uint(gex_AM_LUBRequestLong() ,>=, 512);
  gasneti_assert_always_uint(gex_AM_LUBReplyLong() ,>=, 512);

  gasneti_assert_always_uint(gasneti_nodes ,>=, 1);
  gasneti_assert_always_uint(gasneti_mynode ,<, gasneti_nodes);
  { static int firstcall = 1;
    if (firstcall) { /* miscellaneous conduit-independent initializations */
      firstcall = 0;

      #ifndef GASNET_DISABLE_MUNMAP_DEFAULT
      #define GASNET_DISABLE_MUNMAP_DEFAULT 0
      #endif
      if (gasneti_getenv_yesno_withdefault("GASNET_DISABLE_MUNMAP",GASNET_DISABLE_MUNMAP_DEFAULT)) {
        #if HAVE_PTMALLOC                                        
          mallopt(M_TRIM_THRESHOLD, -1);
          mallopt(M_MMAP_MAX, 0);
          GASNETI_TRACE_PRINTF(I,("Setting mallopt M_TRIM_THRESHOLD=-1 and M_MMAP_MAX=0"));
          gasneti_malloc_munmap_disabled = 1;
        #else
          if (gasneti_verboseenv()) 
            gasneti_console0_message("WARNING","GASNET_DISABLE_MUNMAP set on an unsupported platform");
          else
            GASNETI_TRACE_PRINTF(I,("WARNING: GASNET_DISABLE_MUNMAP set on an unsupported platform"));
        #endif
      }
      #if GASNET_NDEBUG
        gasneti_check_portable_conduit();
        gasneti_check_architecture();
      #endif
    }
  }
  gasneti_memcheck_all();

  gasneti_flush_streams();  // flush above messages, and ensure FS_SYNC envvar is initted
}

/* ------------------------------------------------------------------------------------ */
// Helpers for debug checks

#if GASNET_DEBUG
void gasneti_check_inject(int for_reply GASNETI_THREAD_FARG) {
  gasneti_threaddata_t * const mythread = GASNETI_MYTHREAD;
  if (!mythread) return; // Some conduits communicate very early

  if (mythread->reply_handler_active) {
    gasneti_fatalerror("Invalid GASNet call (communication injection or poll) while executing a Reply handler");
  }
  if (mythread->request_handler_active && !for_reply) {
    gasneti_fatalerror("Invalid GASNet call (communication injection or poll) while executing a Request handler");
  }

  // NPAM checks are distinct to allow that entire subsytem to be overridden
  gasneti_checknpam(for_reply GASNETI_THREAD_PASS);

  // TODO: check for HSL context
}

// Resets all state indicative of restricted context.
// This is intended for use within `gasnet-exit()` which *is* valid from
// handler context, and is known to run with HSLs held on error paths.
// There is currently no other known-valid reason to use this call.
void gasneti_check_inject_reset(GASNETI_THREAD_FARG_ALONE) {
  gasneti_threaddata_t * const mythread = GASNETI_MYTHREAD;
  if (!mythread) return; // Some conduits communicate very early
  mythread->reply_handler_active = 0;
  mythread->request_handler_active = 0;
  // TODO: reset HSL context
}
#endif

/* ------------------------------------------------------------------------------------ */
#ifndef _GASNET_ERRORNAME
extern const char *gasnet_ErrorName(int errval) {
  switch (errval) {
    case GASNET_OK:           return "GASNET_OK";      
    case GASNET_ERR_NOT_INIT: return "GASNET_ERR_NOT_INIT";      
    case GASNET_ERR_BAD_ARG:  return "GASNET_ERR_BAD_ARG";       
    case GASNET_ERR_RESOURCE: return "GASNET_ERR_RESOURCE";      
    case GASNET_ERR_BARRIER_MISMATCH: return "GASNET_ERR_BARRIER_MISMATCH";      
    case GASNET_ERR_NOT_READY: return "GASNET_ERR_NOT_READY";      
    default: return "*unknown*";
  }
}
#endif

#ifndef _GASNET_ERRORDESC
extern const char *gasnet_ErrorDesc(int errval) {
  switch (errval) {
    case GASNET_OK:           return "No error";      
    case GASNET_ERR_NOT_INIT: return "GASNet message layer not initialized"; 
    case GASNET_ERR_BAD_ARG:  return "Invalid function parameter passed";    
    case GASNET_ERR_RESOURCE: return "Problem with requested resource";      
    case GASNET_ERR_BARRIER_MISMATCH: return "Barrier id's mismatched";      
    case GASNET_ERR_NOT_READY: return "Non-blocking operation not complete";      
    default: return "no description available";
  }
}
#endif
/* ------------------------------------------------------------------------------------ */
extern void gasneti_freezeForDebugger(void) {
  if (gasneti_getenv_yesno_withdefault("GASNET_FREEZE",0)) {
    gasneti_freezeForDebuggerNow(&gasnet_frozen,"gasnet_frozen");
  }
}
/* ------------------------------------------------------------------------------------ */
// Client management

#ifdef GASNETC_CLIENT_EXTRA_DECLS
GASNETC_CLIENT_EXTRA_DECLS
#endif

#ifndef _GEX_CLIENT_T
#ifndef gasneti_import_client
gasneti_Client_t gasneti_import_client(gex_Client_t _client) {
  const gasneti_Client_t _real_client = GASNETI_IMPORT_POINTER(gasneti_Client_t,_client);
  GASNETI_IMPORT_MAGIC(_real_client, CLIENT);
  return _real_client;
}
#endif

#ifndef gasneti_export_client
gex_Client_t gasneti_export_client(gasneti_Client_t _real_client) {
  GASNETI_CHECK_MAGIC(_real_client, GASNETI_CLIENT_MAGIC);
  return GASNETI_EXPORT_POINTER(gex_Client_t, _real_client);
}
#endif

// TODO-EX: either ensure name is unique OR perform "auto-increment" according to flags
gasneti_Client_t gasneti_alloc_client(
                       const char *name,
                       gex_Flags_t flags)
{
  gasneti_Client_t client;
#ifdef GASNETC_SIZEOF_CLIENT_T
  size_t alloc_size = GASNETC_SIZEOF_CLIENT_T();
  gasneti_assert_uint(alloc_size ,>=, sizeof(*client));
#else
  size_t alloc_size = sizeof(*client);
#endif
  client = gasneti_malloc(alloc_size);
  GASNETI_INIT_MAGIC(client, GASNETI_CLIENT_MAGIC);
  client->_tm0 = NULL;
  client->_name = gasneti_strdup(name);
  client->_cdata = NULL;
  client->_flags = flags;
  gasneti_assert_always(sizeof(client->_next_ep_index) >= sizeof(gex_EP_Index_t));
  gasneti_weakatomic32_set(&client->_next_ep_index, 0, 0);
  memset(client->_ep_tbl, 0, sizeof(client->_ep_tbl));
#ifdef GASNETC_CLIENT_INIT_HOOK
  GASNETC_CLIENT_INIT_HOOK(client);
#else
  size_t extra = alloc_size - sizeof(*client);
  if (extra) memset(client + 1, 0, extra);
#endif
  return client;
}

void gasneti_free_client(gasneti_Client_t client)
{
#ifdef GASNETI_CLIENT_FINI_HOOK
  GASNETI_CLIENT_FINI_HOOK(client);
#endif
  gasneti_free((/*non-const*/char*)client->_name);
  GASNETI_INIT_MAGIC(client, GASNETI_CLIENT_BAD_MAGIC);
  gasneti_free(client);
}
#endif // _GEX_CLIENT_T

/* ------------------------------------------------------------------------------------ */
// Segment management

#ifdef GASNETC_SEGMENT_EXTRA_DECLS
GASNETC_SEGMENT_EXTRA_DECLS
#endif

#ifndef _GEX_SEGMENT_T
#ifndef gasneti_import_segment
gasneti_Segment_t gasneti_import_segment(gex_Segment_t _segment) {
  const gasneti_Segment_t _real_segment = GASNETI_IMPORT_POINTER(gasneti_Segment_t,_segment);
  GASNETI_IMPORT_MAGIC(_real_segment, SEGMENT);
  return _real_segment;
}
#endif

#ifndef gasneti_export_segment
gex_Segment_t gasneti_export_segment(gasneti_Segment_t _real_segment) {
  GASNETI_CHECK_MAGIC(_real_segment, GASNETI_SEGMENT_MAGIC);
  return GASNETI_EXPORT_POINTER(gex_Segment_t, _real_segment);
}
#endif

const gasnet_seginfo_t gasneti_null_segment = {0};

// TODO-EX: probably need to add to a per-client container of some sort
gasneti_Segment_t gasneti_alloc_segment(
                       gasneti_Client_t client,
                       void *addr,
                       uintptr_t size,
                       gex_MK_t kind,
                       int client_allocated,
                       gex_Flags_t flags)
{
  gasneti_Segment_t segment;
#ifdef GASNETC_SIZEOF_SEGMENT_T
  size_t alloc_size = GASNETC_SIZEOF_SEGMENT_T();
  gasneti_assert_uint(alloc_size ,>=, sizeof(*segment));
#else
  size_t alloc_size = sizeof(*segment);
#endif
  segment = gasneti_malloc(alloc_size);
  GASNETI_INIT_MAGIC(segment, GASNETI_SEGMENT_MAGIC);
  segment->_client = client;
  segment->_cdata = NULL;
  segment->_kind = kind;
  segment->_flags = flags;
  segment->_addr = addr;
  segment->_ub = (void*)((uintptr_t)addr + size);
  segment->_size = size;
  segment->_client_allocated = client_allocated;
#ifdef GASNETC_SEGMENT_INIT_HOOK
  GASNETC_SEGMENT_INIT_HOOK(segment);
#else
  size_t extra = alloc_size - sizeof(*segment);
  if (extra) memset(segment + 1, 0, extra);
#endif
  return segment;
}

void gasneti_free_segment(gasneti_Segment_t segment)
{
#ifdef GASNETI_SEGMENT_FINI_HOOK
  GASNETI_SEGMENT_FINI_HOOK(segment);
#endif
  GASNETI_INIT_MAGIC(segment, GASNETI_SEGMENT_BAD_MAGIC);
  gasneti_free(segment);
}
#endif // _GEX_SEGMENT_T

extern int gex_Segment_Attach(
                gex_Segment_t          *segment_p,
                gex_TM_t               e_tm,
                uintptr_t              length)
{
  gasneti_TM_t i_tm = gasneti_import_tm_nonpair(e_tm);

  GASNETI_TRACE_PRINTF(O,("gex_Segment_Attach: segment_p=%p tm="GASNETI_TMSELFFMT" length=%"PRIuPTR,
                          (void*)segment_p, GASNETI_TMSELFSTR(e_tm), length));
  GASNETI_CHECK_INJECT();

  if (! segment_p) {
    gasneti_fatalerror("Invalid call to gex_Segment_Attach with NULL segment_p");
  }

  if (! e_tm) {
    gasneti_fatalerror("Invalid call to gex_Segment_Attach with NULL e_tm");
  }

  // TODO-EX: remove if/when this limitation is removed
  static int once = 1;
  if (once) once = 0;
  else gasneti_fatalerror("gex_Segment_Attach: current implementation can be called at most once");

  #if GASNET_SEGMENT_EVERYTHING
    *segment_p = GEX_SEGMENT_INVALID;
    gex_Event_Wait(gex_Coll_BarrierNB(e_tm, 0));
  #else
    /* create a segment collectively */
    // TODO-EX: this implementation only works *once*
    // TODO-EX: need to pass proper flags (e.g. pshm and bind) instead of 0
    if (GASNET_OK != gasneti_segmentAttach(segment_p, e_tm, length, 0)) {
      GASNETI_RETURN_ERRR(RESOURCE,"Error attaching segment");
    }
  #endif

  #if GASNETC_SEGMENT_ATTACH_HOOK
    if (GASNET_OK != gasnetc_segment_attach_hook(*segment_p, e_tm)) {
      GASNETI_RETURN_ERRR(RESOURCE,"Error attaching segment (conduit hook)");
    }
  #endif

  return GASNET_OK;
}

extern int gex_Segment_Create(
                gex_Segment_t           *segment_p,
                gex_Client_t            e_client,
                gex_Addr_t              address,
                uintptr_t               length,
                gex_MK_t                kind,
                gex_Flags_t             flags)
{
  gasneti_Client_t i_client = gasneti_import_client(e_client);

  // TODO: tracing of "kind"
  GASNETI_TRACE_PRINTF(O,("gex_Segment_Create: client='%s' address=%p length=%"PRIuPTR" flags=%d",
                          i_client ? i_client->_name : "(NULL)", address, length, flags));
  GASNETI_CHECK_INJECT();

  if (! segment_p) {
    gasneti_fatalerror("Invalid call to gex_Segment_Create() with NULL segment_p");
  }
  if (! i_client) {
    gasneti_fatalerror("Invalid call to gex_Segment_Create() with NULL client");
  }
  if (flags) {
    gasneti_fatalerror("Invalid call to gex_Segment_Create() with non-zero flags");
  }
  if (! length) {
    gasneti_fatalerror("Invalid call to gex_Segment_Create() with zero length");
  }
  if (kind == GEX_MK_INVALID) {
    gasneti_fatalerror("Invalid call to gex_Segment_Create() with kind = GEX_MK_INVALID");
  }

  // Create the Segment object, allocating memory if appropriate
  int rc = gasneti_segmentCreate(segment_p, i_client, address, length, kind, flags);

  #if GASNETC_SEGMENT_CREATE_HOOK
    if (rc == GASNET_OK) {
      rc = gasnetc_segment_create_hook(*segment_p);
      if (rc) { // Conduit hook failed.  So cleanup conduit-independent resources
        gasneti_Segment_t i_segment = gasneti_import_segment(*segment_p);
        gasneti_assert_zeroret( gasneti_segmentDestroy(i_segment, 0) );
      }
    }
  #endif

  GASNETI_RETURN(rc);
}

extern void gex_Segment_Destroy(
                gex_Segment_t           e_segment,
                gex_Flags_t             flags)
{
  GASNETI_TRACE_PRINTF(O,("gex_Segment_Destroy: segment=%p flags=%d",
                          (void*)e_segment, flags));
  GASNETI_CHECK_INJECT();

  if (!e_segment) {
    gasneti_fatalerror("Invalid call to gex_Segment_Destroy() with NULL segment");
  }
  if (flags) {
    gasneti_fatalerror("Invalid call to gex_Segment_Destroy() with non-zero flags");
  }
  // TODO: check reference count, once implemented

  gasneti_Segment_t i_segment = gasneti_import_segment(e_segment);
  gasneti_assert_zeroret( gasneti_segmentDestroy(i_segment, 1) );
}

/* ------------------------------------------------------------------------------------ */
// Endpoint management

#ifdef GASNETC_EP_EXTRA_DECLS
GASNETC_EP_EXTRA_DECLS
#endif

#ifndef _GEX_EP_T
#ifndef gasneti_import_ep
gasneti_EP_t gasneti_import_ep(gex_EP_t _ep) {
  const gasneti_EP_t _real_ep = GASNETI_IMPORT_POINTER(gasneti_EP_t,_ep);
  GASNETI_IMPORT_MAGIC(_real_ep, EP);
  return _real_ep;
}
#endif

#ifndef gasneti_export_ep
gex_EP_t gasneti_export_ep(gasneti_EP_t _real_ep) {
  GASNETI_CHECK_MAGIC(_real_ep, GASNETI_EP_MAGIC);
  return GASNETI_EXPORT_POINTER(gex_EP_t, _real_ep);
}
#endif

// Static on the assumption that all callers will reside in this file
// TODO: might subsume into gex_EP_Create() if there are no other callers
static gasneti_EP_t gasneti_alloc_ep(
                       gasneti_Client_t client,
                       gex_EP_Capabilities_t caps,
                       gex_Flags_t flags,
                       int new_index)
{
  gasneti_EP_t endpoint;
#ifdef GASNETC_SIZEOF_EP_T
  size_t alloc_size = GASNETC_SIZEOF_EP_T();
  gasneti_assert_uint(alloc_size ,>=, sizeof(*endpoint));
#else
  size_t alloc_size = sizeof(*endpoint);
#endif
  endpoint = gasneti_malloc(alloc_size);
  GASNETI_INIT_MAGIC(endpoint, GASNETI_EP_MAGIC);
  endpoint->_client = client;
  endpoint->_cdata = NULL;
  endpoint->_segment = NULL;
  endpoint->_orig_caps = endpoint->_caps = caps;
  endpoint->_flags = flags;
  endpoint->_index = new_index;
  gasneti_assert(! client->_ep_tbl[new_index]);
  client->_ep_tbl[new_index] = endpoint;
  gasneti_amtbl_init(endpoint);
#ifndef GASNETC_EP_INIT_HOOK
  size_t extra = alloc_size - sizeof(*endpoint);
  if (extra) memset(endpoint + 1, 0, extra);
#endif
  return endpoint;
}

// Static on the assumption that all callers will reside in this file
static void gasneti_free_ep(gasneti_EP_t endpoint)
{
#ifdef GASNETI_EP_FINI_HOOK
  GASNETI_EP_FINI_HOOK(endpoint);
#endif
  GASNETI_INIT_MAGIC(endpoint, GASNETI_EP_BAD_MAGIC);
  gasneti_free(endpoint);
}
#endif // _GEX_EP_T

extern int gex_EP_Create(
            gex_EP_t               *ep_p,
            gex_Client_t           e_client,
            gex_EP_Capabilities_t  caps,
            gex_Flags_t            flags)
{
  gasneti_Client_t client = gasneti_import_client(e_client);

  // TODO: formatted printing for capabilities
  GASNETI_TRACE_PRINTF(O,("gex_EP_Create: client='%s' capabilities=%d flags=%d",
                          client ? client->_name : "(NULL)", caps, flags));
  GASNETI_CHECK_INJECT();

  if (! client) {
    gasneti_fatalerror("Invalid call to gex_EP_Create with NULL client");
  }

  if (!ep_p) {
    gasneti_fatalerror("Invalid call to gex_EP_Create with NULL ep_p");
  }

  GASNETI_CHECK_ERRR((! caps), BAD_ARG,
                     "no capabilities were requested");
  GASNETI_CHECK_ERRR((caps & ~GEX_EP_CAPABILITY_ALL), BAD_ARG,
                     "invalid capabilities were requested");

  // Currently require/demand that primordial EP have ALL capabilities
  gasneti_assert(gasneti_weakatomic32_read(&client->_next_ep_index, 0)
                 || caps == GEX_EP_CAPABILITY_ALL);

  // TODO: any other validation of caps
  // TODO: maybe silently OR-in {VIS,AD,COLL} dependencies?

  // TODO: any validation of flags? any conditional behaviors?

  uint32_t new_index = gasneti_weakatomic32_add(&client->_next_ep_index, 1, 0) - 1;
  if_pf (new_index >= GASNET_MAXEPS) {
    gasneti_weakatomic32_decrement(&client->_next_ep_index, 0);
    GASNETI_RETURN_ERRR(RESOURCE,"would exceed per-client EP limit of " _STRINGIFY(GASNET_MAXEPS));
  }
    
  gasneti_EP_t ep = gasneti_alloc_ep(client, caps, flags, new_index);

  // TODO: any need/want to omit on non-primordial EPs?
  { /*  core API handlers */
    gex_AM_Entry_t *ctable = (gex_AM_Entry_t *)gasnetc_get_handlertable();
    int len = 0;
    int numreg = 0;
    gasneti_assert(ctable);
    while (ctable[len].gex_fnptr) len++; /* calc len */
    if (gasneti_amregister(ep, ctable, len,
                           GASNETC_HANDLER_BASE, GASNETE_HANDLER_BASE,
                           0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering core API handlers");
    gasneti_assert_int(numreg ,==, len);
  }

  // TODO: any need/want to omit on non-primordial EPs?
  { /*  extended API handlers */
    gex_AM_Entry_t *etable = (gex_AM_Entry_t *)gasnete_get_handlertable();
    int len = 0;
    int numreg = 0;
    gasneti_assert(etable);
    while (etable[len].gex_fnptr) len++; /* calc len */
    if (gasneti_amregister(ep, etable, len,
                           GASNETE_HANDLER_BASE, GASNETI_CLIENT_HANDLER_BASE,
                           0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering extended API handlers");
    gasneti_assert_int(numreg ,==, len);
  }

#ifdef GASNETC_EP_INIT_HOOK
  int rc = GASNETC_EP_INIT_HOOK(ep);
  if (rc != GASNET_OK) {
    gasneti_free_ep(ep);
    ep = NULL;
  }
#else
  int rc = GASNET_OK;
#endif

  *ep_p = gasneti_export_ep(ep);
  return rc;
}

/* ------------------------------------------------------------------------------------ */
// TM management

#ifdef GASNETC_TM_EXTRA_DECLS
GASNETC_TM_EXTRA_DECLS
#endif


#ifndef _GEX_TM_T
#ifndef gasneti_import_tm
gasneti_TM_t gasneti_import_tm(gex_TM_t _tm) {
  gasneti_assert(_tm != GEX_TM_INVALID);
  const gasneti_TM_t _real_tm = GASNETI_IMPORT_POINTER(gasneti_TM_t,_tm);
  if (! gasneti_i_tm_is_pair(_real_tm)) {
    GASNETI_IMPORT_MAGIC(_real_tm, TM);
  }
  return _real_tm;
}
#endif

#ifndef gasneti_import_tm_nonpair
gasneti_TM_t gasneti_import_tm_nonpair(gex_TM_t _tm) {
  gasneti_assert(_tm != GEX_TM_INVALID);
  const gasneti_TM_t _real_tm = GASNETI_IMPORT_POINTER(gasneti_TM_t,_tm);
  if (gasneti_i_tm_is_pair(_real_tm)) {
    gasneti_fatalerror("Invalid use of a TM-Pair where such is prohibited");
  }
  GASNETI_IMPORT_MAGIC(_real_tm, TM);
  return _real_tm;
}
#endif

#ifndef gasneti_export_tm
gex_TM_t gasneti_export_tm(gasneti_TM_t _real_tm) {
  GASNETI_CHECK_MAGIC(_real_tm, GASNETI_TM_MAGIC);
  return GASNETI_EXPORT_POINTER(gex_TM_t, _real_tm);
}
#endif

// TODO-EX: probably need to add to a per-client container of some sort
extern gasneti_TM_t gasneti_alloc_tm(
                       gasneti_EP_t ep,
                       gex_Rank_t rank,
                       gex_Rank_t size,
                       gex_Flags_t flags)
{
  gasneti_assert_uint(rank ,<, size);
  gasneti_assert_uint(size ,>, 0);

  gasneti_assert(ep);
  gasneti_assert(ep->_client);
  const int is_tm0 = (ep->_client->_tm0 == NULL);

  gasneti_TM_t tm;
#ifdef GASNETC_SIZEOF_TM_T
  size_t actual_sz = GASNETC_SIZEOF_TM_T();
  gasneti_assert_uint(actual_sz ,>=, sizeof(*tm));
#else
  size_t actual_sz = sizeof(*tm);
#endif

#if GASNETI_TM0_ALIGN
  // TM0 is aligned to GASNETI_TM0_ALIGN, and all others to half that
  size_t disalign = (is_tm0 ? 0 : GASNETI_TM0_ALIGN/2);
  tm = (gasneti_TM_t)(disalign + (uintptr_t)gasneti_malloc_aligned(GASNETI_TM0_ALIGN, actual_sz + disalign));
#else
  tm = gasneti_malloc(actual_sz);
#endif

  GASNETI_INIT_MAGIC(tm, GASNETI_TM_MAGIC);
  tm->_ep = ep;
  tm->_cdata = NULL;
  tm->_flags = flags;
  tm->_rank = rank;
  tm->_size = size;
  tm->_coll_team = NULL;
#ifdef GASNETC_TM_INIT_HOOK
  GASNETC_TM_INIT_HOOK(tm);
#else
  size_t extra = actual_sz - sizeof(*tm);
  if (extra) memset(tm + 1, 0, extra);
#endif
  
  if (is_tm0) {
    gasneti_legacy_alloc_tm_hook(tm); // init g2ex layer if appropriate

    ep->_client->_tm0 = tm;

    // TODO-EX: Please remove this!
    gasneti_assert(! gasneti_thing_that_goes_thunk_in_the_dark);
    gasneti_thing_that_goes_thunk_in_the_dark = tm;
  }

  return tm;
}

void gasneti_free_tm(gasneti_TM_t tm)
{
#ifdef GASNETI_TM_FINI_HOOK
  GASNETI_TM_FINI_HOOK(tm);
#endif
  GASNETI_INIT_MAGIC(tm, GASNETI_TM_BAD_MAGIC);
  gasneti_free_aligned((void*)((uintptr_t)tm - (GASNETI_TM0_ALIGN/2)));
}
#endif // _GEX_TM_T

/* ------------------------------------------------------------------------------------ */

// TM-pair is NOT an object type, but must masquerade as a gex_TM_t.
// Therefore, we handle swizzling and internal/external type distinction
// in the same manner as for object types (but no MAGIC).

#ifndef gasneti_import_tm_pair
gasneti_TM_Pair_t gasneti_import_tm_pair(gex_TM_t tm) {
  gasneti_assert(tm != GEX_TM_INVALID);
  gasneti_assert(gasneti_e_tm_is_pair(tm));
  return GASNETI_IMPORT_POINTER(gasneti_TM_Pair_t,tm);
}
#endif

#ifndef gasneti_export_tm_pair
gex_TM_t gasneti_export_tm_pair(gasneti_TM_Pair_t tm_pair) {
  gasneti_assert(gasneti_i_tm_is_pair((gasneti_TM_t) tm_pair));
  return GASNETI_EXPORT_POINTER(gex_TM_t, tm_pair);
}
#endif

// Helper for "mappable" queries
// return the bound segment for local ep_idx
// i_tm is provided only to retrieve the Client
gasneti_Segment_t gasneti_epidx_to_segment(gasneti_TM_t i_tm, gex_EP_Index_t ep_idx) {
   // TODO: multi-client would extract from i_tm OR signature may change to take client
   gasneti_Client_t i_client = gasneti_import_client(gasneti_THUNK_CLIENT);

   gasneti_assert_int(ep_idx ,<, GASNET_MAXEPS);
   gasneti_assert_int(ep_idx ,<, gasneti_weakatomic32_read(&i_client->_next_ep_index, 0));
   gasneti_EP_t i_ep = i_client->_ep_tbl[ep_idx];   
   gasneti_assert(i_ep);
   return i_ep->_segment;
}

#if GASNET_DEBUG && GASNET_HAVE_MK_CLASS_MULTIPLE
// Helper for bounds checking local address range for host-vs-device bound segment.
// Local address must match kind of bound segment, if any
//
// If bound segment kind is device, the local address must be in-segment.
//   Returns GASNETI_BAD_LOCAL_OUTSIDE_DEVICE_SEGMENT if this constraint is violated
// Otherwise, the local address must not be in any device segment
//   Returns GASNETI_BAD_LOCAL_INSIDE_DEVICE_SEGMENT if this constraint is violated
// Returns 0 in the absence of violations
// Returns the device segment via *segment_p if either constraint is violated
int _gasneti_boundscheck_local(gex_TM_t tm, void *addr, size_t len, gasneti_Segment_t *segment_p)
{
  gasneti_EP_t i_ep = gasneti_e_tm_to_i_ep(tm);
  gasneti_Segment_t i_bound_segment = i_ep->_segment;
  gex_Segment_t e_bound_segment = gasneti_export_segment(i_bound_segment);

  if (!len || (e_bound_segment && _gasneti_in_segment_t(addr, len, e_bound_segment))) {
    // Zero-length, or in the bound segment (if any) are always OK.
  } else if (! gasneti_i_segment_kind_is_host(i_bound_segment)) {
    // Local "device memory" must never be outside the bound segment.
    *segment_p = i_bound_segment;
    return GASNETI_BAD_LOCAL_OUTSIDE_DEVICE_SEGMENT;
  } else if (gasneti_in_local_auxsegment(i_ep, addr, len)) {
    // When not bound to device segment, local aux segment is OK.
  } else {
    // Search segment table for a match
    // If a match is found, it must not be a device segment, but no match is fine too
    gasneti_Segment_t i_segment;
    GASNETI_SEGTBL_LOCK();
    GASNETI_SEGTBL_FOR_EACH(i_segment) {
      gex_Segment_t e_segment = gasneti_export_segment(i_segment);
      if (_gasneti_in_segment_t(addr, len, e_segment)) {
        if (gasneti_i_segment_kind_is_host(i_segment)) break;
        GASNETI_SEGTBL_UNLOCK();
        *segment_p = i_segment;
        return GASNETI_BAD_LOCAL_INSIDE_DEVICE_SEGMENT;
      }
    }
    GASNETI_SEGTBL_UNLOCK();
  }
  *segment_p = NULL;
  return 0;
}
#endif

/* ------------------------------------------------------------------------------------ */

#if GASNET_DEBUG
  // Verify that client did actually write to gasnet-allocated buffer
  //
  // gasneti_init_sd_poison(sd) - write a "canary"
  //   For (sd->_size >= gasneti_sd_init_len) writes a "canary" value (also of length
  //   gasneti_sd_init_len) to sd->_addr if (and only if) the buffer is gasnet-owned
  // gasneti_test_sd_poison(addr, len) - test a "canary"
  //   For (len >= gasneti_sd_init_len) looks for the same "canary" value,
  //   returning non-zero if it is present.
  // Note that 'len' at "test" may be less than the one given at "init",
  // as is permitted for the nbytes values passed to Prepare/Commit.

  static uint64_t gasneti_memalloc_envint(const char *name, const char *deflt);
  static void gasneti_memalloc_valset(void *p, size_t len, uint64_t val);
  static const void *gasneti_memalloc_valcmp(const void *p, size_t len, uint64_t val);

  static int gasneti_sd_init_enabled = 1;
  static uint64_t gasneti_sd_init_val = 0; // Value used to initialize gasnet-allocated SrcDesc buffers
  static size_t gasneti_sd_init_len = 128; // Max length to init at Prepare, and min to check at Commit

  extern void gasneti_init_sd_poison(gasneti_AM_SrcDesc_t sd) {
    if (!gasneti_sd_init_enabled) return;
    if (sd->_addr != sd->_gex_buf) return;
    gasneti_assert_uint(((uintptr_t)sd->_addr) % GASNETI_MEDBUF_ALIGNMENT ,==, 0);
    static int isinit = 0;
    if_pf (!isinit) {
      static gasneti_mutex_t lock = GASNETI_MUTEX_INITIALIZER;
      gasneti_mutex_lock(&lock);
        if (!isinit) {
          gasneti_sd_init_enabled = gasneti_getenv_yesno_withdefault("GASNET_SD_INIT",1);
          gasneti_sd_init_val = gasneti_memalloc_envint("GASNET_SD_INITVAL","NAN");
          gasneti_sd_init_len = MAX((int64_t)1,gasneti_getenv_int_withdefault("GASNET_SD_INITLEN",128,0));
          isinit = 1;
        }
      gasneti_mutex_unlock(&lock);
      if (!gasneti_sd_init_enabled) return;
    } else gasneti_sync_reads();
    if (sd->_size < gasneti_sd_init_len) return;
    gasneti_memalloc_valset(sd->_addr, gasneti_sd_init_len, gasneti_sd_init_val);
  }

  extern int gasneti_test_sd_poison(void *addr, size_t len) { // return non-zero if still poison
    return gasneti_sd_init_enabled &&
           (len >= gasneti_sd_init_len) &&
           !gasneti_memalloc_valcmp(addr, gasneti_sd_init_len, gasneti_sd_init_val);
  }
#endif

/* ------------------------------------------------------------------------------------ */

#ifndef GASNETC_FATALSIGNAL_CALLBACK
#define GASNETC_FATALSIGNAL_CALLBACK(sig)
#endif
#ifndef GASNETC_FATALSIGNAL_CLEANUP_CALLBACK
#define GASNETC_FATALSIGNAL_CLEANUP_CALLBACK(sig)
#endif

void gasneti_defaultSignalHandler(int sig) {
  gasneti_sighandlerfn_t oldsigpipe = NULL;
  const char *signame =  gasnett_signame_fromval(sig);

  gasneti_assert(signame);

  switch (sig) {
    case SIGQUIT:
      /* client didn't register a SIGQUIT handler, so just exit */
      gasnet_exit(1);
      break;
    case SIGABRT:
    case SIGILL:
    case SIGSEGV:
    case SIGBUS:
    case SIGFPE: {
      oldsigpipe = gasneti_reghandler(SIGPIPE, SIG_IGN);

      GASNETC_FATALSIGNAL_CALLBACK(sig); /* give conduit first crack at it */

      gasneti_console_message("Caught a fatal signal", "%s(%i)", signame, sig);

      gasnett_freezeForDebuggerErr(); /* allow freeze */

      gasneti_print_backtrace_ifenabled(STDERR_FILENO); /* try to print backtrace */

      // Try to flush I/O (especially the tracefile) before crashing
      signal(SIGALRM, _exit); alarm(5); 
      gasneti_flush_streams();
      alarm(0);

      (void) gasneti_reghandler(SIGPIPE, oldsigpipe);

      GASNETC_FATALSIGNAL_CLEANUP_CALLBACK(sig); /* conduit hook to kill the job */

      signal(sig, SIG_DFL); /* restore default core-dumping handler and re-raise */
      gasneti_raise(sig);
      break;
    }
    default: 
      /* translate signal to SIGQUIT */
      { static int sigquit_raised = 0;
        if (sigquit_raised) {
          /* sigquit was already raised - we cannot safely reraise it, so just die */
          _exit(1);
        } else sigquit_raised = 1;
      }

      oldsigpipe = gasneti_reghandler(SIGPIPE, SIG_IGN);
      gasneti_console_message("Caught a signal", "%s(%i)", signame, sig);
      (void) gasneti_reghandler(SIGPIPE, oldsigpipe);

      gasneti_raise(SIGQUIT);
  }
}

extern int gasneti_set_waitmode(int wait_mode) {
  const char *desc = NULL;
  GASNETI_CHECKINIT();
  switch (wait_mode) {
    case GASNET_WAIT_SPIN:      desc = "GASNET_WAIT_SPIN"; break;
    case GASNET_WAIT_BLOCK:     desc = "GASNET_WAIT_BLOCK"; break;
    case GASNET_WAIT_SPINBLOCK: desc = "GASNET_WAIT_SPINBLOCK"; break;
    default:
      GASNETI_RETURN_ERRR(BAD_ARG, "illegal wait mode");
  }
  GASNETI_TRACE_PRINTF(I, ("gasnet_set_waitmode(%s)", desc));
  #ifdef gasnetc_set_waitmode
    gasnetc_set_waitmode(wait_mode);
  #endif
  gasneti_wait_mode = wait_mode;
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/* Global environment variable handling */

extern char **environ; 

static void gasneti_serializeEnvironment(uint8_t **pbuf, int *psz) {
  /* flatten a snapshot of the environment to make it suitable for transmission
   * here we assume the standard representation where a pointer to the environment 
   * is stored in a global variable 'environ' and the environment is represented as an array 
   * of null-terminated strings where each has the form 'key=value' and value may be empty, 
   * and the final string pointer is a NULL pointer
   * we flatten this into a list of null-terminated 'key=value' strings, 
   * terminated with a double-null
   */
  uint8_t *buf; 
  uint8_t *p;
  int i;
  int totalEnvSize = 0;
  if (!environ) {
    /* T3E stupidly omits environ support, despite documentation to the contrary */
    GASNETI_TRACE_PRINTF(I,("WARNING: environ appears to be empty -- ignoring it"));
    *pbuf = NULL;
    *psz = 0;
    return;
  }
  for(i = 0; environ[i]; i++) 
    totalEnvSize += strlen(environ[i]) + 1;
  totalEnvSize++;

  buf = (uint8_t *)gasneti_malloc(totalEnvSize);
  p = buf;
  p[0] = 0;
  for(i = 0; environ[i]; i++) {
    strcpy((char*)p, environ[i]);
    p += strlen((char*)p) + 1;
    }
  *p = 0;
  gasneti_assert_int((p+1) - buf ,==, totalEnvSize);

  *pbuf = buf;
  *psz = totalEnvSize;
}

extern char *gasneti_globalEnv;

typedef struct {
  int sz;
  uint64_t checksum;
} gasneti_envdesc_t;

/* do the work necessary to setup the global environment for use by gasneti_getenv
   broadcast the environment variables from one node to all nodes
   Note this currently assumes that at least one of the compute nodes has the full
    environment - systems where the environment is not propagated to any compute node
    will need something more sophisticated.
   exchangefn is required function for exchanging data 
   broadcastfn is optional (can be NULL) but highly recommended for scalability
 */
extern void gasneti_setupGlobalEnvironment(gex_Rank_t numnodes, gex_Rank_t mynode,
                                           gasneti_bootstrapExchangefn_t exchangefn,
                                           gasneti_bootstrapBroadcastfn_t broadcastfn) {
  uint8_t *myenv; 
  int sz; 
  uint64_t checksum;
  gasneti_envdesc_t myenvdesc = {0};
  gasneti_envdesc_t *allenvdesc;

  gasneti_assert(exchangefn);

  gasneti_serializeEnvironment(&myenv,&sz);
  checksum = gasneti_checksum(myenv,sz);

  myenvdesc.sz = sz;
  myenvdesc.checksum = checksum;

  allenvdesc = gasneti_malloc(numnodes*sizeof(gasneti_envdesc_t));
  /* gather environment description from all nodes */
  (*exchangefn)(&myenvdesc, sizeof(gasneti_envdesc_t), allenvdesc);

  { /* see if the node environments differ and find the largest */
    int i;
    int rootid = 0;
    int identical = 1;
    gasneti_envdesc_t rootdesc = allenvdesc[rootid];
    for (i=1; i < numnodes; i++) {
      if (rootdesc.checksum != allenvdesc[i].checksum || 
          rootdesc.sz != allenvdesc[i].sz) 
          identical = 0;
      if (allenvdesc[i].sz > rootdesc.sz) { 
        /* assume the largest env is the one we want */
        rootdesc = allenvdesc[i];
        rootid = i;
      }
    }
    if (identical) { /* node environments all identical - don't bother to propagate */
      gasneti_free(allenvdesc);
      gasneti_free(myenv);
      return;
    } else {
      int envsize = rootdesc.sz;
      gasneti_globalEnv = gasneti_malloc(envsize);
      gasneti_leak(gasneti_globalEnv);
      if (broadcastfn) {
        (*broadcastfn)(myenv, envsize, gasneti_globalEnv, rootid);
      } else {
        /* this is wasteful of memory and bandwidth, and non-scalable */
        char *tmp = gasneti_malloc(envsize*numnodes);
        memcpy(tmp+mynode*envsize, myenv, sz);
        (*exchangefn)(tmp+mynode*envsize, envsize, tmp);
        memcpy(gasneti_globalEnv, tmp+rootid*envsize, envsize);
        gasneti_free(tmp);
      }
      gasneti_assert_uint(gasneti_checksum(gasneti_globalEnv,envsize) ,==, rootdesc.checksum);
      gasneti_free(allenvdesc);
      gasneti_free(myenv);
      return;
    }
  }

}

/* decode src into dst, arguments permitted to overlap exactly */
extern size_t gasneti_decodestr(char *dst, const char *src) {
  #define IS_HEX_DIGIT(c)  (isdigit(c) || (isalpha(c) && toupper(c) <= 'F'))
  #define VAL_HEX_DIGIT(c) ((unsigned int)(isdigit(c) ? (c)-'0' : 10 + toupper(c) - 'A'))
  size_t dstidx = 0;
  const char *p = src;
  gasneti_assert(src && dst);
  while (*p) {
    char c;
    if (p[0] == '%' && p[1] == '0' && 
        p[2] && IS_HEX_DIGIT(p[2]) && p[3] && IS_HEX_DIGIT(p[3])) {
      c = (char)(VAL_HEX_DIGIT(p[2]) << 4) | VAL_HEX_DIGIT(p[3]);
      p += 4;
    } else c = *(p++);
    dst[dstidx++] = c;
  }
  dst[dstidx] = '\0';
  return dstidx;
  #undef VAL_HEX_DIGIT
  #undef IS_HEX_DIGIT
}

extern const char *gasneti_decode_envval(const char *val) {
  static struct _gasneti_envtable_S {
    const char *pre;
    char *post;
    struct _gasneti_envtable_S *next;
  } *gasneti_envtable = NULL;
  static gasneti_mutex_t gasneti_envtable_lock = GASNETI_MUTEX_INITIALIZER;
  static int firsttime = 1;
  static int decodeenv = 1;
  if (firsttime) {
    decodeenv = !gasneti_getenv("GASNET_DISABLE_ENVDECODE");
    if (gasneti_init_done && gasneti_mynode != (gex_Rank_t)-1) {
      gasneti_envstr_display("GASNET_DISABLE_ENVDECODE",(decodeenv?"NO":"YES"),decodeenv);
      gasneti_sync_writes();
      firsttime = 0;
    }
  } else gasneti_sync_reads();
  if (!decodeenv) return val;

  if (strstr(val,"%0")) {
    struct _gasneti_envtable_S *p;
    gasneti_mutex_lock(&gasneti_envtable_lock);
      p = gasneti_envtable;
      while (p) {
        if (!strcmp(val, p->pre)) break;
        p = p->next;
      }
      if (p) val = p->post;
      else { /* decode it and save the result (can't trust setenv to safely set it back) */
        struct _gasneti_envtable_S *newentry = gasneti_malloc(sizeof(struct _gasneti_envtable_S));
        newentry->pre = gasneti_strdup(val);
        newentry->post = gasneti_malloc(strlen(val)+1);
        gasneti_decodestr(newentry->post, newentry->pre);
        if (!strcmp(newentry->post, newentry->pre)) { 
          gasneti_free(newentry); 
        } else {
          newentry->next = gasneti_envtable;
          gasneti_envtable = newentry;
          val = newentry->post;
        }
      }
    gasneti_mutex_unlock(&gasneti_envtable_lock);
  }
  return val;
}

/* gasneti_verboseenv_fn returns an expression that defines whether the given process should report to the console
   on env queries - needs to work before gasnet_init
   1 = yes, 0 = no, -1 = not yet / don't know
 */
#ifndef GASNETI_ENV_OUTPUT_NODE
#define GASNETI_ENV_OUTPUT_NODE()  (gasneti_mynode == 0)
#endif
extern int gasneti_verboseenv_parse(const char *);
extern int _gasneti_verboseenv_fn(void) {
  static int verboseenv = -1;
  if (verboseenv == -1) {
    if (gasneti_init_done && gasneti_mynode != (gex_Rank_t)-1) {
      if (!GASNETI_ENV_OUTPUT_NODE()) verboseenv = 0; // wrong process
      else {
      #if GASNET_DEBUG_VERBOSE
        verboseenv = 1; // hard-wired to enabled
      #else
        verboseenv = gasneti_verboseenv_parse(gasneti_getenv("GASNET_VERBOSEENV"));
      #endif
      }
      gasneti_sync_writes();
    }
  } else gasneti_sync_reads();
  return verboseenv;
}
extern int (*gasneti_verboseenv_fn)(void);
int (*gasneti_verboseenv_fn)(void) = &_gasneti_verboseenv_fn;

extern const char * gasneti_backtraceid(void) {
  static char myid[255];
  sprintf(myid, "[%i] ", (int)gasneti_mynode);
  return myid;
}

extern void gasneti_decode_args(int *argc, char ***argv) {
  static int firsttime = 1;
  if (!firsttime) return; /* ignore subsequent calls, to allow early decode */
  firsttime = 0;
  if (!gasneti_getenv_yesno_withdefault("GASNET_DISABLE_ARGDECODE",0)) {
    int argidx;
    char **origargv = *argv;
    for (argidx = 0; argidx < *argc; argidx++) {
      if (strstr((*argv)[argidx], "%0")) {
        char *tmp = gasneti_strdup((*argv)[argidx]);
        int newsz = gasneti_decodestr(tmp, tmp);
        if (newsz == strlen((*argv)[argidx])) gasneti_free(tmp); /* no change */
        else {
          int i, newcnt = 0;
          for (i = 0; i < newsz; i++) if (!tmp[i]) newcnt++; /* count growth due to inserted NULLs */
          if (newcnt == 0) { /* simple parameter replacement */
            (*argv)[argidx] = tmp;
          } else { /* need to grow argv */
            char **newargv = gasneti_malloc(sizeof(char *)*(*argc+1+newcnt));
            memcpy(newargv, *argv, sizeof(char *)*argidx);
            newargv[argidx] = tmp; /* base arg */
            memcpy(newargv+argidx+newcnt, (*argv)+argidx, sizeof(char *)*(*argc - argidx - 1));
            for (i = 0; i < newsz; i++) /* hook up new args */
              if (!tmp[i]) newargv[1+argidx++] = &(tmp[i+1]); 
            *argc += newcnt;
            if (*argv != origargv) gasneti_free(*argv);
            *argv = newargv;
            (*argv)[*argc] = NULL; /* ensure null-termination of arg list */
          }
        } 
      }
    }
  }
}

/* Propagate requested env vars from GASNet global env to the local env */

void (*gasneti_propagate_env_hook)(const char *, int) = NULL; // spawner- or conduit-specific hook

extern void gasneti_propagate_env_helper(const char *environ, const char * keyname, int flags) {
  const int is_prefix = flags & GASNETI_PROPAGATE_ENV_PREFIX;
  const char *p = environ;

  gasneti_assert(environ);
  gasneti_assert(keyname && !strchr(keyname,'='));

  int keylen = strlen(keyname);
  while (*p) {
    if (!strncmp(keyname, p, keylen) && (is_prefix || p[keylen] == '=')) {
      gasneti_assert(NULL != strchr(p+keylen, '='));
      char *var = gasneti_strdup(p);
      char *val = strchr(var, '=');
      *(val++) = '\0';
      val = (char *)gasneti_decode_envval(val);
      gasnett_setenv(var, val);
      GASNETI_TRACE_PRINTF(I,("gasneti_propagate_env(%s) => '%s'", var, val));
      gasneti_free(var);
      if (!is_prefix) break;
    }
    p += strlen(p) + 1;
  }
}

extern void gasneti_propagate_env(const char * keyname, int flags) {
  gasneti_assert(keyname);
  gasneti_assert(NULL == strchr(keyname, '='));

  // First look for matches in gasneti_globalEnv (if any)
  if (gasneti_globalEnv) {
    gasneti_propagate_env_helper(gasneti_globalEnv, keyname, flags);
  }

  // Next allow conduit-specific getenv (if any) to overwrite
  if (gasneti_propagate_env_hook) {
    gasneti_propagate_env_hook(keyname, flags);
  }
}


/* Process environment for exittimeout.
 * If (GASNET_EXITTIMEOUT is set), it is returned
 * else return = min(GASNET_EXITTIMEOUT_MAX,
 *                   GASNET_EXITTIMEOUT_MIN + gasneti_nodes * GASNET_EXITTIMEOUT_FACTOR)
 * Where all the GASNET_EXITTIMEOUT* tokens above are env vars.
 * The arguments are defaults for MAX, MIN and FACTOR, and the lowest value to allow.
 */
extern double gasneti_get_exittimeout(double dflt_max, double dflt_min, double dflt_factor, double lower_bound)
{
  double my_max = gasneti_getenv_dbl_withdefault("GASNET_EXITTIMEOUT_MAX", dflt_max);
  double my_min = gasneti_getenv_dbl_withdefault("GASNET_EXITTIMEOUT_MIN", dflt_min);
  double my_factor = gasneti_getenv_dbl_withdefault("GASNET_EXITTIMEOUT_FACTOR", dflt_factor);
  double result = gasneti_getenv_dbl_withdefault("GASNET_EXITTIMEOUT",
						 MIN(my_max, my_min + my_factor * gasneti_nodes));

  if (result < lower_bound) {
    gasneti_assert_dbl(MIN(dflt_max, dflt_min + dflt_factor * gasneti_nodes) ,>=, lower_bound);
    if (gasneti_getenv("GASNET_EXITTIMEOUT")) {
      gasneti_fatalerror("If used, environment variable GASNET_EXITTIMEOUT must be set to a value no less than %g", lower_bound);
    } else {
      gasneti_fatalerror("Environment variables GASNET_EXITTIMEOUT_{MAX,MIN,FACTOR} yield a timeout less than %g seconds", lower_bound);
    }
  }

  return result;
}

// Used in some conduits to coordinate user-provided exit code across layers
gasneti_atomic_t gasneti_exit_code = gasneti_atomic_init(0);

/* ------------------------------------------------------------------------------------ */
/* Bits for conduits which want/need to override pthread_create() */

#if defined(PTHREAD_MUTEX_INITIALIZER) /* only if pthread.h available */ && !GASNET_SEQ
  #ifndef GASNETC_PTHREAD_CREATE_OVERRIDE
    /* Default is just pass through */
    #define GASNETC_PTHREAD_CREATE_OVERRIDE(create_fn, thread, attr, start_routine, arg) \
      (*create_fn)(thread, attr, start_routine, arg)
  #endif

  int gasneti_pthread_create(gasneti_pthread_create_fn_t *create_fn, pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    // There is no portable way to printf a pointer-to-function. This way avoids warnings with -pedantic
    union { void *vp; gasneti_pthread_create_fn_t *cfp; void *(*sfp)(void *); } ucreate, ustart; 
    ucreate.cfp = create_fn;
    ustart.sfp = start_routine;
    GASNETI_TRACE_PRINTF(I, ("gasneti_pthread_create(%p, %p, %p, %p, %p)", ucreate.vp, (void *)thread, (void *)attr, ustart.vp, arg));
    return GASNETC_PTHREAD_CREATE_OVERRIDE(create_fn, thread, attr, start_routine, arg);
  }
#endif

/* ------------------------------------------------------------------------------------ */
#ifdef GASNETC_CHECK_PORTABLE_CONDUIT_HOOK
  // If a conduit is *conditionally* considered a "portable conduit", then this
  // hook can be implemented to allow the conduit to indicate if those
  // conditions are met.  This function should return non-zero when the conduit
  // is "portable" and zero when "native".
  // Runs via gasnete_check_config(), called by gasnete_init().
  extern int gasnetc_check_portable_conduit(void);
#else
  #define gasnetc_check_portable_conduit() 0
#endif

typedef struct { 
    const char *filename;
    mode_t filemode;
    const char *desc;
    int hwid;
} gasneti_device_probe_t;

#define GASNETI_IBV_DEVICES \
        { "/dev/infiniband/uverbs0",     S_IFCHR, "InfiniBand IBV", 2 },  /* OFED 1.0 */ \
        { "/dev/infiniband/ofs/uverbs0", S_IFCHR, "InfiniBand IBV", 2 }   /* Solaris */
#define GASNETI_CXI_DEVICES \
        { "/dev/cxi0",                   S_IFCHR, "HPE Slingshot-11 (OFI)", 3 }, \
        { "/sys/class/cxi",              S_IFDIR, "HPE Slingshot-11 (OFI)", 3 } 
#define GASNETI_GNI_DEVICES \
        { "/dev/kgni0",                  S_IFCHR, "Cray Aries/Gemini", 6 }, \
        { "/proc/kgnilnd",               S_IFDIR, "Cray Aries/Gemini", 6 }

// Boolean probe for device nodes (file or directory)
static int gasneti_device_probe(gasneti_device_probe_t *dev_to_probe) {
  struct stat stat_buf;
  return !stat(dev_to_probe->filename,&stat_buf) && 
         (!dev_to_probe->filemode || (dev_to_probe->filemode & stat_buf.st_mode));
}

// bug 3609: some verbs-compatible networks need special handling
// While that bug is about ibv-conduit, something simlar holds for OFI verbs provider
#define GASNETI_HCA_OMNI_PATH  1
#define GASNETI_HCA_TRUESCALE  2
static int gasneti_probeInfiniBandHCAs(void) {
  static int probeInfiniBandHCAs = 0;
#if PLATFORM_OS_LINUX
  static int is_init = 0;
  if (!is_init) {
    const char *filename[] = {
      "/sys/class/infiniband/hfi1_0/board_id", // Intel Omni-Path
      "/sys/class/infiniband/qib0/board_id",   // QLogic/Intel TrueScale
    };
    for (int i=0; i < sizeof(filename)/sizeof(filename[0]); i++) {
      FILE *fp = fopen(filename[i],"r");
      if (fp) {
        char buffer[128];
        size_t r = fread(&buffer, 1, sizeof(buffer), fp);
        if (r) { 
          buffer[r-1] = 0;
          // eg: "Intel Omni-Path HFI Adapter 100 Series, 1 Port, PCIe x16"
          if (strstr(buffer, "Omni-Path")) probeInfiniBandHCAs |= GASNETI_HCA_OMNI_PATH;
          // eg: "InfiniPath_QLE7340"
          if (strstr(buffer, "InfiniPath")) probeInfiniBandHCAs |= GASNETI_HCA_TRUESCALE;
        }
        fclose(fp);
      }
    }
    is_init = 1;
  }
#endif
  return probeInfiniBandHCAs;
}

// bug 3609: some verbs-compatible networks need special handling
static int gasneti_lowQualityVerbs(void) {
  int mask = (GASNETI_HCA_OMNI_PATH | GASNETI_HCA_TRUESCALE);
  return gasneti_probeInfiniBandHCAs() & mask;
}

// Search for hardware with a corresponding "native" OFI provider
//
// TODO: Mellanox drivers are available for at least FreeBSD and macOS, and
// libfabric support's both of those as well.  So, the Linux-specific probe
// for GASNETI_IBV_DEVICES should be expanded if we have interest in the
// verbs provider on those platforms.
static int gasneti_nativeOfiProvider(void) {
  static int nativeOfiProvider = 0;
#if PLATFORM_OS_LINUX
  static int is_init = 0;
  if (!is_init) {
    gasneti_device_probe_t dev_list[] = {
      GASNETI_IBV_DEVICES, // verbs or psm2 providers
      GASNETI_CXI_DEVICES  // cxi provider
      #if !GASNET_SEGMENT_EVERYTHING
        , GASNETI_GNI_DEVICES // gni provider
      #endif
    };
    if (gasneti_probeInfiniBandHCAs() & GASNETI_HCA_TRUESCALE) {
      // Assume no good if TrueScale HCA is found (we assume single fabric)
    } else {
      for (int i = 0; i < sizeof(dev_list)/sizeof(dev_list[0]); ++i) {
        if (gasneti_device_probe(dev_list + i)) {
          nativeOfiProvider = 1;
          break;
        }
      }
    }
    is_init = 1;
  }
#endif
  return nativeOfiProvider;
}

// Search for hardware where we will recommend ucx-conduit as native.
// Currently we only document support for Mellanox ConnectX-5 and newer.
// However, we currently accept anything not on ibv-consuit's ban list.
// TODO: more accurate device probe?
// TODO: more platforms than just Linux?
static int gasneti_nativeUcxSupport(void) {
  static int nativeUcxSupport = 0;
#if PLATFORM_OS_LINUX
  static int is_init = 0;
  if (!is_init) {
    gasneti_device_probe_t dev_list[] = { GASNETI_IBV_DEVICES };
    if (gasneti_lowQualityVerbs()) {
      // Assume no good if any ban-listed HCA is found (we assume single fabric)
    } else {
      for (int i = 0; i < sizeof(dev_list)/sizeof(dev_list[0]); ++i) {
        if (gasneti_device_probe(dev_list + i)) {
          nativeUcxSupport = 1;
          break;
        }
      }
    }
    is_init = 1;
  }
#endif
  return nativeUcxSupport;
}

static void gasneti_check_portable_conduit(void) { /* check for portable conduit abuse */
  char mycore[80], myext[80];
  char const *mn = GASNET_CORE_NAME_STR;
  char *m;
  m = mycore; while (*mn) { *m = tolower(*mn); m++; mn++; }
  *m = '\0';
  mn = GASNET_EXTENDED_NAME_STR;
  m = myext; while (*mn) { *m = tolower(*mn); m++; mn++; }
  *m = '\0';
  
  if ( /* is a portable network conduit */
      gasnetc_check_portable_conduit()
      || (!strcmp("mpi",mycore) && !strcmp("reference",myext))
      || (!strcmp("udp",mycore) && !strcmp("reference",myext))
      || (!strcmp("ucx",mycore) && !gasneti_nativeUcxSupport())
      ) {
    const char *p = GASNETI_CONDUITS;
    char natives[255];
    char reason[255];
    natives[0] = 0;
    reason[0] = 0;
    while (*p) { /* look for configure-detected native conduits */
      #define GASNETI_CONDUITS_DELIM " ,/;\t\n"
      char name[80];
      p += strspn(p,GASNETI_CONDUITS_DELIM);
      if (*p) {
        int len = strcspn(p,GASNETI_CONDUITS_DELIM);
        strncpy(name, p, len);
        name[len] = 0;
        p += len;
        p += strspn(p,GASNETI_CONDUITS_DELIM);
        /* Ignore the portable conduits */
        if (!strcmp(name,"smp")) continue;
        if (!strcmp(name,"mpi")) continue;
        if (!strcmp(name,"udp")) continue;
        if (!strcmp(name,"ucx") && !gasneti_nativeUcxSupport()) continue;
        if (!strcmp(name,"ofi") && !gasneti_nativeOfiProvider()) continue;
        if (!strcmp(name,"ibv") && gasneti_lowQualityVerbs()) continue; // never recommend ibv on these networks
        if (strlen(natives)) strcat(natives,", ");
        strcat(natives,name);
      }
      #undef GASNETI_CONDUITS_DELIM
    }
    if (natives[0]) {
      sprintf(reason, "    WARNING: Support was detected for native GASNet conduits: %s",natives);
    } else { /* look for hardware devices supported by native conduits */
      gasneti_device_probe_t known_devs[] = {
        GASNETI_IBV_DEVICES,
        GASNETI_CXI_DEVICES,
        #if !GASNET_SEGMENT_EVERYTHING
          GASNETI_GNI_DEVICES,
        #endif
        { "/list_terminator", S_IFDIR, "", 9999 }
      };
      int lim = sizeof(known_devs)/sizeof(known_devs[0]);
      for (int i = 0; i < lim; i++) {
        if (gasneti_device_probe(known_devs + i)) {
            int hwid = known_devs[i].hwid;
            if (hwid == 2 && gasneti_lowQualityVerbs()) continue; // never recommend ibv on these networks
            if (strlen(natives)) strcat(natives,", ");
            strcat(natives,known_devs[i].desc);
            while (i < lim && hwid == known_devs[i].hwid) i++; /* don't report a network twice */
        }
      }
      if (natives[0]) {
        sprintf(reason, "    WARNING: This system appears to contain recognized network hardware: %s\n"
                        "    WARNING: which is supported by a GASNet native conduit, although\n"
                        "    WARNING: it was not detected at configure time (missing drivers?)",
                        natives);
      }
    }
    if (reason[0] && !gasneti_getenv_yesno_withdefault("GASNET_QUIET",0)) {
      gasneti_console0_message("WARNING",
                     "Using GASNet's %s-conduit, which exists for portability convenience.\n"
                     "%s\n"
                     "    WARNING: You should *really* use the high-performance native GASNet conduit\n"
                     "    WARNING: if communication performance is at all important in this program run.",
              mycore, reason);
    }
  }
}

static void gasneti_check_architecture(void) { // check for bad build configurations
  #if PLATFORM_OS_SUBFAMILY_CNL && PLATFORM_ARCH_X86_64 // bug 3743, verify correct processor tuning
  { FILE *fp = fopen("/proc/cpuinfo","r");
    char model[255];
    if (!fp) gasneti_fatalerror("Failure in fopen('/proc/cpuinfo','r')=%s",strerror(errno));
    while (!feof(fp) && fgets(model, sizeof(model), fp)) {
      if (strstr(model,"model name")) break;
    }
    fclose(fp);
    GASNETI_TRACE_PRINTF(I,("CPU %s",model));
    int isKNL = !!strstr(model, "Phi");
    #ifdef __CRAY_MIC_KNL  // module craype-mic-knl that tunes for AVX512
      const char *warning = isKNL ? 0 :
      "This executable was optimized for MIC KNL (module craype-mic-knl) but run on another processor!";
    #else // some other x86 tuning mode
      const char *warning = isKNL ? 
      "This executable is running on a MIC KNL architecture, but was not optimized for MIC KNL.\n"
      "    WARNING: This often has a MAJOR impact on performance. Please re-build with module craype-mic-knl!"
      : 0;
    #endif
    if (warning) gasneti_console0_message("WARNING", "%s", warning);
  }
  #endif
}

/* ------------------------------------------------------------------------------------ */
/* Trivial handling of defered-start progress threads
 */

int gasneti_query_progress_threads(
            gex_Client_t                     e_client,
            unsigned int                    *count_p,
            const gex_ProgressThreadInfo_t **info_p,
            gex_Flags_t                      flags)
{
  // TODO: this enforcement will become incorrect for the multi-client case
  static int have_run = 0;
  if (have_run) {
    gasneti_fatalerror("A client may make at most one call to gex_System_QueryProgressFunctions().");
  } else {
    have_run = 1;
  }

  if (! e_client) GASNETI_RETURN_ERRR(BAD_ARG, "client must be non-NULL");
  gasneti_Client_t i_client = gasneti_import_client(e_client);
  if (! count_p)  GASNETI_RETURN_ERRR(BAD_ARG, "count_p must be non-NULL");
  if (! info_p)   GASNETI_RETURN_ERRR(BAD_ARG, "info_p must be non-NULL");
  if (flags)      GASNETI_RETURN_ERRR(BAD_ARG, "flags argument must be zero");
  if (!(gex_Client_QueryFlags(e_client) & GEX_FLAG_DEFER_THREADS))
                  GASNETI_RETURN_ERRR(RESOURCE, "GEX_FLAG_DEFER_THREADS was not passed to gex_Client_Init");

  *count_p = 0;
  *info_p = NULL;

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/* Nodemap handling
 */

gex_Rank_t *gasneti_nodemap = NULL;
gasneti_nodegrp_t gasneti_myhost = {NULL,0,(gex_Rank_t)(-1),0,(gex_Rank_t)(-1)};
gasneti_nodegrp_t gasneti_mysupernode = {NULL,0,(gex_Rank_t)(-1),0,(gex_Rank_t)(-1)};
gasnet_nodeinfo_t *gasneti_nodeinfo = NULL;

/* This code is "good" for all "sensible" process layouts, where "good"
 * means identifing all sharing for such a mapping in one pass and the
 * term "sensible" includes:
 *   "Block" layouts like       |0.1.2.3|4.5.6.7|8.9._._|
 *                           or |0.1.2.3|4.5.6._|7.8.9._|
 *   "Block-cyclic" like        |0.1.6.7|2.3.8.9|4.5._._|
 *   "Cyclic/Round-robin" like  |0.3.6.9|1.4.7._|2.5.8._|
 *   and all 24 permutations of the XYZT dimensions on the BG/P.
 *
 * This is also "safe" for an arbitrary mapping, but may fail to
 * identify some or all of the potential sharing in such a case.
 */
static void gasneti_nodemap_helper_linear(const char *ids, size_t sz, size_t stride) {
  gex_Rank_t i, prev, base;
  const char *p, *base_p, *prev_p;

  prev   = base   = gasneti_nodemap[0] = 0;
  prev_p = base_p = ids;
  p = base_p + stride;

  for (i = 1; i < gasneti_nodes; ++i, p += stride) {
    if (!memcmp(p, prev_p, sz)) {                  /* Repeat the previous id */
      gasneti_nodemap[i] = gasneti_nodemap[prev];
      prev += 1;       prev_p += stride;
      continue;
    }

    gasneti_nodemap[i] = i;
    if (!memcmp(p, ids, sz)) {                     /* Restart the first "row" */
      prev = 0;        prev_p = ids;
    } else if (!memcmp(p, base_p, sz)) {           /* Restart the previous "row" */
      prev = base;     prev_p = base_p;
    } else if (!memcmp(p, prev_p + stride, sz)) {  /* Continue current "row" if any */
      prev += 1;       prev_p += stride;
    } else {                                       /* Begin a new "row" */
      prev = base = i; prev_p = base_p = p;
    }
    gasneti_nodemap[i] = gasneti_nodemap[prev];
  }
}

/* This code is "good" for all possible process layouts, where "good"
 * means identifing all sharing.  However, the running time is O(n*log(n)).
 */
static struct {
  const char *ids;
  size_t sz;
  size_t stride;
} _gasneti_nodemap_sort_aux;
static int _gasneti_nodemap_sort_fn(const void *a, const void *b) {
  gex_Rank_t key1 = *(const gex_Rank_t *)a;
  gex_Rank_t key2 = *(const gex_Rank_t *)b;
  const char *val1 = _gasneti_nodemap_sort_aux.ids + key1 * _gasneti_nodemap_sort_aux.stride;
  const char *val2 = _gasneti_nodemap_sort_aux.ids + key2 * _gasneti_nodemap_sort_aux.stride;
  int retval = memcmp(val1, val2, _gasneti_nodemap_sort_aux.sz);
  if (!retval) { /* keep sort stable */
    gasneti_assert_uint(key1 ,!=, key2);
    retval = (key1 < key2) ? -1 : 1;
  }
  return retval;
}
static void gasneti_nodemap_helper_qsort(const char *ids, size_t sz, size_t stride) {
  gex_Rank_t *work    = gasneti_malloc(gasneti_nodes * sizeof(gex_Rank_t));
  const char *prev_id;
  int i, prev; /* If these are gex_Rank_t then bug 2634 can crash XLC */

  _gasneti_nodemap_sort_aux.ids    = ids;
  _gasneti_nodemap_sort_aux.sz     = sz;
  _gasneti_nodemap_sort_aux.stride = stride;
  for (i = 0; i < gasneti_nodes; ++i) work[i] = i;
  qsort(work, gasneti_nodes, sizeof(gex_Rank_t), &_gasneti_nodemap_sort_fn);

  prev = work[0];
  gasneti_nodemap[prev] = prev;
  prev_id = ids + prev*stride;
  for (i = 1; i < gasneti_nodes; ++i) {
    int node = work[i]; /* Also subject to bug 2634 */
    const char *tmp_id = ids + node*stride;
    prev = gasneti_nodemap[node] = memcmp(tmp_id, prev_id, sz) ? node : prev;
    prev_id = tmp_id;
  }
  gasneti_free(work);
}

/* gasneti_nodemap_helper
 * Construct a nodemap from a vector of "IDs"
 */
GASNETI_NEVER_INLINE(gasneti_nodemap_helper,
static void gasneti_nodemap_helper(const void *ids, size_t sz, size_t stride)) {
  #ifndef GASNETC_DEFAULT_NODEMAP_EXACT
    // Default to slow-but-steady (it wins the race)
    // However, see Bug 3770 - RFE: restore default linear-time nodemap behavior
    #define GASNETC_DEFAULT_NODEMAP_EXACT 1
  #endif
  gasneti_assert(ids);
  gasneti_assert_uint(sz ,>, 0);
  gasneti_assert_uint(stride ,>=, sz);

  if (gasneti_getenv_yesno_withdefault("GASNET_NODEMAP_EXACT",GASNETC_DEFAULT_NODEMAP_EXACT)) {
    /* "exact" but potentially costly */
    gasneti_nodemap_helper_qsort(ids, sz, stride);
  } else {
    /* cheap and correct for all "normal" cases */
    gasneti_nodemap_helper_linear(ids, sz, stride);
  }
}

// 64-bit FNV-1a, implemented from scratch based on psuedo code and constants in
// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
static uint64_t fnv1a_64(const uint8_t *buf, size_t len)
{
  const uint64_t FNV_offset_basis = 0xcbf29ce484222325ULL;
  const uint64_t FNV_prime = 0x00000100000001B3ULL;
  uint64_t x = FNV_offset_basis;
  for (size_t i = 0; i < len; i++) {
    x ^= *(buf++);
    x *= FNV_prime;
  }
  return x;
}

// gasneti_hosthash(): 64-bit hash of hostname
//
// NOTE: gasneti_checksum() is not suitable
// e.g. "4001.0004" and "1001.0001" hash the same, and when
// we fold down to 32-bits the problem would get even worse.
// At 32-bits the cancellation is at period 4, so that names
// "c03-00", "c13-01" and "c23-02" share the same hash, as
// would the pair "172.16.0.6" and "172.18.0.8".
extern uint64_t gasneti_hosthash(void) {
  const char *myname = gasneti_gethostname();
  return fnv1a_64((const uint8_t *)myname, strlen(myname));
}

/* Wrapper around gethostid() */
extern uint32_t gasneti_gethostid(void) {
    static uint32_t myid = 0;

    if_pf (!myid) {
    #if PLATFORM_OS_CYGWIN || !HAVE_GETHOSTID
      // gethostid() is known to be either unreliable or unavailable
      // always hash the hostname
    #else
      // gethostid() is available
      // hash the hostname only if gethostid() looks unreliable after multiple retries.
      // This allows us to tolerate transient misbehavior such as was reported in
      // Bug 4483 - gethostid() on Perlmutter rarely returns 0, breaking PSHM detection
      #ifndef GASNETI_GETHOSTID_RETRIES
        #define GASNETI_GETHOSTID_RETRIES 24 // try to keep worst case delay under 0.1s
      #endif
      int retries = GASNETI_GETHOSTID_RETRIES;
      uint64_t delay_ns = 1;
    retry:
      myid = (uint32_t)gethostid();
    #endif

      /* Fall back to hashing the hostname if the hostid is obviously invalid */
      if (!myid || !(~myid)        /* 0.0.0.0 or 255.255.255.255 */
          || (myid == 0x7f000001)  /* All 12 distinct permutations of 127.0.0.1: */
          || (myid == 0x7f000100)
          || (myid == 0x7f010000)
          || (myid == 0x007f0001)
          || (myid == 0x007f0100)
          || (myid == 0x017f0000)
          || (myid == 0x00007f01)
          || (myid == 0x00017f00)
          || (myid == 0x01007f00)
          || (myid == 0x0000017f)
          || (myid == 0x0001007f)
          || (myid == 0x0100007f)) {
      #if HAVE_GETHOSTID && !PLATFORM_OS_CYGWIN
        if (retries-- > 0) {
          GASNETI_TRACE_PRINTF(I,("Retrying after invalid return 0x%08x from gethostid()", (unsigned int)myid));
          gasneti_nsleep(delay_ns);
          delay_ns *= 2;
          goto retry;
        }
        gasneti_console_message("WARNING", "Invalid return 0x%08x from gethostid().  "
                                "Please see documentation on GASNET_HOST_DETECT in README and "
                                "consider setting its value to 'hostname' or reconfiguring using "
                                "'--with-host-detect=hostname' to make that the default value.",
                                (unsigned int)myid);
      #endif
        uint64_t csum = gasneti_hosthash();
        myid = GASNETI_HIWORD(csum) ^ GASNETI_LOWORD(csum);
      }
    }

    return myid;
}

static enum {
    gasneti_hostid_alg_invalid = 0,
    gasneti_hostid_alg_conduit,
    gasneti_hostid_alg_gethostid,
    gasneti_hostid_alg_hostname,
    gasneti_hostid_alg_trivial
} gasneti_hostid_alg;

static  union {
    uint64_t hostname;
    uint32_t hostid;
} gasneti_my_hostid;

// gasneti_format_host_detect()
//
// Returns the hostid, or equivalent, as a printable string (possibly empty).
// This is for use in error, warning and trace messages (uses gasneti_dynsprintf)
const char *gasneti_format_host_detect(void) {
  switch (gasneti_hostid_alg) {
    case gasneti_hostid_alg_conduit:
      return "[opaque conduit-specific value]";
      break;

    case gasneti_hostid_alg_gethostid:
      return gasneti_dynsprintf("0x%08x", gasneti_my_hostid.hostid);
      break;

    case gasneti_hostid_alg_hostname:
      return gasneti_dynsprintf("'%s' (hashed to 0x%"PRIx64")",
                                gasneti_gethostname(), gasneti_my_hostid.hostname);
      break;

    case gasneti_hostid_alg_trivial:
      return gasneti_dynsprintf("%d", gasneti_mynode);
      break;

    default: gasneti_unreachable_error(("Unknown host detect algorithm %i", (int)gasneti_hostid_alg));
  }
}


/* gasneti_nodemapParse()
 *
 * Performs "common" tasks after gasneti_nodemap[] has been constucted.
 * A conduit which builds a gasneti_nodemap[] w/o calling gasneti_nodemapInit()
 * should still call this function to perform the "common" work.
 *
 * Constructs:
 *   gasneti_nodeinfo[] = array of length gasneti_nodes of supernode ids and mmap offsets
 * and fills in the fields in gasneti_mysupernode:
 *   gasneti_mysupernode.nodes       array of nodes in my supernode
 *   gasneti_mysupernode.node_count  count of nodes in my supernode (length of 'nodes' array)
 *   gasneti_mysupernode.node_rank   my ranks in 'nodes' array
 *   gasneti_mysupernode.grp_count   number of supernodes in the job
 *   gasneti_mysupernode.grp_rank    my supernode's rank within all supernodes
 *   gasneti_myhost.*                same quantities for 'host' rather than for 'supernode'
 * Those first five quantities are also available via the following respective aliases:
 *   gasneti_nodemap_local[]
 *   gasneti_nodemap_local_count
 *   gasneti_nodemap_local_rank
 *   gasneti_nodemap_global_count
 *   gasneti_nodemap_global_rank
 *
 * NOTE: may modify gasneti_nodemap[] if env var GASNET_SUPERNODE_MAXSIZE is set,
 *        or if gasneti_nodemap_local_count would exceed GASNETI_PSHM_MAX_NODES.
 * TODO: splitting by socket or other criteria for/with GASNET_SUPERNODE_MAXSIZE.
 * TODO: keep widths around for conduits to use? (at least ibv uses)
 */
extern void gasneti_nodemapParse(void) {
  gex_Rank_t i,j,limit;
  gex_Rank_t initial,final;

  struct { /* TODO: alloca? */
    gex_Rank_t width;
    gex_Rank_t h_lead;
    gex_Rank_t sn_lead;
    gex_Rank_t host;
    gex_Rank_t supernode;
  } *s = gasneti_calloc(gasneti_nodes, sizeof(*s));

  gasneti_assert(gasneti_nodemap);
  gasneti_assert(gasneti_nodemap[0] == 0);
  gasneti_assert_uint(gasneti_nodemap[gasneti_mynode] ,<=, gasneti_mynode);

  /* Check for user-imposed limit: 0 (or negative) means no limit */
#if GASNET_PSHM
  limit = gasneti_getenv_int_withdefault("GASNET_SUPERNODE_MAXSIZE", 0, 0);
 #if GASNET_CONDUIT_SMP
  if (limit) {
    gasneti_console0_message("WARNING","ignoring GASNET_SUPERNODE_MAXSIZE for smp-conduit with PSHM.");
  }
  limit = gasneti_nodes;
 #else
  if (limit <= 0) {
     limit = GASNETI_PSHM_MAX_NODES;
  } else if (limit > GASNETI_PSHM_MAX_NODES) {
     gasneti_fatalerror("GASNET_SUPERNODE_MAXSIZE %d exceeds GASNETI_PSHM_MAX_NODES (%d)", limit, GASNETI_PSHM_MAX_NODES);
  }
 #endif
#else
  limit = 1; /* No PSHM */
#endif

  gasneti_assert(!gasneti_nodeinfo);
  gasneti_nodeinfo = gasneti_calloc(gasneti_nodes, sizeof(gasnet_nodeinfo_t));
  gasneti_leak(gasneti_nodeinfo);

  /* First pass: 
   * + Apply the supernode size limit, if any.
   * + Determine the counts and ranks
   * + Construct gasneti_nodeinfo[].{host,supernode}
   */
  initial = gasneti_nodemap[gasneti_mynode];
  for (i = 0; i < gasneti_nodes; ++i) {
    const gex_Rank_t n = gasneti_nodemap[i];
    const gex_Rank_t width = s[n].width++;
    const gex_Rank_t lrank = width % limit;
    if (!width) { /* First node on host */
      s[n].host = gasneti_myhost.grp_count++;
    }
    if (!lrank) { /* First node on supernode */
      s[n].sn_lead = i;
      s[n].supernode = gasneti_mysupernode.grp_count++;
    }
    if (i == gasneti_mynode) {
      gasneti_mysupernode.node_rank = lrank;
      gasneti_myhost.node_rank = width;
    }
    s[i].h_lead = n;
    gasneti_nodemap[i] = s[n].sn_lead;
    gasneti_nodeinfo[i].supernode = s[n].supernode;
    gasneti_nodeinfo[i].host = s[n].host;
  }
  final = gasneti_nodemap[gasneti_mynode];
  gasneti_mysupernode.node_count = (final == s[initial].sn_lead) ? (((s[initial].width - 1) % limit) + 1) : limit;
  gasneti_myhost.node_count = s[initial].width;
  gasneti_mysupernode.grp_rank = gasneti_nodeinfo[gasneti_mynode].supernode;
  gasneti_myhost.grp_rank = gasneti_nodeinfo[gasneti_mynode].host;

  /* Second pass: Construct arrays of local nodes */
  gasneti_assert_uint(gasneti_myhost.node_count ,>=, gasneti_mysupernode.node_count);
  gasneti_myhost.nodes = gasneti_malloc(gasneti_myhost.node_count*sizeof(gex_Rank_t));
  gasneti_leak(gasneti_myhost.nodes);
  for (i = initial, j = 0; j < gasneti_myhost.node_count; ++i) {
    gasneti_assert_uint(i ,<, gasneti_nodes);
    if (s[i].h_lead == initial) {
      if (i == final) gasneti_mysupernode.nodes = gasneti_myhost.nodes + j;
      gasneti_myhost.nodes[j++] = i;
    }
  }
  gasneti_assert(gasneti_mysupernode.nodes != NULL);

  gasneti_free(s);

#if GASNET_PSHM
  GASNETI_TRACE_PRINTF(I,("nodemap: process %d of %d in its nbrhd, %d of %d on host %s",
                          gasneti_mysupernode.node_rank, gasneti_mysupernode.node_count,
                          gasneti_myhost.node_rank, gasneti_myhost.node_count,
                          gasneti_gethostname()));
  gex_Rank_t nonh_rank, nonh_count;  // "Nbrhd ON Host" coordinates
  nonh_rank = nonh_count = 0;
  for (i = 0; i < gasneti_myhost.node_count; ++i) {
    j = gasneti_myhost.nodes[i];  // iterating over procs on my host
    if (gasneti_nodemap[j] == j) { // first proc in its nbrhd
      nonh_count += 1;
      nonh_rank += (j <= gasneti_mynode); // too high by 1, corrected after loop
    }
  }
  --nonh_rank;
  GASNETI_TRACE_PRINTF(I,("nodemap: nbrhd %d of %d in the job, %d of %d on host %s",
                          gasneti_mysupernode.grp_rank, gasneti_mysupernode.grp_count,
                          nonh_rank, nonh_count, gasneti_gethostname()));
#else
  GASNETI_TRACE_PRINTF(I,("nodemap: process %d of %d on host %s",
                          gasneti_myhost.node_rank, gasneti_myhost.node_count,
                          gasneti_gethostname()));
#endif
  GASNETI_TRACE_PRINTF(I,("nodemap: host %d of %d",
                          gasneti_myhost.grp_rank, gasneti_myhost.grp_count));

  #if GASNET_DEBUG_VERBOSE
    for (i = 0; i < gasneti_nodes; ++i) {
      gasneti_console0_message("INFO","gasneti_nodemap[%i] = %i\n", (int)i, (int)gasneti_nodemap[i]);
    }
  #endif
  
#if GASNET_NDEBUG && !GASNET_PSHM && !GASNET_SEGMENT_EVERYTHING && !GASNET_NO_PSHM_WARNING
  if (!gasneti_mynode && (gasneti_nodes != gasneti_myhost.grp_count)) {
    // at least one host holds more than one process
    gasneti_console_message("WARNING",
        "Running with multiple processes per host without shared-memory communication support (PSHM).  "
        "This can significantly reduce performance.  "
        "Please re-configure GASNet using `--enable-pshm` to enable fast intra-host comms.");
  }
#endif
}

// gasneti_nodemapInit(exchangefn, ids, sz, stride)
//
// Collectively called to construct the gasneti_nodemap[] such that
//   For all i: gasneti_nodemap[i] is the lowest node number collocated w/ node i
// GASNet nodes are considered collocated if they have the same node "ID" (see below).
//
// Calls gasneti_nodemapParse() after construction of the nodemap.
//
// A given conduit may fall into one of four cases based on the first two arguments:
//   if (exchangefn && ids) {
//     GASNET_HOST_DETECT is honored, and "conduit" is a valid option which will
//     use `sz` bytes at `*ids` as a local identifier.
//     The `stride` argument is ignored.
//   } else if (exchangefn && !ids)
//     GASNET_HOST_DETECT is honored, but "conduit" is NOT a valid option.
//     Both `sz` and `stride` are ignored.
//   } else if (ids) {
//     GASNET_HOST_DETECT defaults to "conduit", which is the only valid option
//     (other than undocumented "trivial").
//     Memory starting at `*ids` is taken as a vector of `gasneti_nodes` local
//     ids, each of length `sz` bytes, spaced by `stride` bytes.  The vector of
//     IDs need not be "single valued" across calling nodes, so long as the
//     resulting nodemap is the same.  This allows, for instance, the use of
//     "local/relative" IDs as well as "global/absolute" ones.
//     This should only be used in cases where the conduit-specific identifier
//     is 100% reliable and/or gasneti_gethostname() and gasnet_gethostid() are
//     UNreliable (or unavailable).  See also: First TODO note below.
//   } else {
//     Lacking both an `exchangefn` and `ids`, this case will always fail unless
//     (undocumented) GASNET_HOST_DETECT="trivial" is requested.
//     This may be useful early in conduit development, but is otherwise
//     very strongly discouraged.
//     Both `sz` and `stride` are ignored.
//   }
//
// TODO: The "four cases" above conflate the scalar/vector nature of `ids`
// with the availability of an `exchangefn`, with the result that a vector of
// conduit-specific ids cannot be ignored in favor of one of the standard
// GASNET_HOST_DETECT options (which had made "conduit" and "trivial" the only
// supported options for aries-conduit).  This could be fixed with an
// adjustment such as making non-zero `stride` the indicator for a vector `ids`.
//
// TODO: There is a proposed "greedy" algorithm which uses all of the available
// IDs (hostid, hostname, conduit, ...).
//
extern void gasneti_nodemapInit(gasneti_bootstrapExchangefn_t exchangefn,
                                const void *ids, size_t sz, size_t stride) {
  gasneti_nodemap = gasneti_malloc(gasneti_nodes * sizeof(gex_Rank_t));

  // First parse GASNET_HOST_DETECT
  // Default is complicated:
  // Will use "conduit" if valid (ids is non-NULL)
  // else use the default selected at configure-time (if any),
  // else use "gethostid" if available/trusted on this platform,
  // else use "hostname".
  #ifdef GASNETI_HOST_DETECT_CONFIGURE
    #define GASNETI_HOST_DETECT_DEFAULT GASNETI_HOST_DETECT_CONFIGURE
  #elif HAVE_GETHOSTID && !PLATFORM_OS_CYGWIN
    // gethostid() is available and not known-broken
    #define GASNETI_HOST_DETECT_DEFAULT "gethostid"
  #else
    #define GASNETI_HOST_DETECT_DEFAULT "hostname"
  #endif
  const char *dflt = ids ? "conduit" : GASNETI_HOST_DETECT_DEFAULT;
  const char *envval = gasneti_getenv_withdefault("GASNET_HOST_DETECT", dflt);
  if (! gasneti_strcasecmp(envval, "conduit")) {
    gasneti_hostid_alg = gasneti_hostid_alg_conduit;
  } else if (! gasneti_strcasecmp(envval, "gethostid")) {
    gasneti_hostid_alg = gasneti_hostid_alg_gethostid;
  } else if (! gasneti_strcasecmp(envval, "hostname")) {
    gasneti_hostid_alg = gasneti_hostid_alg_hostname;
  } else if (! gasneti_strcasecmp(envval, "trivial")) {
    // NOTE: this option is intentionally undocumented
    gasneti_hostid_alg = gasneti_hostid_alg_trivial;
  } else {
    gasneti_fatalerror("GASNET_HOST_DETECT='%s' is not recognized", envval);
  }

  void *tmp = NULL;

  switch (gasneti_hostid_alg) {
    case gasneti_hostid_alg_conduit:
      // TODO: save something suitable for gasneti_format_host_detect()
      // If we lack conduit-specific ID(s), then "conduit" is invalid:
      if (!ids) goto out_bad_alg;
      // (ids && !exchangefn) means a *full* vector of conduit-specific IDs:
      if (!exchangefn) goto no_exchange;
      // otherwise we'll exchange the conduit-specific IDs, below
      break;

    case gasneti_hostid_alg_gethostid:
      gasneti_my_hostid.hostid = gasneti_gethostid();
      sz = sizeof(gasneti_my_hostid.hostid);
      ids = &gasneti_my_hostid;
      break;

    case gasneti_hostid_alg_hostname:
      gasneti_my_hostid.hostname = gasneti_hosthash();
      sz = sizeof(gasneti_my_hostid.hostname);
      ids = &gasneti_my_hostid;
      break;

    case gasneti_hostid_alg_trivial:
      for (gex_Rank_t i = 0; i < gasneti_nodes; ++i) gasneti_nodemap[i] = i;
      goto no_helper;
      break;

    default: gasneti_unreachable_error(("Unknown host detect algorithm %i", (int)gasneti_hostid_alg));
  }

  // Bail if conduit did not provide an exchangefn
  if (!exchangefn) goto out_bad_alg;

  // Exchange the chosen local id
  gasneti_assert(sz);
  tmp = gasneti_malloc(gasneti_nodes * sz);
  (*exchangefn)((void*)ids, sz, tmp);
  ids = tmp;
  stride = sz;
no_exchange:
  gasneti_nodemap_helper(ids, sz, stride);
  gasneti_free(tmp);

no_helper:
  // Perform "common" work w.r.t the nodemap
  GASNETI_TRACE_PRINTF(I,("GASNET_HOST_DETECT=%s yields %s", envval, gasneti_format_host_detect()));
  gasneti_nodemapParse();
  return;

out_bad_alg:
  gasneti_fatalerror("This conduit (" GASNET_CORE_NAME_STR ") does not support GASNET_HOST_DETECT='%s'.", envval);
}

/* Presently just frees the space allocated for the full nodemap.
 */
extern void gasneti_nodemapFini(void) {
  gasneti_free(gasneti_nodemap);
#if GASNET_DEBUG
  /* To help catch any use-afer-Fini: */
  gasneti_nodemap = NULL;
#endif
}

/* ------------------------------------------------------------------------------------ */
/* Get a line up to '\n' or EOF using dynamicly grown buffer
 *  If buffer is too small (or NULL) then it is gasneti_realloc()ed.
 *  Buffer and length written to *buf_p and *n_p, even on error.
 *  Subsequent calls may reuse the buffer and length.
 *  Caller is responsible for eventual gasneti_free().
 *  Buffer is always terminated by '\0', even on error.
 *  If a '\n' was read, it is perserved.
 *  A '\n' is NOT added if EOF was reached first.
 *  Returns bytes read on success; -1 on EOF or other error.
 */
#ifdef gasneti_getline
/* Using glibc version */
#else
ssize_t gasneti_getline(char **buf_p, size_t *n_p, FILE *fp) {
    char   *buf = *buf_p;
    char   *p   = buf;
    size_t  n   = buf ? *n_p : 0;
    ssize_t len = 0;

    gasneti_assert_int((ssize_t)n ,>=, 0);

    do {
        size_t space = n - len;
        if_pf (space < 2) {
            n += MAX(2, n);
            buf = gasneti_realloc(buf, n);
            p = buf + len;
            space = n - len;
        }
        if (fgets(p, space, fp) == NULL) {
            *p = '\0';
            len = -1;
            break; /* error before full line read */
        }
        len += strlen(p);
        p = buf + len;
    } while (!feof(fp) && (p[-1] != '\n'));

    *buf_p = buf;
    *n_p = n;
    return len;
}
#endif /* gasneti_getline */

/* ------------------------------------------------------------------------------------ */
// Internal conduit interface to spawner

#if HAVE_SSH_SPAWNER
  extern gasneti_spawnerfn_t const *gasneti_bootstrapInit_ssh(int *argc, char ***argv, gex_Rank_t *nodes, gex_Rank_t *mynode);
#endif
#if HAVE_MPI_SPAWNER
  extern gasneti_spawnerfn_t const *gasneti_bootstrapInit_mpi(int *argc, char ***argv, gex_Rank_t *nodes, gex_Rank_t *mynode);
#endif
#if HAVE_PMI_SPAWNER
  extern gasneti_spawnerfn_t const *gasneti_bootstrapInit_pmi(int *argc, char ***argv, gex_Rank_t *nodes, gex_Rank_t *mynode);
#endif

int gasneti_spawn_verbose = 0;

extern gasneti_spawnerfn_t const *gasneti_spawnerInit(int *argc_p, char ***argv_p,
                                  const char *force_spawner,
                                  gex_Rank_t *nodes_p, gex_Rank_t *mynode_p) {
  gasneti_spawnerfn_t const *res = NULL;
  const char *spawner_envvar = "GASNET_" GASNET_CORE_NAME_STR "_SPAWNER";
  const char *spawner = NULL;
  int enabled = 0;  // non-zero if an enabled spawner is selected explicitly
  int disabled = 0; // non-zero if a known spawner is selected explicitly but is not enabled
  int spawner_not_set = 0;
  int match;
  if (force_spawner) spawner = force_spawner;
  else { 
    // Purposely hide this variable from verbose output, since it's only for
    // use as an internal hand-off from out-dated gasnetrun scripts.
    // End users should set GASNET_<conduit>_SPAWNER.
    const char *dflt = gasneti_getenv("GASNET_SPAWN_CONTROL");
    if (!dflt) {
    #ifdef GASNETC_DEFAULT_SPAWNER
      dflt = GASNETC_DEFAULT_SPAWNER;
      spawner_not_set = !gasneti_getenv(spawner_envvar); // Not traced
    #else
      gasneti_unreachable_error(("Call to gasneti_spawnerInit() without any default spawner defined"));
    #endif
    }
    // GASNET_<CONDUIT>_SPAWNER=FOO may be set explicitly by a user, or
    // implicitly by gasnetrun.  If it was not set by either of those means,
    // then the default will normally be the configure-time default.  However,
    // if the legacy GASNET_SPAWN_CONTROL envvar is set, it takes precedence
    // over the configure-time value.
    spawner = gasneti_getenv_withdefault(spawner_envvar, dflt);
  }
  gasneti_assert(spawner != NULL);

  match = !gasneti_strcasecmp(spawner, "MPI");
#if HAVE_MPI_SPAWNER
  // For bug 3406:
  // Try mpi-spawner first if GASNET_<CONDUIT>_SPAWNER is unset.
  // This is a requirement for spawning using bare mpirun.
  if (!res && (spawner_not_set || match)) {
    res = gasneti_bootstrapInit_mpi(argc_p, argv_p, nodes_p, mynode_p);
  }
  enabled += match;
#else
  disabled += match;
#endif

  match = !gasneti_strcasecmp(spawner, "SSH");
#if HAVE_SSH_SPAWNER
  // We do not attempt ssh-spawner in the absence of a (possibly default)
  // setting of GASNET_<CONDUIT>_SPAWNER.  Such could never be expected to
  // work, since a portion of the support logic is in the gasnetrun script.
  if (!res && match && !spawner_not_set) {
    res = gasneti_bootstrapInit_ssh(argc_p, argv_p, nodes_p, mynode_p);
  }
  enabled += match;
#else
  disabled += match;
#endif

  match = !gasneti_strcasecmp(spawner, "PMI");
#if HAVE_PMI_SPAWNER
  // We unofficially support "bare" (no gasnetrun script) PMI-based spawning
  // if GASNET_<CONDUIT>_SPAWNER is unset (assuming gasneti_bootstrapInit_mpi()
  // doesn't run first and fail fatally).
  if (!res && (spawner_not_set || match)) {
    res = gasneti_bootstrapInit_pmi(argc_p, argv_p, nodes_p, mynode_p);
  }
  enabled += match;
#else
  disabled += match;
#endif

  if (!res) {
    if (enabled) {
      gasneti_fatalerror("Requested spawner \"%s\" failed to initialize", spawner);
    } else if (disabled) {
      gasneti_fatalerror("Requested spawner \"%s\" is known, but not enabled in this build", spawner);
    } else if (! spawner_not_set) {
      gasneti_fatalerror("Requested spawner \"%s\" is unknown", spawner);
    } else {
      // TODO: enumerate the supported spawners
      gasneti_fatalerror("No supported spawner was able to initialize the job");
    }
  }

  gasneti_spawn_verbose = gasneti_getenv_yesno_withdefault("GASNET_SPAWN_VERBOSE",0);

  return res;
}

/* ------------------------------------------------------------------------------------ */
/* Simple container of segments
 *
 * Current implementation is a array, with deletions moving the last element
 * into the vacated slot to retain a dense table.  This design choice favors
 * simple/efficient iteration.
 *
 * The field `_opaque_container_use` in gasneti_Segment_t stores the index
 * of the segment in this table, to provide for O(1) deletion (w/o a search)
 * and is not inteded to be used (for instance) as an identifer on the wire.
 */

// State, protected by _gasneti_segtbl_lock
gasneti_mutex_t _gasneti_segtbl_lock = GASNETI_MUTEX_INITIALIZER;
gasneti_Segment_t *_gasneti_segtbl = NULL;
int _gasneti_segtbl_count = 0;

void gasneti_segtbl_add(gasneti_Segment_t seg) {
  gasneti_mutex_lock(&_gasneti_segtbl_lock);
  seg->_opaque_container_use = _gasneti_segtbl_count++;
  size_t space = _gasneti_segtbl_count * sizeof(gasneti_Segment_t);
  _gasneti_segtbl = gasneti_realloc(_gasneti_segtbl, space);
  gasneti_leak(_gasneti_segtbl);
  _gasneti_segtbl[seg->_opaque_container_use] = seg;
  gasneti_mutex_unlock(&_gasneti_segtbl_lock);
}

void gasneti_segtbl_del(gasneti_Segment_t seg) {
  gasneti_mutex_lock(&_gasneti_segtbl_lock);
  gasneti_Segment_t last = _gasneti_segtbl[--_gasneti_segtbl_count];
  last->_opaque_container_use = seg->_opaque_container_use;
  _gasneti_segtbl[last->_opaque_container_use] = last;
  // TODO: realloc to shrink if we think this would lead to significant savings?
  gasneti_mutex_unlock(&_gasneti_segtbl_lock);
}

/* ------------------------------------------------------------------------------------ */
/* Buffer management
 */
#if GASNET_DEBUGMALLOC || GASNET_DEBUG
  static uint64_t gasneti_memalloc_envint(const char *name, const char *deflt) {
    /* Signaling NaN: any bit pattern between 0x7ff0000000000001 and 0x7ff7ffffffffffff  
                   or any bit pattern between 0xfff0000000000001 and 0xfff7ffffffffffff
       Quiet NaN: any bit pattern between 0x7ff8000000000000 and 0x7fffffffffffffff 
               or any bit pattern between 0xfff8000000000000 and 0xffffffffffffffff
    */
    uint64_t sNAN = ((uint64_t)0x7ff7ffffffffffffULL); 
    uint64_t qNAN = ((uint64_t)0x7fffffffffffffffULL);
    uint64_t val = 0;
    const char *envval = gasneti_getenv_withdefault(name, deflt);
    const char *p = envval;
    char tmp[255];
    int i = 0;
    for ( ; *p; p++) {
      if (!isspace(*p)) tmp[i++] = toupper(*p);
      if (i == 254) break;
    }
    tmp[i] = '\0';
    if (!strcmp(tmp, "NAN")) return sNAN;
    else if (!strcmp(tmp, "SNAN")) return sNAN;
    else if (!strcmp(tmp, "QNAN")) return qNAN;
    else val = gasneti_parse_int(tmp, 0);
    if (val <= 0xFF) {
      int i;
      uint64_t byte = val;
      for (i = 0; i < 7; i++) {
        val = (val << 8) | byte;
      }
    }
    return val;
  }
  static void gasneti_memalloc_valset(void *p, size_t len, uint64_t val) {
    gasneti_assert(! ((uintptr_t)p & 7));
    uint64_t *output = p;
    size_t blocks = len/8;
    size_t extra = len%8;
    size_t i;
    for (i = 0; i < blocks; i++) {
      *output = val; 
      output++;
    }
    if (extra) memcpy(output, &val, extra);
  }
  static const void *gasneti_memalloc_valcmp(const void *p, size_t len, uint64_t val) {
    gasneti_assert(! ((uintptr_t)p & 7));
    const uint64_t *input = p;
    size_t blocks = len/8;
    size_t extra = len%8;
    size_t i;
    for (i = 0; i < blocks; i++) {
      if (*input != val) {
        const uint8_t *in = (uint8_t *)input;
        const uint8_t *cmp = (uint8_t *)&val;
        for (i = 0; i < 8; i++, in++, cmp++)
          if (*in != *cmp) return in;
        gasneti_fatalerror("bizarre failure in gasneti_memalloc_valcmp");
      }
      input++;
    }
    if (extra) {
      const uint8_t *in = (uint8_t *)input;
      const uint8_t *cmp = (uint8_t *)&val;
      for (i = 0; i < extra; i++, in++, cmp++)
        if (*in != *cmp) return in;
    }
    return NULL;
  }
#endif
#if GASNET_DEBUGMALLOC
/* ------------------------------------------------------------------------------------ */
/* Debug memory management
   debug memory format:
  | prev | next | allocdesc (2*sizeof(void*)) | datasz | BEGINPOST | <user data> | ENDPOST |
                                             ptr returned by malloc ^
 */
  /* READ BEFORE MODIFYING gasneti_memalloc_desc_t:
   *
   * malloc() is specified as returning memory "suitably aligned for any kind of variable".
   * We don't know a priori what that alignment is, but we MUST preserve it in
   * this debugging malloc implementation if we are to meet that same requirement.
   * The current length of gasneti_memalloc_desc_t is:
   *     ILP32: 32 bytes (4+4+4+4+8+8)
   *   [I]LP64: 48 bytes (8+8+8+8+8+8)
   * This means that any alignment up to 16-bytes will be preserved.  That is ideal
   * since 16 is the strictest alignment requirement (long double on some platforms)
   * that we have encountered in practice.
   *
   * If you change this structure, you MUST add padding to maintain the length
   * at a multiple of 16-bytes AND please update the lengths above.
   *
   * NOTE: If the malloc() returns less than 16-byte alignment, then
   * it is not our responsibility to create it where it did not exists.
   * Any GASNet code needing larger alignment than 4- or 8-bytes should
   * probably be using gasneti_{malloc,free}_aligned() (or gasnett_*).
   */
  typedef struct gasneti_memalloc_desc {  
    struct gasneti_memalloc_desc * volatile prevdesc;
    struct gasneti_memalloc_desc * volatile nextdesc;
    const char *allocdesc_str; /* a file name, or file name:linenum */
    uintptr_t   allocdesc_num; /* a line number, or zero for none */
    uint64_t datasz;
    uint64_t beginpost;
  } gasneti_memalloc_desc_t;
  static uint64_t gasneti_memalloc_allocatedbytes = 0;   /* num bytes ever allocated */
  static uint64_t gasneti_memalloc_freedbytes = 0;       /* num bytes ever freed */
  static uint64_t gasneti_memalloc_allocatedobjects = 0; /* num objects ever allocated */
  static uint64_t gasneti_memalloc_freedobjects = 0;     /* num objects ever freed */
  static uint64_t gasneti_memalloc_ringobjects = 0;      /* num objects in the ring */
  static uint64_t gasneti_memalloc_ringbytes = 0;        /* num bytes in the ring */
  static size_t   gasneti_memalloc_maxobjectsize = 0;    /* max object size ever allocated */
  static uintptr_t gasneti_memalloc_maxobjectloc = 0;    /* max address ever allocated */
  static uint64_t gasneti_memalloc_maxlivebytes = 0;     /* max num bytes live at any given time */
  static uint64_t gasneti_memalloc_maxliveobjects = 0;   /* max num bytes live at any given time */
  static int gasneti_memalloc_extracheck = 0;
  static int gasneti_memalloc_init = -1;
  static uint64_t gasneti_memalloc_initval = 0;
  static int gasneti_memalloc_clobber = -1;
  static uint64_t gasneti_memalloc_clobberval = 0;
  static int gasneti_memalloc_leakall = -1;
  static int gasneti_memalloc_scanfreed = -1;
  static int gasneti_memalloc_envisinit = 0;
  static gasneti_mutex_t gasneti_memalloc_lock = GASNETI_MUTEX_INITIALIZER;
  static gasneti_memalloc_desc_t *gasneti_memalloc_pos = NULL;
  #define GASNETI_MEM_BEGINPOST   ((uint64_t)0xDEADBABEDEADBABEULL)
  #define GASNETI_MEM_LEAKMARK    ((uint64_t)0xBABEDEADCAFEBEEFULL)
  #define GASNETI_MEM_ENDPOST     ((uint64_t)0xCAFEDEEDCAFEDEEDULL)
  #define GASNETI_MEM_FREEMARK    ((uint64_t)0xBEEFEFADBEEFEFADULL)
  #define GASNETI_MEM_HEADERSZ    (sizeof(gasneti_memalloc_desc_t))
  #define GASNETI_MEM_TAILSZ      8     
  #define GASNETI_MEM_EXTRASZ     (GASNETI_MEM_HEADERSZ+GASNETI_MEM_TAILSZ)     
  #define GASNETI_MEM_MALLOCALIGN 4
  #define gasneti_looksaligned(p) (!(((uintptr_t)(p)) & (GASNETI_MEM_MALLOCALIGN-1)))

  GASNETI_INLINE(gasneti_memalloc_envinit)
  void gasneti_memalloc_envinit(void) {
    if (!gasneti_memalloc_envisinit) {
      gasneti_mutex_lock(&gasneti_memalloc_lock);
        if (!gasneti_memalloc_envisinit && gasneti_init_done) {
          gasneti_memalloc_envisinit = 1; /* set first, because getenv might call malloc when tracing */
          gasneti_memalloc_init =       gasneti_getenv_yesno_withdefault("GASNET_MALLOC_INIT",0);
          gasneti_memalloc_initval =    gasneti_memalloc_envint("GASNET_MALLOC_INITVAL","NAN");
          gasneti_memalloc_clobber =    gasneti_getenv_yesno_withdefault("GASNET_MALLOC_CLOBBER",0);
          gasneti_memalloc_clobberval = gasneti_memalloc_envint("GASNET_MALLOC_CLOBBERVAL","NAN");
          gasneti_memalloc_leakall =    gasneti_getenv_yesno_withdefault("GASNET_MALLOC_LEAKALL", 0);
          gasneti_memalloc_scanfreed =  gasneti_getenv_yesno_withdefault("GASNET_MALLOC_SCANFREED", 0);
          gasneti_memalloc_extracheck = gasneti_getenv_yesno_withdefault("GASNET_MALLOC_EXTRACHECK", 0);
          if (gasneti_memalloc_scanfreed && !gasneti_memalloc_clobber) {
            gasneti_memalloc_clobber = 1;
            gasneti_console0_message("WARNING", "GASNET_MALLOC_SCANFREED requires GASNET_MALLOC_CLOBBER: enabling it.");
          }
          if (gasneti_memalloc_scanfreed && !gasneti_memalloc_leakall) {
            gasneti_memalloc_leakall = 1;
            gasneti_console0_message("WARNING", "GASNET_MALLOC_SCANFREED requires GASNET_MALLOC_LEAKALL: enabling it.");
          }
        }
      gasneti_mutex_unlock(&gasneti_memalloc_lock);
    }
  }

  #define GASNETI_MAX_LOCSZ 280
  /* formats a curloc into buffer[GASNETI_MAX_LOCSZ] and returns buffer */
  static char *_gasneti_format_curloc(char *buffer, const char *curloc) {
      char retval[GASNETI_MAX_LOCSZ];

      if (curloc == NULL) {
        sprintf(retval, buffer, "<unknown>");
      } else if (!strcmp(curloc,"SRCPOS")) {
        const char *filename = "<unknown>"; 
        unsigned int linenum = 0;
        char temp[GASNETI_MAX_LOCSZ];
        GASNETI_TRACE_GETSOURCELINE(&filename, &linenum); /* noop if not avail */
        sprintf(temp,"%s:%i", filename, linenum);
        sprintf(retval, buffer, temp);
      } else {
        sprintf(retval, buffer, curloc);
      }
      strcpy(buffer, retval);
      return buffer;
  }

  // bug 4366: avoid spammy assertions if we are running in the context of a
  // signal that arrived during a memcheck operation
  // _gasneti_mutex_trylock_mayberecursive() is a version of gasneti_mutex_trylock
  // that returns failure instead of asserting on a recursive attempt in DEBUG mode
  // TODO: possibly promote this to the general gasneti_mutex API?
  static int _gasneti_mutex_trylock_mayberecursive(gasneti_mutex_t *mut) {
    #ifdef _gasneti_mutex_heldbyme
      if (_gasneti_mutex_heldbyme(mut)) {
        // we know we are already holding this mutex, return failure instead of asserting below
        return EBUSY;
      }
    #endif
    return gasneti_mutex_trylock(mut);
  }

  extern void _gasneti_memcheck_one(const char *curloc) {
    if (gasneti_memalloc_extracheck) _gasneti_memcheck_all(curloc);
    else {
      if (_gasneti_mutex_trylock_mayberecursive(&gasneti_memalloc_lock)) return;
        if (gasneti_memalloc_pos) {
          _gasneti_memcheck(gasneti_memalloc_pos+1, curloc, 2);
          gasneti_memalloc_pos = gasneti_memalloc_pos->nextdesc;
        } else gasneti_assert_always(gasneti_memalloc_ringobjects == 0 && gasneti_memalloc_ringbytes == 0);
      gasneti_mutex_unlock(&gasneti_memalloc_lock);
    }
  }
  extern void _gasneti_memcheck_all(const char *curloc) {
    if (_gasneti_mutex_trylock_mayberecursive(&gasneti_memalloc_lock)) return;
      if (gasneti_memalloc_pos) {
        gasneti_memalloc_desc_t *begin = gasneti_memalloc_pos;
        uint64_t cnt;
        uint64_t sumsz = 0;
        for (cnt=0; cnt < gasneti_memalloc_ringobjects; cnt++) {
          sumsz += _gasneti_memcheck(gasneti_memalloc_pos+1, curloc, 2);
          gasneti_memalloc_pos = gasneti_memalloc_pos->nextdesc;
          if (gasneti_memalloc_pos == begin) break;
        } 
        if (cnt+1 != gasneti_memalloc_ringobjects || gasneti_memalloc_pos != begin || 
            sumsz != gasneti_memalloc_ringbytes) {
          gasneti_fatalerror("Debug malloc memcheck_all (called at %s) detected an error "
                             "in the memory ring linkage, most likely as a result of memory corruption.", 
                             curloc);
        }
      } else gasneti_assert_always(gasneti_memalloc_ringobjects == 0 && gasneti_memalloc_ringbytes == 0);
    gasneti_mutex_unlock(&gasneti_memalloc_lock);
  }

  /* assert the integrity of given memory block and return size of the user object 
      checktype == 0: check a live object
      checktype == 1: check an object which is about to be freed
      checktype == 2: check an object which resides in the ring (and may be dead)
  */
  extern size_t _gasneti_memcheck(void *ptr, const char *curloc, int checktype) {
    const char *corruptstr = NULL;
    char tmpstr[255];
    size_t nbytes = 0;
    const char *allocdesc_str = NULL;
    uintptr_t allocdesc_num = 0;
    uint64_t beginpost = 0;
    uint64_t endpost = 0;
    int doscan = 0;
    gasneti_assert_always_uint((unsigned int)checktype ,<=, 2);
    if (gasneti_looksaligned(ptr)) {
      gasneti_memalloc_desc_t *desc = ((gasneti_memalloc_desc_t *)ptr) - 1;
      beginpost = (desc->beginpost == GASNETI_MEM_LEAKMARK)
                     ? GASNETI_MEM_BEGINPOST : desc->beginpost;
      nbytes = (size_t)desc->datasz;
      if (nbytes == 0 || nbytes > gasneti_memalloc_maxobjectsize || 
          ((uintptr_t)ptr)+nbytes > gasneti_memalloc_maxobjectloc ||
          !desc->prevdesc || !desc->nextdesc || 
          !gasneti_looksaligned(desc->prevdesc) || 
          !gasneti_looksaligned(desc->nextdesc)) {
            nbytes = 0; /* bad metadata, don't trust any of it */
      } else {
        allocdesc_str = desc->allocdesc_str;
        allocdesc_num = desc->allocdesc_num;
        memcpy(&endpost,((char*)ptr)+nbytes,GASNETI_MEM_TAILSZ);
      }
    }
    if (beginpost == GASNETI_MEM_FREEMARK) {
      switch (checktype) {
        case 0: /* should be a live object */
          corruptstr = "Debug malloc memcheck() called on freed memory (may indicate local heap corruption)";
          break;
        case 1: /* about to be freed - should still be a live object */
          corruptstr = "Debug free detected a duplicate free() or local heap corruption";
          break;
        case 2:
          if (gasneti_memalloc_scanfreed <= 0) /* freed objects should not be in ring */
            corruptstr = "Debug malloc found a freed object in the memory ring, indicating local heap corruption";
          else doscan = 1;
          break;
      }
    }  
    if (beginpost != GASNETI_MEM_FREEMARK && 
        (beginpost != GASNETI_MEM_BEGINPOST || endpost != GASNETI_MEM_ENDPOST)) {
      const char *diagnosis = "a bad pointer or local heap corruption";
      #if !GASNET_SEGMENT_EVERYTHING
        // TODO-EX: multi-segment equivalent?
        gasneti_EP_t i_ep = gasneti_import_ep(gasneti_THUNK_EP);
        if (gasneti_attach_done && gasneti_in_local_segment(i_ep,ptr,1))
          diagnosis = "a bad pointer, referencing the shared segment (outside malloc heap)";
        else 
      #endif
      if (nbytes && beginpost == GASNETI_MEM_BEGINPOST && endpost != GASNETI_MEM_ENDPOST)
        diagnosis = "local heap corruption (probable buffer overflow)";
      else if (nbytes && beginpost != GASNETI_MEM_BEGINPOST && endpost == GASNETI_MEM_ENDPOST)
        diagnosis = "local heap corruption (probable buffer underflow)";
      if (checktype == 1) {
        sprintf(tmpstr, "Debug free detected %s", diagnosis);
      } else {
        sprintf(tmpstr, "Debug malloc memcheck() detected %s", diagnosis);
      }
      corruptstr = tmpstr;
    }
    if (corruptstr == NULL && doscan) {
      const void *badloc = gasneti_memalloc_valcmp(ptr, nbytes, gasneti_memalloc_clobberval);
      if (badloc) {
        sprintf(tmpstr, "Debug malloc memcheck() detected a write to freed memory at object offset: %i bytes",
                        (int)((uintptr_t)badloc - (uintptr_t)ptr));
        corruptstr = tmpstr;
      }
    }

    if (corruptstr != NULL) {
      char nbytesstr[80];
      char allocstr[GASNETI_MAX_LOCSZ];
      const char *allocdesc;
      char curlocstr[GASNETI_MAX_LOCSZ];

      if (allocdesc_str != NULL && memchr(allocdesc_str,'\0',255) == 0) { /* allocptr may be bad */
        allocdesc = NULL; 
      } else {
        if (allocdesc_num) {
          sprintf(allocstr,"\n   allocated at: %s:%i",allocdesc_str,(int)allocdesc_num);
        } else {
          sprintf(allocstr,"\n   allocated at: %s",allocdesc_str);
        }
        allocdesc = allocstr;
      }
      if (allocdesc == NULL) nbytesstr[0] = '\0';
      else sprintf(nbytesstr," nbytes=%i",(int)nbytes);

      if (checktype == 1) strcpy(curlocstr,"\n   freed at: %s");
      else                strcpy(curlocstr,"\n   detected at: %s");

      gasneti_fatalerror("%s\n   ptr="GASNETI_LADDRFMT"%s%s%s",
           corruptstr,
           GASNETI_LADDRSTR(ptr), nbytesstr,
           (allocdesc!=NULL?allocdesc:""),
           _gasneti_format_curloc(curlocstr,curloc));
    }
    return nbytes;
  }

  /* get access to system malloc/free */
  #undef malloc
  #undef free
  static void *_gasneti_malloc_inner(int allowfail, size_t nbytes, const char *curloc) {
    void *ret = NULL;
    gasneti_memalloc_envinit();
    _gasneti_memcheck_one(curloc);
    GASNETI_STAT_EVENT_VAL(I, GASNET_MALLOC, nbytes);
    if_pf (nbytes == 0) {
      return NULL;
    }
    ret = malloc(nbytes+GASNETI_MEM_EXTRASZ);
    gasneti_assert_always_uint((((uintptr_t)ret) & 0x3) ,==, 0); /* should have at least 4-byte alignment */
    if_pf (ret == NULL) {
      char curlocstr[GASNETI_MAX_LOCSZ];
      strcpy(curlocstr, "\n   at: %s");
      if (allowfail) {
        GASNETI_TRACE_PRINTF(I,("Warning: returning NULL for a failed gasneti_malloc(%"PRIuPTR")%s",
                                (uintptr_t)nbytes, _gasneti_format_curloc(curlocstr,curloc)));
        return NULL;
      }
      gasneti_fatalerror("Debug malloc(%"PRIuPTR") failed (%"PRIu64" bytes in use, in %"PRIu64" objects)%s", 
                     (uintptr_t)nbytes, 
                     (gasneti_memalloc_allocatedbytes - gasneti_memalloc_freedbytes),
                     (gasneti_memalloc_allocatedobjects - gasneti_memalloc_freedobjects),
                     _gasneti_format_curloc(curlocstr,curloc));
    } else {
      uint64_t gasneti_endpost_ref = GASNETI_MEM_ENDPOST;
      gasneti_memalloc_desc_t *desc = ret;
      if (!strcmp(curloc,"SRCPOS")) {
        const char *filename = "<unknown>"; 
        unsigned int linenum = 0;
        GASNETI_TRACE_GETSOURCELINE(&filename, &linenum); /* noop if not avail */
        desc->allocdesc_str = filename;
        desc->allocdesc_num = linenum;
      } else {
        desc->allocdesc_str = curloc;
        desc->allocdesc_num = 0;
      }
      desc->datasz = (uint64_t)nbytes;
      desc->beginpost = GASNETI_MEM_BEGINPOST;
      memcpy(((char*)ret)+nbytes+GASNETI_MEM_HEADERSZ, &gasneti_endpost_ref, GASNETI_MEM_TAILSZ);

      gasneti_mutex_lock(&gasneti_memalloc_lock);
        gasneti_memalloc_allocatedbytes += nbytes;
        gasneti_memalloc_allocatedobjects++;
        gasneti_memalloc_ringobjects++;
        gasneti_memalloc_ringbytes += nbytes;
        if (nbytes > gasneti_memalloc_maxobjectsize) gasneti_memalloc_maxobjectsize = nbytes;
        if (((uintptr_t)ret)+nbytes+GASNETI_MEM_HEADERSZ > gasneti_memalloc_maxobjectloc) 
          gasneti_memalloc_maxobjectloc = ((uintptr_t)ret)+nbytes+GASNETI_MEM_HEADERSZ;
        gasneti_memalloc_maxlivebytes = 
          MAX(gasneti_memalloc_maxlivebytes, gasneti_memalloc_allocatedbytes-gasneti_memalloc_freedbytes);
        gasneti_memalloc_maxliveobjects = 
          MAX(gasneti_memalloc_maxliveobjects, gasneti_memalloc_allocatedobjects-gasneti_memalloc_freedobjects);
        if (gasneti_memalloc_pos == NULL) { /* first object */
          gasneti_memalloc_pos = desc;
          desc->prevdesc = desc;
          desc->nextdesc = desc;
        } else { /* link into ring */
          desc->prevdesc = gasneti_memalloc_pos->prevdesc;
          desc->nextdesc = gasneti_memalloc_pos;
          gasneti_memalloc_pos->prevdesc->nextdesc = desc;
          gasneti_memalloc_pos->prevdesc = desc;
        }
      gasneti_mutex_unlock(&gasneti_memalloc_lock);

      ret = desc+1;
      if (gasneti_memalloc_init > 0) gasneti_memalloc_valset(ret, nbytes, gasneti_memalloc_initval);
    }
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }
  extern void *_gasneti_malloc(size_t nbytes, const char *curloc) {
    return _gasneti_malloc_inner(0, nbytes, curloc);
  }
  extern void *_gasneti_malloc_allowfail(size_t nbytes, const char *curloc) {
    return _gasneti_malloc_inner(1, nbytes, curloc);
  }

  extern void _gasneti_free(void *ptr, const char *curloc) {
    size_t nbytes;
    gasneti_memalloc_desc_t *desc;
    gasneti_memalloc_envinit();
    _gasneti_memcheck_one(curloc);
    if_pf (ptr == NULL) return;
    nbytes = _gasneti_memcheck(ptr, curloc, 1);
    GASNETI_STAT_EVENT_VAL(I, GASNET_FREE, nbytes);
    desc = ((gasneti_memalloc_desc_t *)ptr) - 1;
    if (gasneti_memalloc_clobber > 0) gasneti_memalloc_valset(desc+1, nbytes, gasneti_memalloc_clobberval);

    gasneti_mutex_lock(&gasneti_memalloc_lock);
      desc->beginpost = GASNETI_MEM_FREEMARK;
      gasneti_memalloc_freedbytes += nbytes;
      gasneti_memalloc_freedobjects++;
      if (gasneti_memalloc_scanfreed <= 0) {
        gasneti_memalloc_ringobjects--;
        gasneti_memalloc_ringbytes -= nbytes;
        if (desc->nextdesc == desc) { /* last item in list */
          gasneti_assert_always(desc->prevdesc == desc && gasneti_memalloc_ringobjects == 0);
          gasneti_memalloc_pos = NULL;
        } else {
          if (gasneti_memalloc_pos == desc) gasneti_memalloc_pos = desc->nextdesc;
          desc->prevdesc->nextdesc = desc->nextdesc;
          desc->nextdesc->prevdesc = desc->prevdesc;
        }
      }
    gasneti_mutex_unlock(&gasneti_memalloc_lock);

    if (gasneti_memalloc_leakall <= 0) free(desc);
  }

  extern void *_gasneti_calloc(size_t N, size_t S, const char *curloc) {
    void *ret;
    size_t nbytes = N*S;
    if_pf (nbytes == 0) return NULL;
    ret = _gasneti_malloc(nbytes, curloc);
    memset(ret,0,nbytes);
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }
  extern void *_gasneti_realloc(void *ptr, size_t sz, const char *curloc) {
    void *ret = _gasneti_malloc(sz, curloc);
    if_pt (ptr != NULL) {
      size_t nbytes = _gasneti_memcheck(ptr, curloc, 0);
      memcpy(ret, ptr, MIN(nbytes, sz));
      _gasneti_free(ptr, curloc);
    }
    if (sz) {
      _gasneti_memcheck(ret,curloc,0);
    }
    return ret;
  }
  extern void _gasneti_leak(void *ptr, const char *curloc) {
    gasneti_memalloc_desc_t *desc;
    if_pf (ptr == NULL) return;
    _gasneti_memcheck(ptr, curloc, 0);
    desc = ((gasneti_memalloc_desc_t *)ptr) - 1;
    gasneti_mutex_lock(&gasneti_memalloc_lock);
      desc->beginpost = GASNETI_MEM_LEAKMARK;
    gasneti_mutex_unlock(&gasneti_memalloc_lock);
  }

  extern int gasneti_getheapstats(gasneti_heapstats_t *pstat) {
    pstat->allocated_bytes = gasneti_memalloc_allocatedbytes;
    pstat->freed_bytes = gasneti_memalloc_freedbytes;
    pstat->live_bytes = gasneti_memalloc_allocatedbytes - gasneti_memalloc_freedbytes;
    pstat->live_bytes_max = gasneti_memalloc_maxlivebytes;
    pstat->allocated_objects = gasneti_memalloc_allocatedobjects;
    pstat->freed_objects = gasneti_memalloc_freedobjects;
    pstat->live_objects = gasneti_memalloc_allocatedobjects - gasneti_memalloc_freedobjects;
    pstat->live_objects_max = gasneti_memalloc_maxliveobjects;
    pstat->overhead_bytes = gasneti_memalloc_ringbytes - pstat->live_bytes + 
                            gasneti_memalloc_ringobjects*GASNETI_MEM_EXTRASZ;
    return 0;
  }

  extern void gasneti_malloc_dump_liveobjects(FILE *fp) {
    gasneti_mutex_lock(&gasneti_memalloc_lock);
      if (gasneti_memalloc_pos) {
        gasneti_memalloc_desc_t *pos = gasneti_memalloc_pos;
        uint64_t cnt;
        for (cnt=0; cnt < gasneti_memalloc_ringobjects; cnt++) {
          uint64_t datasz = pos->datasz;
          const char * allocptr = NULL;
          char allocdesc[GASNETI_MAX_LOCSZ];
          if (!pos->allocdesc_str) {
            allocptr = NULL;
          } else if (pos->allocdesc_num) {
            sprintf(allocdesc,"%s:%i",pos->allocdesc_str,(int)pos->allocdesc_num);
            allocptr = allocdesc;
          } else {
            allocptr = pos->allocdesc_str;
          }
          fprintf(fp, "   %10lu %c   %s\n",
                  (unsigned long)datasz,
                  (pos->beginpost == GASNETI_MEM_LEAKMARK ? '*' : ' '),
                  (allocptr?allocptr:"<unknown>"));
          pos = pos->nextdesc;
        } 
      } 
    gasneti_mutex_unlock(&gasneti_memalloc_lock);
  }

  extern void gasneti_heapinfo_dump(const char *filename, int show_live_objects) {
    static gasneti_mutex_t lock = GASNETI_MUTEX_INITIALIZER;
    gasneti_mutex_lock(&lock);
    FILE *fp = gasneti_open_outputfile(filename, "debugmalloc heap report");
    if (fp) {
      time_t ltime;
      char temp[1024];
      time(&ltime);
      strcpy(temp, ctime(&ltime));
      if (temp[strlen(temp)-1] == '\n') temp[strlen(temp)-1] = '\0';

      gasnett_heapstats_t stats;
      gasnett_getheapstats(&stats);
      fprintf(fp, "# GASNet Debug Mallocator Report\n");
      fprintf(fp, "#\n");
      fprintf(fp, "# program: %s\n",gasneti_exe_name());
      fprintf(fp, "# date:    %s\n",temp);
      fprintf(fp, "# host:    %s\n",gasnett_gethostname());
      fprintf(fp, "# pid:     %i\n",(int)getpid());
      fprintf(fp, "# node:    %i / %i\n", (int)gasneti_mynode, (int)gasneti_nodes);
      fprintf(fp, "#\n");
      fprintf(fp, "# Private memory utilization:\n");
      fprintf(fp, "# ---------------------------\n");
      fprintf(fp, "#\n");
      fprintf(fp, "# malloc() space total:        %10"PRIu64" bytes, in %10"PRIu64" objects\n",
                  stats.allocated_bytes, stats.allocated_objects);
      fprintf(fp, "# malloc() space in-use:       %10"PRIu64" bytes, in %10"PRIu64" objects\n",
                  stats.live_bytes, stats.live_objects);
      fprintf(fp, "# malloc() space freed:        %10"PRIu64" bytes, in %10"PRIu64" objects\n",
                  stats.freed_bytes, stats.freed_objects);
      fprintf(fp, "# malloc() space peak usage:   %10"PRIu64" bytes,    %10"PRIu64" objects\n",
                  stats.live_bytes_max, stats.live_objects_max);
      fprintf(fp, "# malloc() system overhead: >= %10"PRIu64" bytes\n",
                  stats.overhead_bytes);
      fprintf(fp, "#\n");
    
      gasneti_memcheck_all(); /* check ring sanity */
   
      if (show_live_objects) { 
        fprintf(fp, "# Live objects\n");
        fprintf(fp, "# ------------\n");
        fprintf(fp, "#\n");
        fprintf(fp, "# Table below shows objects allocated, but not freed.\n");
        fprintf(fp, "# Note that GASNet does not free most of its internal permanent data structures,\n");
        fprintf(fp, "# in order to streamline job shutdown.  An asterisk (*) following the size\n");
        fprintf(fp, "# identifies objects known to correspond to these permanent allocations.\n");
        fprintf(fp, "#\n");
        fprintf(fp, "# Object size     Location Allocated\n");
        fprintf(fp, "# ==================================\n");
    
        gasneti_malloc_dump_liveobjects(fp);
      }
      if (fp != stdout && fp != stderr) fclose(fp);
    }
    gasneti_mutex_unlock(&lock);
  }
#endif
/* extern versions of gasnet malloc fns for use in public headers */
extern void *_gasneti_extern_malloc(size_t sz GASNETI_CURLOCFARG) {
  return _gasneti_malloc(sz GASNETI_CURLOCPARG);
}
extern void *_gasneti_extern_realloc(void *ptr, size_t sz GASNETI_CURLOCFARG) {
  return _gasneti_realloc(ptr, sz GASNETI_CURLOCPARG);
}
extern void *_gasneti_extern_calloc(size_t N, size_t S GASNETI_CURLOCFARG) {
  return _gasneti_calloc(N,S GASNETI_CURLOCPARG);
}
extern void _gasneti_extern_free(void *ptr GASNETI_CURLOCFARG) {
  _gasneti_free(ptr GASNETI_CURLOCPARG);
}
extern void _gasneti_extern_leak(void *ptr GASNETI_CURLOCFARG) {
  _gasneti_leak(ptr GASNETI_CURLOCPARG);
}
extern char *_gasneti_extern_strdup(const char *s GASNETI_CURLOCFARG) {
  return _gasneti_strdup(s GASNETI_CURLOCPARG);
}
extern char *_gasneti_extern_strndup(const char *s, size_t n GASNETI_CURLOCFARG) {
  return _gasneti_strndup(s,n GASNETI_CURLOCPARG);
}

// append to a string with dynamic memory allocation
// not high-performance, but concise
char *gasneti_sappendf(char *s, const char *fmt, ...) {
  // compute length of thing to append
  va_list args;
  va_start(args, fmt);
  int add_len = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  // grow (or allocate) the string, including space for '\0'
  int old_len = s ? strlen(s) : 0;
  s = gasneti_realloc(s, old_len + add_len + 1);

  // append
  va_start(args, fmt);
  vsprintf((s+old_len), fmt, args);
  va_end(args);

  return s;
}

// case-insensitive string comparison
// same semantics as the POSIX-1.2001 equivalent except for NULL arguments,
// which these functions treat as indistinguishable from a pointer to '\0'
int gasneti_strcasecmp(const char *s1, const char *s2) {
  static char zero = '\0';
  if (!s1) s1 = &zero;
  if (!s2) s2 = &zero;
  size_t i = 0;
  while (s1[i] && s2[i]) {
    char a = tolower(s1[i]);
    char b = tolower(s2[i]);
    if (a != b) return ((a < b) ? -1 : 1);
    ++i;
  }
  if (!s1[i] && !s2[i]) return 0; // ended together (identical)
  else return (s2[i] ? -1 : 1); // shorter string is the lesser
}

int gasneti_strncasecmp(const char *s1, const char *s2, size_t n) {
  static char zero = '\0';
  if (!s1) s1 = &zero;
  if (!s2) s2 = &zero;
  size_t i = 0;
  while ((i < n) && s1[i] && s2[i]) {
    char a = tolower(s1[i]);
    char b = tolower(s2[i]);
    if (a != b) return ((a < b) ? -1 : 1);
    ++i;
  }
  if (i == n) return 0; // first n chars were identical
  if (!s1[i] && !s2[i]) return 0; // ended together (identical)
  else return (s2[i] ? -1 : 1); // shorter string is the lesser
}

#if GASNET_DEBUGMALLOC
  extern void *(*gasnett_debug_malloc_fn)(size_t sz, const char *curloc);
  extern void *(*gasnett_debug_calloc_fn)(size_t N, size_t S, const char *curloc);
  extern void *(*gasnett_debug_realloc_fn)(void *ptr, size_t sz, const char *curloc);
  extern void (*gasnett_debug_free_fn)(void *ptr, const char *curloc);
  extern char *(*gasnett_debug_strdup_fn)(const char *ptr, const char *curloc);
  extern char *(*gasnett_debug_strndup_fn)(const char *ptr, size_t n, const char *curloc);
  void *(*gasnett_debug_malloc_fn)(size_t sz, const char *curloc) =
         &_gasneti_extern_malloc;
  void *(*gasnett_debug_calloc_fn)(size_t N, size_t S, const char *curloc) =
         &_gasneti_extern_calloc;
  void *(*gasnett_debug_realloc_fn)(void *ptr, size_t sz, const char *curloc) =
        &_gasneti_extern_realloc;
  void (*gasnett_debug_free_fn)(void *ptr, const char *curloc) =
         &_gasneti_extern_free;
  char *(*gasnett_debug_strdup_fn)(const char *s, const char *curloc) =
         &_gasneti_extern_strdup;
  char *(*gasnett_debug_strndup_fn)(const char *s, size_t sz, const char *curloc) =
         &_gasneti_extern_strndup;
  /* these only exist with debug malloc */
  extern void (*gasnett_debug_memcheck_fn)(void *ptr, const char *curloc);
  extern void (*gasnett_debug_memcheck_one_fn)(const char *curloc);
  extern void (*gasnett_debug_memcheck_all_fn)(const char *curloc);
  extern void _gasneti_extern_memcheck(void *ptr, const char *curloc) {
    _gasneti_memcheck(ptr, curloc, 0);
  }
  void (*gasnett_debug_memcheck_fn)(void *ptr, const char *curloc) = 
        &_gasneti_extern_memcheck;
  void (*gasnett_debug_memcheck_one_fn)(const char *curloc) = 
        &_gasneti_memcheck_one;
  void (*gasnett_debug_memcheck_all_fn)(const char *curloc) =
        &_gasneti_memcheck_all;
#endif

/* don't put anything here - malloc stuff must come last */
