/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 * -*- */

/* coap -- simple implementation of the Constrained Application Protocol (CoAP)
 *         as defined in RFC 7252
 *
 * Copyright (C) 2010--2016 Olaf Bergmann <bergmann@tzi.org>
 *
 * This file is part of the CoAP library libcoap. Please see README for terms
 * of use.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#include "getopt.c"
#else
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#endif

#include <coap/coap.h>
#include <coap/coap_dtls.h>
#include <coap/uthash.h>

/***********************************/
#define REQUIRE_ETAG 0x01 	/* flag for coap_payload_t: require ETag option  */
typedef struct {
  UT_hash_handle hh;
  coap_key_t resource_key;	/* foreign key that points into resource space */
  unsigned int flags;		/* some flags to control behavior */
  size_t max_data;		/* maximum size allocated for @p data */
  uint16_t media_type;		/* media type for this object */
  size_t length;		/* length of data */
  unsigned char data[];		/* the actual contents */
} coap_payload_t;

coap_payload_t *test_resources = NULL;

/**
 * This structure is used to store URIs for dynamically allocated
 * resources, usually by POST or PUT.
 */
typedef struct {
  UT_hash_handle hh;
  coap_key_t resource_key;	/* foreign key that points into resource space */
  size_t length;		/* length of data */
  unsigned char data[];		/* the actual contents */
} coap_dynamic_uri_t;

coap_dynamic_uri_t *test_dynamic_uris = NULL;

/***********************************/

#define COAP_RESOURCE_CHECK_TIME 2

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

/* temporary storage for dynamic resource representations */
static int quit = 0;


#ifndef WITHOUT_ASYNC
/* This variable is used to mimic long-running tasks that require
 * asynchronous responses. */
static coap_async_state_t *async = NULL;
#endif /* WITHOUT_ASYNC */

#ifdef __GNUC__
#define UNUSED_PARAM __attribute__ ((unused))
#else /* not a GCC */
#define UNUSED_PARAM
#endif /* GCC */

/* SIGINT handler: set quit to 1 for graceful termination */
static void
handle_sigint(int signum UNUSED_PARAM) {
  quit = 1;
}

#define INDEX "This is a test server made with libcoap (see https://libcoap.net)\n" \
              "Copyright (C) 2010--2016 Olaf Bergmann <bergmann@tzi.org>\n\n"

static void
hnd_get_index(coap_context_t *ctx UNUSED_PARAM,
              struct coap_resource_t *resource UNUSED_PARAM,
              coap_session_t *session UNUSED_PARAM,
              coap_pdu_t *request UNUSED_PARAM,
              str *token UNUSED_PARAM,
              coap_pdu_t *response) {
  unsigned char buf[3];

  response->hdr->code = COAP_RESPONSE_CODE(205);

  coap_add_option(response,
                  COAP_OPTION_CONTENT_TYPE,
                  coap_encode_var_bytes(buf, COAP_MEDIATYPE_TEXT_PLAIN), buf);

  coap_add_option(response,
                  COAP_OPTION_MAXAGE,
                  coap_encode_var_bytes(buf, 0x2ffff), buf);

  coap_add_data(response, strlen(INDEX), (unsigned char *)INDEX);
}


#ifndef WITHOUT_ASYNC
static void
hnd_get_async(coap_context_t *ctx,
              struct coap_resource_t *resource UNUSED_PARAM,
              coap_session_t *session,
              coap_pdu_t *request,
              str *token UNUSED_PARAM,
              coap_pdu_t *response) {
  coap_opt_iterator_t opt_iter;
  coap_opt_t *option;
  unsigned long delay = 5;
  size_t size;

  if (async) {
    if (async->id != request->hdr->id) {
      coap_opt_filter_t f;
      coap_option_filter_clear(f);
      response->hdr->code = COAP_RESPONSE_CODE(503);
    }
    return;
  }

  option = coap_check_option(request, COAP_OPTION_URI_QUERY, &opt_iter);
  if (option) {
    unsigned char *p = COAP_OPT_VALUE(option);

    delay = 0;
    for (size = COAP_OPT_LENGTH(option); size; --size, ++p)
      delay = delay * 10 + (*p - '0');
  }

  async = coap_register_async(ctx,
                              session,
                              request,
                              COAP_ASYNC_SEPARATE | COAP_ASYNC_CONFIRM,
                              (void *)(COAP_TICKS_PER_SECOND * delay));
}

