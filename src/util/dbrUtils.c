/*
 * Copyright © 2018 IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include "logutil.h"
#include "libdatabroker.h"
#include "libdatabroker_int.h"

#include "lib/sge.h"
#include "lib/backend.h"

#ifdef __APPLE__
#include <stdlib.h>
#else
#include <stdlib.h>
#include <malloc.h>
#endif
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <pthread.h>

static dbrMain_context_t *gMain_context = NULL;
static pthread_mutex_t gMain_creation_lock = PTHREAD_MUTEX_INITIALIZER;

dbrMain_context_t* dbrCheckCreateMainCTX(void)
{
  pthread_mutex_lock( &gMain_creation_lock );
  if( gMain_context == NULL )
  {
    gMain_context = (dbrMain_context_t*)malloc( sizeof( dbrMain_context_t ) );
    if( gMain_context == NULL )
    {
      errno = ENOMEM;
      pthread_mutex_unlock( &gMain_creation_lock );
      return NULL;
    }
    memset( gMain_context, 0, sizeof( dbrMain_context_t ) );

    char *to_str = getenv(DBR_TIMEOUT_ENV);
    if( to_str == NULL )
      gMain_context->_config._timeout_sec = DBR_TIMEOUT_DEFAULT;
    else
    {
      gMain_context->_config._timeout_sec = strtol( to_str, NULL, 10 );
      if(( gMain_context->_config._timeout_sec == LONG_MIN ) || ( gMain_context->_config._timeout_sec == LONG_MAX ))
        gMain_context->_config._timeout_sec = DBR_TIMEOUT_DEFAULT;

    }
    if( gMain_context->_config._timeout_sec == 0 )
      gMain_context->_config._timeout_sec = INT_MAX;

    gMain_context->_be_ctx = dbrlib_backend_get_handle();
    if( gMain_context->_be_ctx == NULL )
    {
      fprintf( stderr, "libdatabroker: failed to create/connect backend.\n" );
      free( gMain_context );
      gMain_context = NULL;
      pthread_mutex_unlock( &gMain_creation_lock );
      return NULL;
    }
    pthread_mutex_init( &gMain_context->_biglock, NULL );
  }
  pthread_mutex_unlock( &gMain_creation_lock );
  return gMain_context;
}

int dbrMain_exit(void)
{
  pthread_mutex_lock( &gMain_creation_lock );
  if( gMain_context == NULL )
  {
    pthread_mutex_unlock( &gMain_creation_lock );
    return 0;
  }

  int rc = dbrlib_backend_delete( gMain_context->_be_ctx );

  pthread_mutex_destroy( &gMain_context->_biglock );
  memset( gMain_context, 0, sizeof( gMain_context ) );
  free( gMain_context );
  gMain_context = NULL;

  pthread_mutex_unlock( &gMain_creation_lock );
  return rc;
}


DBR_Tag_t dbrTag_get( dbrMain_context_t *ctx )
{
  typeof(ctx->_tag_head) t = ctx->_tag_head;

  // hop through the work entries to check for available tags
  while( ctx->_cs_wq[ t ] != NULL )
  {
    // while we're at it: clean up any closed request entries
    if( ctx->_cs_wq[ t ]->_status != dbrSTATUS_CLOSED )
    {
      dbrRequestContext_t *r = ctx->_cs_wq[ t ];
      memset( r, 0, sizeof( dbrRequestContext_t ) + r->_req._sge_count * sizeof(dbBE_sge_t) );
      free( r );
      ctx->_cs_wq[ t ] = NULL;
      break;
    }

    t = ( t + 1 ) % dbrMAX_TAGS;
    if( t == ctx->_tag_head )
    {
      LOG( DBG_ERR, stderr, "No more tags available for async op\n" );
      return DB_TAG_ERROR;
    }
  }

  ctx->_tag_head = ( t + 1 ) % dbrMAX_TAGS;

#ifdef DBR_INTTAG
//  LOG( DBG_INFO, stdout, "Returning Tag: %d\n", t );
  return (DBR_Tag_t)t;
#else
  // needs to point into the array!
  // -> otherwise there is no index calculation afterwards
  return (DBR_Tag_t)&ctx->_cs_wq[ t ];
#endif
}

DBR_Errorcode_t dbrValidateTag( dbrRequestContext_t *rctx, DBR_Tag_t req_tag )
{
#ifdef DBR_INTTAG
  if(( req_tag >= 0 ) && ( req_tag < dbrMAX_TAGS ))
#else
  if(( req_tag != NULL ) && ( req_tag != DB_TAG_ERROR ))
#endif
    return DBR_SUCCESS;
  else
    return DBR_ERR_TAGERROR;
}
