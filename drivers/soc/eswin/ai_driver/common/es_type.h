
#ifndef __ES_TYPE_H__
#define __ES_TYPE_H__

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/*----------------------------------------------*
 * The common data type, will be used in the whole project.*
 *----------------------------------------------*/

typedef unsigned char ES_U8;
typedef unsigned short ES_U16;
typedef unsigned int ES_U32;

typedef signed char ES_S8;
typedef short ES_S16;
typedef int ES_S32;

typedef unsigned long ES_UL;
typedef signed long ES_SL;

typedef float ES_FLOAT;
typedef double ES_DOUBLE;

typedef unsigned long long ES_U64;
typedef long long ES_S64;

typedef char ES_CHAR;
#define ES_VOID void

typedef unsigned int ES_HANDLE;

/*----------------------------------------------*
 * const defination                             *
 *----------------------------------------------*/
typedef enum {
    ES_FALSE = 0,
    ES_TRUE = 1,
} ES_BOOL;

/*----------------------------------------------*
 * log level defination                         *
 *----------------------------------------------*/
typedef enum {
    ES_LOG_MIN         = -1,
    ES_LOG_EMERGENCY   = 0,
    ES_LOG_ALERT       = 1,
    ES_LOG_CRITICAL    = 2,
    ES_LOG_ERROR       = 3,
    ES_LOG_WARN        = 4,
    ES_LOG_NOTICE      = 5,
    ES_LOG_INFO        = 6,
    ES_LOG_DEBUG       = 7,
    ES_LOG_MAX
} ES_LOG_LEVEL_E;

#ifndef NULL
#define NULL 0L
#endif

#define ES_NULL 0L
#define ES_SUCCESS 0
#define ES_FAILURE (-1)

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __ES_TYPE_H__ */