static void
check_async(coap_context_t *ctx,
            coap_tick_t now) {
  coap_pdu_t *response;
  coap_async_state_t *tmp;

  size_t size = sizeof(coap_hdr_t) + 13;

  if (!async || now < async->created + (unsigned long)async->appdata)
    return;

  response = coap_pdu_init(async->flags & COAP_ASYNC_CONFIRM
             ? COAP_MESSAGE_CON
             : COAP_MESSAGE_NON,
             COAP_RESPONSE_CODE(205), 0, size);
  if (!response) {
    debug("check_async: insufficient memory, we'll try later\n");
    async->appdata =
      (void *)((unsigned long)async->appdata + 15 * COAP_TICKS_PER_SECOND);
    return;
  }

  response->hdr->id = coap_new_message_id(async->session);

  if (async->tokenlen)
    coap_add_token(response, async->tokenlen, async->token);

  coap_add_data(response, 4, (unsigned char *)"done");

  if (coap_send(async->session, response) == COAP_INVALID_TID) {
    debug("check_async: cannot send response for message %d\n",
    ntohs(response->hdr->id));
  }
  coap_remove_async(ctx, async->session, async->id, &tmp);
  coap_free_async(async);
  async = NULL;
}
#endif /* WITHOUT_ASYNC */


/************************************************************/
static inline coap_payload_t *
coap_find_payload(const coap_key_t key) {
  coap_payload_t *p;
  HASH_FIND(hh, test_resources, key, sizeof(coap_key_t), p);
  return p;
}


coap_payload_t *
coap_new_payload(size_t size) {
  coap_payload_t *p;
  p = (coap_payload_t *)coap_malloc(sizeof(coap_payload_t) + size);
  if (p) {
    memset(p, 0, sizeof(coap_payload_t));
    p->max_data = size;
  }

  return p;
}

static inline void
coap_add_payload(const coap_key_t key, coap_payload_t *payload,
		 coap_dynamic_uri_t *uri) {
  assert(payload);

  memcpy(payload->resource_key, key, sizeof(coap_key_t));
  HASH_ADD(hh, test_resources, resource_key, sizeof(coap_key_t), payload);

  if (uri) {
    memcpy(uri->resource_key, key, sizeof(coap_key_t));
    HASH_ADD(hh, test_dynamic_uris, resource_key, sizeof(coap_key_t), uri);
  }
}


static inline void
coap_delete_payload(coap_payload_t *payload) {
  if (payload) {
    coap_dynamic_uri_t *uri;
    HASH_FIND(hh, test_dynamic_uris,
	      payload->resource_key, sizeof(coap_key_t), uri);
    if (uri) {
      HASH_DELETE(hh, test_dynamic_uris, uri);
      coap_free(uri);
    }
  }

  HASH_DELETE(hh, test_resources, payload);
  coap_free(payload);
}

static void
my_put_test(coap_context_t  *ctx, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response) {
  coap_opt_iterator_t opt_iter;
  coap_opt_t *option;
  coap_payload_t *payload;
  size_t len;
  unsigned char *data;

  response->hdr->code = COAP_RESPONSE_CODE(204);

  coap_get_data(request, &len, &data);

  payload = coap_find_payload(resource->key);
  if (payload && payload->max_data < len) { /* need more storage */
    coap_delete_payload(payload);
    payload = NULL;
    /* bug: when subsequent coap_new_payload() fails, our old contents
       is gone */
  }

  if (!payload) {		/* create new payload */
    payload = coap_new_payload(len);
    if (!payload)
      goto error;

    coap_add_payload(resource->key, payload, NULL);
  }
  payload->length = len;
  memcpy(payload->data, data, len);

  option = coap_check_option(request, COAP_OPTION_CONTENT_TYPE, &opt_iter);
  if (option) {
    /* set media type given in request */
    payload->media_type =
      coap_decode_var_bytes(COAP_OPT_VALUE(option), COAP_OPT_LENGTH(option));
  } else {
    /* set default value */
    payload->media_type = COAP_MEDIATYPE_TEXT_PLAIN;
  }
  /* FIXME: need to change attribute ct of resource.
     To do so, we need dynamic management of the attribute value
  */

  return;
 error:
  warn("cannot modify resource\n");
  response->hdr->code = COAP_RESPONSE_CODE(500);
}

static void
my_get_test(coap_context_t  *ctx, struct coap_resource_t *resource,
		 coap_address_t *peer, coap_pdu_t *request, str *token,
		 coap_pdu_t *response) {
  coap_key_t etag;
  unsigned char buf[2];
  coap_payload_t *test_payload;
  coap_block_t block;

  test_payload = coap_find_payload(resource->key);
  if (!test_payload) {
    response->hdr->code = COAP_RESPONSE_CODE(500);

    return;
  }

  response->hdr->code = COAP_RESPONSE_CODE(205);

  coap_add_option(response, COAP_OPTION_CONTENT_TYPE,
	  coap_encode_var_bytes(buf, test_payload->media_type), buf);

  /* add etag for the resource */
  if (test_payload->flags & REQUIRE_ETAG) {
    memset(etag, 0, sizeof(etag));
    coap_hash(test_payload->data, test_payload->length, etag);
    coap_add_option(response, COAP_OPTION_ETAG, sizeof(etag), etag);
  }

  if (request) {
    int res;

    if (coap_get_block(request, COAP_OPTION_BLOCK2, &block)) {
      res = coap_write_block_opt(&block, COAP_OPTION_BLOCK2, response,
				 test_payload->length);

      switch (res) {
      case -2:			/* illegal block */
	response->hdr->code = COAP_RESPONSE_CODE(400);
	goto error;
      case -1:			/* should really not happen */
	assert(0);
	/* fall through if assert is a no-op */
      case -3:			/* cannot handle request */
	response->hdr->code = COAP_RESPONSE_CODE(500);
	goto error;
      default:			/* everything is good */
	;
      }

      coap_add_block(response, test_payload->length, test_payload->data,
		     block.num, block.szx);
    } else {
      if (!coap_add_data(response, test_payload->length, test_payload->data)) {
	/* set initial block size, will be lowered by
	 * coap_write_block_opt) automatically */
	block.szx = 6;
	coap_write_block_opt(&block, COAP_OPTION_BLOCK2, response,
			     test_payload->length);

	coap_add_block(response, test_payload->length, test_payload->data,
		       block.num, block.szx);
      }
    }
  } else {		      /* this is a notification, block is 0 */
    /* FIXME: need to store block size with subscription */
  }

  return;

 error:
  coap_add_data(response,
		strlen(coap_response_phrase(response->hdr->code)),
		(unsigned char *)coap_response_phrase(response->hdr->code));
}

/*********************************************************************/

static void
init_resources(coap_context_t *ctx) {
  coap_resource_t *r;
  coap_payload_t *test_payload;

  test_payload = coap_new_payload(200);
  if (!test_payload)
    coap_log(LOG_CRIT, "cannot allocate resource /temp");
  else {
    test_payload->length = 13;
    memcpy(test_payload->data, "put data here", test_payload->length);

    /* test_payload->media_type is 0 anyway */
    //resource = coap_resource_init(NULL, 0, 0);
    r = coap_resource_init((unsigned char *)"temp", 4, COAP_RESOURCE_FLAGS_NOTIFY_CON);
    if (r != NULL) {
      coap_register_handler(r, COAP_REQUEST_PUT, my_put_test);
      coap_register_handler(r, COAP_REQUEST_GET, my_get_test);

      coap_add_attr(r, (unsigned char *)"ct", 2, (unsigned char *)"0", 1, 0);
      coap_add_attr(r, (unsigned char *)"rt", 2, (unsigned char *)"temp", 4, 0);
      coap_add_attr(r, (unsigned char *)"if", 2, (unsigned char *)"core#b", 6, 0);
  #if 0
      coap_add_attr(r, (unsigned char *)"obs", 3, NULL, 0, 0);
  #endif
      coap_add_resource(ctx, r);
      coap_add_payload(r->key, test_payload, NULL);
    }
  }

#ifndef WITHOUT_ASYNC
  r = coap_resource_init((unsigned char *)"async", 5, 0);
  coap_register_handler(r, COAP_REQUEST_GET, hnd_get_async);

  coap_add_attr(r, (unsigned char *)"ct", 2, (unsigned char *)"0", 1, 0);
  coap_add_resource(ctx, r);
#endif /* WITHOUT_ASYNC */

}

static void
fill_keystore(coap_context_t *ctx) {
  static uint8_t key[] = "secretPSK";
  size_t key_len = sizeof( key ) - 1;
  coap_context_set_psk( ctx, "CoAP", key, key_len );
}

static void
usage( const char *program, const char *version) {
  const char *p;

  p = strrchr( program, '/' );
  if ( p )
    program = ++p;

  fprintf( stderr, "%s v%s -- a small CoAP implementation\n"
     "(c) 2010,2011,2015 Olaf Bergmann <bergmann@tzi.org>\n\n"
     "usage: %s [-A address] [-p port]\n\n"
     "\t-A address\tinterface address to bind to\n"
     "\t-g group\tjoin the given multicast group\n"
     "\t-p port\t\tlisten on specified port\n"
     "\t-v num\t\tverbosity level (default: 3)\n"
     "\t-l list\t\tFail to send some datagram specified by a comma separated list of number or number intervals(for debugging only)\n"
     "\t-l loss%%\t\tRandmoly fail to send datagrams with the specified probability(for debugging only)\n",
    program, version, program );
}

static coap_context_t *
get_context(const char *node, const char *port) {
  coap_context_t *ctx = NULL;
  int s;
  struct addrinfo hints;
  struct addrinfo *result, *rp;

  ctx = coap_new_context(NULL);
  if (!ctx) {
    return NULL;
  }

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Coap uses UDP */
  hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

  s = getaddrinfo(node, port, &hints, &result);
  if ( s != 0 ) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    return NULL;
  }

  /* iterate through results until success */
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    coap_address_t addr;
    coap_endpoint_t *endpoint;

    if (rp->ai_addrlen <= sizeof(addr.addr)) {
      coap_address_init(&addr);
      addr.size = rp->ai_addrlen;
      memcpy(&addr.addr, rp->ai_addr, rp->ai_addrlen);

      endpoint = coap_new_endpoint(ctx, &addr, COAP_PROTO_UDP);
      if (endpoint) {
        if (coap_dtls_is_supported()) {
          if (addr.addr.sa.sa_family == AF_INET) {
            addr.addr.sin.sin_port = htons(ntohs(addr.addr.sin.sin_port) + 1);
          } else if (addr.addr.sa.sa_family == AF_INET6) {
            addr.addr.sin6.sin6_port =
              htons(ntohs(addr.addr.sin6.sin6_port) + 1);
          } else {
            goto finish;
          }

          endpoint = coap_new_endpoint(ctx, &addr, COAP_PROTO_DTLS);
          if (!endpoint) {
            coap_log(LOG_CRIT, "cannot create DTLS endpoint\n");
          }
        }
        goto finish;
      } else {
        coap_log(LOG_CRIT, "cannot create endpoint\n");
        continue;
      }
    }
  }

  fprintf(stderr, "no context available for interface '%s'\n", node);

  finish:
  freeaddrinfo(result);
  return ctx;
}

static int
join(coap_context_t *ctx, char *group_name){
  struct ipv6_mreq mreq;
  struct addrinfo   *reslocal = NULL, *resmulti = NULL, hints, *ainfo;
  int result = -1;

  /* we have to resolve the link-local interface to get the interface id */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;

  result = getaddrinfo("::", NULL, &hints, &reslocal);
  if (result != 0) {
    fprintf(stderr, "join: cannot resolve link-local interface: %s\n",
            gai_strerror(result));
    goto finish;
  }

  /* get the first suitable interface identifier */
  for (ainfo = reslocal; ainfo != NULL; ainfo = ainfo->ai_next) {
    if (ainfo->ai_family == AF_INET6) {
      mreq.ipv6mr_interface =
                ((struct sockaddr_in6 *)ainfo->ai_addr)->sin6_scope_id;
      break;
    }
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;

  /* resolve the multicast group address */
  result = getaddrinfo(group_name, NULL, &hints, &resmulti);

  if (result != 0) {
    fprintf(stderr, "join: cannot resolve multicast address: %s\n",
            gai_strerror(result));
    goto finish;
  }

  for (ainfo = resmulti; ainfo != NULL; ainfo = ainfo->ai_next) {
    if (ainfo->ai_family == AF_INET6) {
      mreq.ipv6mr_multiaddr =
                ((struct sockaddr_in6 *)ainfo->ai_addr)->sin6_addr;
      break;
    }
  }

  if (ctx->endpoint) {
    result = setsockopt(ctx->endpoint->sock.fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char *)&mreq, sizeof(mreq));
    if ( result == COAP_SOCKET_ERROR ) {
      fprintf( stderr, "join: setsockopt: %s\n", coap_socket_strerror() );
    }
  } else {
    result = -1;
  }

 finish:
  freeaddrinfo(resmulti);
  freeaddrinfo(reslocal);

  return result;
}

int
main(int argc, char **argv) {
  coap_context_t  *ctx;
  char *group = NULL;
  coap_tick_t now;
  char addr_str[NI_MAXHOST] = "::";
  char port_str[NI_MAXSERV] = "5683";
  int opt;
  coap_log_t log_level = LOG_WARNING;
  unsigned wait_ms;


  while ((opt = getopt(argc, argv, "A:g:p:v:l:")) != -1) {
    switch (opt) {
    case 'A' :
      strncpy(addr_str, optarg, NI_MAXHOST-1);
      addr_str[NI_MAXHOST - 1] = '\0';
      break;
    case 'g' :
      group = optarg;
      break;
    case 'p' :
      strncpy(port_str, optarg, NI_MAXSERV-1);
      port_str[NI_MAXSERV - 1] = '\0';
      break;
    case 'v' :
      log_level = strtol(optarg, NULL, 10);
      break;
    case 'l':
      if (!coap_debug_set_packet_loss(optarg)) {
	usage(argv[0], LIBCOAP_PACKAGE_VERSION);
	exit(1);
      }
      break;
    default:
      usage( argv[0], LIBCOAP_PACKAGE_VERSION );
      exit( 1 );
    }
  }

  coap_startup();
  coap_dtls_set_log_level(log_level);
  coap_set_log_level(log_level);

  ctx = get_context(addr_str, port_str);
  if (!ctx)
    return -1;

  fill_keystore(ctx);
  init_resources(ctx);

  /* join multicast group if requested at command line */
  if (group)
    join(ctx, group);

  signal(SIGINT, handle_sigint);

  wait_ms = COAP_RESOURCE_CHECK_TIME * 1000;

  while ( !quit ) {
    int result = coap_run_once( ctx, wait_ms );
    if ( result < 0 ) {
      break;
    } else if ( (unsigned)result < wait_ms ) {
      wait_ms -= result;
    } else {
      //if ( time_resource ) {
	//time_resource->dirty = 1;
    //  }
      wait_ms = COAP_RESOURCE_CHECK_TIME * 1000;
    }

#ifndef WITHOUT_ASYNC
    /* check if we have to send asynchronous responses */
    coap_ticks( &now );
    check_async(ctx, now);
#endif /* WITHOUT_ASYNC */

#ifndef WITHOUT_OBSERVE
    /* check if we have to send observe notifications */
    coap_check_notify(ctx);
#endif /* WITHOUT_OBSERVE */
  }

  coap_free_context(ctx);
  coap_cleanup();

  return 0;
}
