/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#include "fw/src/sj_clubby_js.h"
#include "fw/src/sj_v7_ext.h"
#include "fw/src/sj_hal.h"
#include "fw/src/sj_common.h"

#if !defined(DISABLE_C_CLUBBY) && !defined(CS_DISABLE_JS)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

static const char s_clubby_prop[] = "_$clubby_prop$_";

static int register_js_callback(struct clubby *clubby, struct v7 *v7,
                                const char *id, int8_t id_len,
                                sj_clubby_callback_t cb, v7_val_t cbv,
                                uint32_t timeout);

static void set_clubby(struct v7 *v7, v7_val_t obj, struct clubby *clubby) {
  v7_def(v7, obj, s_clubby_prop, sizeof(s_clubby_prop),
         (V7_DESC_WRITABLE(0) | V7_DESC_CONFIGURABLE(0) | _V7_DESC_HIDDEN(1)),
         v7_mk_foreign(v7, clubby));
}

static struct clubby *get_clubby(struct v7 *v7, v7_val_t obj) {
  v7_val_t clubbyv = v7_get(v7, obj, s_clubby_prop, sizeof(s_clubby_prop));
  if (!v7_is_foreign(clubbyv)) {
    return 0;
  }

  return v7_get_ptr(v7, clubbyv);
}

clubby_handle_t sj_clubby_get_handle(struct v7 *v7, v7_val_t clubby_v) {
  return get_clubby(v7, clubby_v);
}

#define DECLARE_CLUBBY()                                   \
  struct clubby *clubby = get_clubby(v7, v7_get_this(v7)); \
  if (clubby == NULL) {                                    \
    LOG(LL_ERROR, ("Invalid call"));                       \
    *res = v7_mk_boolean(v7, 0);                           \
    return V7_OK;                                          \
  }

static ub_val_t obj_to_ubj(struct v7 *v7, struct ub_ctx *ctx, v7_val_t obj) {
  LOG(LL_VERBOSE_DEBUG, ("enter"));

  if (v7_is_number(obj)) {
    double n = v7_get_double(v7, obj);
    LOG(LL_VERBOSE_DEBUG, ("type=number val=%d", (int) n))
    return ub_create_number(n);
  } else if (v7_is_string(obj)) {
    const char *s = v7_get_cstring(v7, &obj);
    LOG(LL_VERBOSE_DEBUG, ("type=string val=%s", s))
    return ub_create_string(ctx, s);
  } else if (v7_is_array(v7, obj)) {
    int i, arr_len = v7_array_length(v7, obj);
    LOG(LL_VERBOSE_DEBUG, ("type=array len=%d", arr_len));
    ub_val_t ub_arr = ub_create_array(ctx);
    for (i = 0; i < arr_len; i++) {
      v7_val_t item = v7_array_get(v7, obj, i);
      ub_array_push(ctx, ub_arr, obj_to_ubj(v7, ctx, item));
    }
    return ub_arr;
  } else if (v7_is_object(obj)) {
    LOG(LL_VERBOSE_DEBUG, ("type=object"));
    ub_val_t ub_obj = ub_create_object(ctx);
    void *h = NULL;
    v7_val_t name, val;
    v7_prop_attr_t attrs;
    while ((h = v7_next_prop(h, obj, &name, &val, &attrs)) != NULL) {
      LOG(LL_VERBOSE_DEBUG, ("propname=%s", v7_get_cstring(v7, &name)));
      ub_add_prop(ctx, ub_obj, v7_get_cstring(v7, &name),
                  obj_to_ubj(v7, ctx, val));
    }
    return ub_obj;
  } else {
    char buf[100], *p;
    p = v7_stringify(v7, obj, buf, sizeof(buf), V7_STRINGIFY_JSON);
    LOG(LL_ERROR, ("Unknown type, val=%s", p));
    ub_val_t ret = ub_create_string(ctx, p);
    if (p != buf) {
      free(p);
    }
    return ret;
  }
}

static int register_js_callback(struct clubby *clubby, struct v7 *v7,
                                const char *id, int8_t id_len,
                                sj_clubby_callback_t cb, v7_val_t cbv,
                                uint32_t timeout) {
  v7_val_t *cb_ptr = malloc(sizeof(*cb_ptr));
  if (cb_ptr == NULL) {
    LOG(LL_ERROR, ("Out of memory"));
    return 0;
  }
  *cb_ptr = cbv;
  v7_own(v7, cb_ptr);
  return sj_clubby_register_callback(clubby, id, id_len, cb, cb_ptr, timeout);
}

/*
 * Sends resp for `evt.request`
 * Trying to reproduce handleCmd from clubby.js
 */

static void clubby_send_response(struct clubby *clubby, const char *dst,
                                 int64_t id, int status, const char *error_msg,
                                 v7_val_t result_v) {
  /*
   * Do not queueing responses. Work like clubby.js
   * TODO(alashkin): is it good?
   */
  ub_val_t result_ubj = CLUBBY_UNDEFINED;
  ub_val_t error_ubj = CLUBBY_UNDEFINED;

  struct ub_ctx *ctx = ub_ctx_new();
  if (!v7_is_undefined(result_v)) {
    result_ubj = obj_to_ubj(clubby->v7, ctx, result_v);
  }

  if (status != 0) {
    error_ubj = sj_clubby_create_error(ctx, status, error_msg);
  }

  clubby_proto_send(clubby->nc, ctx,
                    clubby_proto_create_resp(ctx, clubby->cfg.device_id,
                                             clubby->cfg.device_psk, dst, id,
                                             result_ubj, error_ubj));
}

/* TODO(alashkin): see hello cmd */
#if 0
static void clubby_hello_req_callback(struct clubby_event *evt,
                                      void *user_data) {
  struct clubby *clubby = (struct clubby *) evt->context;
  (void) user_data;

  LOG(LL_DEBUG,
      ("Incoming /v1/Hello received, id=%d", (int32_t) evt->request.id));
  char src[512] = {0};
  if ((size_t) evt->request.src->len > sizeof(src)) {
    LOG(LL_ERROR, ("src too long, len=%d", evt->request.src->len));
    return;
  }
  memcpy(src, evt->request.src->ptr, evt->request.src->len);

  char status_msg[100];
  snprintf(status_msg, sizeof(status_msg) - 1, "Hello, this is %s",
           clubby->cfg.device_id);

  clubby_send_response(clubby, src, evt->request.id, 0, status_msg,
                       V7_UNDEFINED);
}
#endif

static void simple_cb(struct clubby_event *evt, void *user_data) {
  struct clubby *clubby = (struct clubby *) evt->context;
  v7_val_t *cbp = (v7_val_t *) user_data;
  sj_invoke_cb0(clubby->v7, *cbp);
}

static void simple_cb_run_once(struct clubby_event *evt, void *user_data) {
  struct clubby *clubby = (struct clubby *) evt->context;
  v7_val_t *cbp = (v7_val_t *) user_data;
  sj_invoke_cb0(clubby->v7, *cbp);
  v7_disown(clubby->v7, cbp);
  free(cbp);
}

static enum v7_err set_on_event(const char *eventid, int8_t eventid_len,
                                struct v7 *v7, v7_val_t *res) {
  DECLARE_CLUBBY();

  v7_val_t cbv = v7_arg(v7, 0);

  if (!v7_is_callable(v7, cbv)) {
    printf("Invalid arguments\n");
    goto error;
  }

  if (!register_js_callback(clubby, v7, eventid, eventid_len, simple_cb, cbv,
                            0)) {
    goto error;
  }

  *res = v7_mk_boolean(v7, 1);
  return V7_OK;

error:
  *res = v7_mk_boolean(v7, 0);
  return V7_OK;
}

/*
 * TODO(alashkin): move this declaration to common sjs header
 * or recompile newlib with int64_t support in sprintf
 */
int c_snprintf(char *buf, size_t buf_size, const char *fmt, ...);

static void clubby_resp_cb(struct clubby_event *evt, void *user_data) {
  struct clubby *clubby = (struct clubby *) evt->context;
  v7_val_t *cbp = (v7_val_t *) user_data;
  v7_val_t resp_obj = V7_UNDEFINED;
  enum v7_err res = V7_OK;

  if (v7_is_undefined(*cbp)) {
    LOG(LL_DEBUG, ("Callback is not set for id=%d", (int) evt->id));
    goto clean;
  }

  if (evt->ev == CLUBBY_TIMEOUT) {
    /*
     * if event is CLUBBY_TIMEOUT we don't have actual answer
     * and we need to compose it
     */
    resp_obj = v7_mk_object(clubby->v7);
    v7_set(
        clubby->v7, resp_obj, "message", ~0,
        v7_mk_string(clubby->v7, "Delivery deadline reached (local)", ~0, 1));
    /* clubby server responses with error 504 on timeout */
    v7_set(clubby->v7, resp_obj, "code", ~0, v7_mk_number(clubby->v7, 504));
  } else if (evt->response.result != NULL ||
             evt->response.error.error_obj != NULL) {
    struct json_token *cb_param_tok = evt->response.result
                                          ? evt->response.result
                                          : evt->response.error.error_obj;
    /* v7_parse_json wants null terminated string */
    if (cb_param_tok->type == JSON_TYPE_OBJECT) {
      char *obj_str = calloc(1, cb_param_tok->len + 1);
      if (obj_str == NULL) {
        LOG(LL_ERROR, ("Out of memory"));
        goto clean;
      }
      memcpy(obj_str, cb_param_tok->ptr, cb_param_tok->len);

      res = v7_parse_json(clubby->v7, obj_str, &resp_obj);
      free(obj_str);
    } else {
      resp_obj =
          v7_mk_string(clubby->v7, cb_param_tok->ptr, cb_param_tok->len, 1);
    }
  }

  if (res != V7_OK) {
    /*
     * TODO(alashkin): do we need to report in case of malformed
     * answer of just skip it?
     */
    LOG(LL_ERROR, ("Unable to parse reply"));
    resp_obj = V7_UNDEFINED;
  }

  v7_own(clubby->v7, &resp_obj);
  v7_val_t cb_param = v7_mk_object(clubby->v7);
  v7_own(clubby->v7, &cb_param);
  if (!v7_is_undefined(resp_obj)) {
    v7_set(clubby->v7, cb_param, evt->response.result ? "result" : "error", ~0,
           resp_obj);
  }
  sj_invoke_cb1(clubby->v7, *cbp, cb_param);
  v7_disown(clubby->v7, &resp_obj);
  v7_disown(clubby->v7, &cb_param);
clean:
  v7_disown(clubby->v7, cbp);
  free(cbp);
}

struct done_func_context {
  char *dst;
  int64_t id;
  struct clubby *clubby;
};

static enum v7_err done_func(struct v7 *v7, v7_val_t *res) {
  v7_val_t me = v7_get_this(v7);
  if (!v7_is_foreign(me)) {
    LOG(LL_ERROR, ("Internal error"));
    *res = v7_mk_boolean(v7, 0);
    return V7_OK;
  }

  struct done_func_context *ctx = v7_get_ptr(v7, me);
  v7_val_t cb_res = v7_arg(v7, 0);
  v7_val_t cb_err = v7_arg(v7, 1);

  if (!v7_is_undefined(cb_err)) {
    clubby_send_response(ctx->clubby, ctx->dst, ctx->id, 1,
                         v7_get_cstring(v7, &cb_err), V7_UNDEFINED);
  } else {
    clubby_send_response(ctx->clubby, ctx->dst, ctx->id, 0, NULL, cb_res);
  }
  *res = v7_mk_boolean(v7, 1);

  free(ctx->dst);
  free(ctx);

  return V7_OK;
}

static void clubby_req_cb(struct clubby_event *evt, void *user_data) {
  struct clubby *clubby = (struct clubby *) evt->context;
  v7_val_t *cbv = (v7_val_t *) user_data;
  v7_val_t args_v = V7_UNDEFINED, clubby_param;
  enum v7_err res = V7_OK;

  if (evt->request.args != NULL) {
    /* v7_parse_json wants null terminated string */
    char *obj_str = calloc(1, evt->request.args->len + 1);
    if (obj_str == NULL) {
      LOG(LL_ERROR, ("Out of memory"));
      return;
    }
    memcpy(obj_str, evt->request.args->ptr, evt->request.args->len);

    res = v7_parse_json(clubby->v7, obj_str, &args_v);
    free(obj_str);
  }

  if (res != V7_OK) {
    LOG(LL_ERROR, ("Failed to parse arguments"));
    return;
  }

  v7_val_t argcv = v7_get(clubby->v7, *cbv, "length", ~0);
  /* Must be verified before */
  assert(!v7_is_undefined(argcv));
  int argc = v7_get_double(clubby->v7, argcv);

  char *dst = calloc(1, evt->src->len + 1);
  if (dst == NULL) {
    LOG(LL_ERROR, ("Out of memory"));
    return;
  }

  memcpy(dst, evt->src->ptr, evt->src->len);

  if (argc < 2) {
    /*
     * Callback with one (or none) parameter
     * Ex: function(req) { print("nice request"); return "response"}
     */
    enum v7_err cb_res;
    v7_val_t args;
    v7_val_t res;

    v7_own(clubby->v7, &args_v);
    clubby_param = v7_mk_object(clubby->v7);
    v7_own(clubby->v7, &clubby_param);
    v7_set(clubby->v7, clubby_param, "args", ~0, args_v);
    args = v7_mk_array(clubby->v7);
    v7_array_push(clubby->v7, args, clubby_param);
    cb_res = v7_apply(clubby->v7, *cbv, v7_get_global(clubby->v7), args, &res);
    if (cb_res == V7_OK) {
      clubby_send_response(clubby, dst, evt->id, 0, NULL, res);
    } else {
      clubby_send_response(clubby, dst, evt->id, 1,
                           v7_get_cstring(clubby->v7, &res), V7_UNDEFINED);
    }
    v7_disown(clubby->v7, &clubby_param);
    v7_disown(clubby->v7, &args_v);
    free(dst);
    return;
  } else {
    /*
     * Callback with two (or more) parameters:
     * Ex:
     *    function(req, done) { print("let me think...");
     *        setTimeout(function() { print("yeah, nice request");
     *                                done("response")}, 1000); }
     */

    enum v7_err res;
    struct done_func_context *ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
      LOG(LL_ERROR, ("Out of memory"));
      return;
    }
    ctx->dst = dst;
    ctx->id = evt->id;
    ctx->clubby = clubby;

    v7_own(clubby->v7, &args_v);
    clubby_param = v7_mk_object(clubby->v7);
    v7_own(clubby->v7, &clubby_param);
    v7_set(clubby->v7, clubby_param, "args", ~0, args_v);

    v7_val_t done_func_v = v7_mk_cfunction(done_func);
    v7_val_t bind_args = v7_mk_array(clubby->v7);
    v7_val_t donevb;

    v7_array_push(clubby->v7, bind_args, v7_mk_foreign(clubby->v7, ctx));
    res = v7_apply(clubby->v7, v7_get(clubby->v7, done_func_v, "bind", ~0),
                   done_func_v, bind_args, &donevb);
    v7_own(clubby->v7, &donevb);

    if (res != V7_OK) {
      LOG(LL_ERROR, ("Bind invocation error"));
      goto cleanup;
    }

    v7_val_t cb_args = v7_mk_array(clubby->v7);
    v7_array_push(clubby->v7, cb_args, clubby_param);
    v7_array_push(clubby->v7, cb_args, donevb);

    v7_val_t cb_res;
    res =
        v7_apply(clubby->v7, *cbv, v7_get_global(clubby->v7), cb_args, &cb_res);

    if (res != V7_OK) {
      LOG(LL_ERROR, ("Callback invocation error"));
      goto cleanup;
    }

    v7_disown(clubby->v7, &donevb);
    v7_disown(clubby->v7, &clubby_param);
    v7_disown(clubby->v7, &args_v);
    return;

  cleanup:
    v7_disown(clubby->v7, &donevb);
    v7_disown(clubby->v7, &clubby_param);
    v7_disown(clubby->v7, &args_v);
    free(ctx->dst);
    free(ctx);
  }
}

SJ_PRIVATE enum v7_err Clubby_sayHello(struct v7 *v7, v7_val_t *res) {
  DECLARE_CLUBBY();

  sj_clubby_send_hello(clubby);
  *res = v7_mk_boolean(v7, 1);

  return V7_OK;
}

/* clubby.call(dst: string, cmd: object, callback(resp): function) */
SJ_PRIVATE enum v7_err Clubby_call(struct v7 *v7, v7_val_t *res) {
  DECLARE_CLUBBY();

  struct ub_ctx *ctx = NULL;
  v7_val_t dst_v = v7_arg(v7, 0);
  v7_val_t request_v = v7_arg(v7, 1);
  v7_val_t cb_v = v7_arg(v7, 2);

  if ((!v7_is_null(dst_v) && !v7_is_string(dst_v) && !v7_is_undefined(dst_v)) ||
      !v7_is_object(request_v) ||
      (!v7_is_undefined(cb_v) && !v7_is_callable(v7, cb_v))) {
    printf("Invalid arguments\n");
    goto error;
  }

#ifndef CLUBBY_DISABLE_MEMORY_LIMIT
  if (!sj_clubby_is_connected(clubby) && get_cfg()->clubby.memory_limit != 0 &&
      sj_get_free_heap_size() < (size_t) get_cfg()->clubby.memory_limit) {
    return v7_throwf(v7, "Error", "Not enough memory to enqueue packet");
  }
#endif

  if (sj_clubby_is_overcrowded(clubby)) {
    return v7_throwf(v7, "Error", "Too many unanswered packets, try later");
  }

  /* Check if id and timeout exists and put default if not */
  v7_val_t id_v = v7_get(v7, request_v, "id", 2);
  int64_t id;

  if (!v7_is_number(id_v)) {
    id = clubby_proto_get_new_id();
    v7_set(v7, request_v, "id", ~0, v7_mk_number(v7, id));
  } else {
    id = v7_get_double(v7, id_v);
  }

  v7_val_t timeout_v = v7_get(v7, request_v, "timeout", 7);
  uint32_t timeout = 0;
  if (v7_is_number(timeout_v)) {
    timeout = v7_get_double(v7, timeout_v);
  } else {
    timeout = clubby->cfg.request_timeout;
  }

  v7_set(v7, request_v, "timeout", 7, v7_mk_number(v7, timeout));

  /*
   * NOTE: Propably, we don't need UBJSON it is flower's legacy
   * TODO(alashkin): think about replacing ubjserializer with frozen
   */
  ctx = ub_ctx_new();

  /*
   * TODO(alashkin): do not register callback is cbv is undefined
   * Now it is required to track timeout
   */
  if (!register_js_callback(clubby, v7, (char *) &id, sizeof(id),
                            clubby_resp_cb, cb_v, timeout)) {
    goto error;
  }

  sj_clubby_send_request(clubby, ctx, id, v7_get_cstring(v7, &dst_v),
                         obj_to_ubj(v7, ctx, request_v));
  *res = v7_mk_boolean(v7, 1);

  return V7_OK;

error:
  if (ctx != NULL) {
    ub_ctx_free(ctx);
  }
  *res = v7_mk_boolean(v7, 0);
  return V7_OK;
}

SJ_PRIVATE enum v7_err Clubby_oncmd(struct v7 *v7, v7_val_t *res) {
  DECLARE_CLUBBY();

  v7_val_t arg1 = v7_arg(v7, 0);
  v7_val_t arg2 = v7_arg(v7, 1);

  if (v7_is_callable(v7, arg1) && v7_is_undefined(arg2)) {
    /*
     * oncmd is called with one arg, and this arg is function -
     * setup global `oncmd` handler
     */
    if (!register_js_callback(clubby, v7, s_oncmd_cmd, sizeof(S_ONCMD_CMD),
                              simple_cb_run_once, arg1, 0)) {
      goto error;
    }
  } else if (v7_is_string(arg1) && v7_is_callable(v7, arg2)) {
    /*
     * oncmd is called with two args - string and function
     * setup handler for specific command
     */
    const char *cmd_str = v7_get_cstring(v7, &arg1);
    if (!register_js_callback(clubby, v7, cmd_str, strlen(cmd_str),
                              clubby_req_cb, arg2, 0)) {
      goto error;
    }
  } else {
    printf("Invalid arguments");
    goto error;
  }

  *res = v7_mk_boolean(v7, 1);
  return V7_OK;

error:
  *res = v7_mk_boolean(v7, 0);
  return V7_OK;
}

SJ_PRIVATE enum v7_err Clubby_onclose(struct v7 *v7, v7_val_t *res) {
  return set_on_event(clubby_cmd_onclose, sizeof(CLUBBY_CMD_ONCLOSE), v7, res);
}

SJ_PRIVATE enum v7_err Clubby_onopen(struct v7 *v7, v7_val_t *res) {
  return set_on_event(clubby_cmd_onopen, sizeof(CLUBBY_CMD_ONOPEN), v7, res);
}

SJ_PRIVATE enum v7_err Clubby_connect(struct v7 *v7, v7_val_t *res) {
  DECLARE_CLUBBY();

  if (!clubby_proto_is_connected(clubby->nc)) {
    sj_reset_reconnect_timeout(clubby);
    sj_clubby_connect(clubby);

    *res = v7_mk_boolean(v7, 1);
  } else {
    LOG(LL_WARN, ("Clubby is already connected"));
    *res = v7_mk_boolean(v7, 0);
  }
  return V7_OK;
}

SJ_PRIVATE enum v7_err Clubby_ready(struct v7 *v7, v7_val_t *res) {
  DECLARE_CLUBBY();

  v7_val_t cbv = v7_arg(v7, 0);

  if (!v7_is_callable(v7, cbv)) {
    printf("Invalid arguments\n");
    goto error;
  }

  if (sj_clubby_is_connected(clubby)) {
    v7_own(v7, &cbv);
    sj_invoke_cb0(v7, cbv);
    v7_disown(v7, &cbv);
  } else {
    if (!register_js_callback(clubby, v7, clubby_cmd_ready,
                              sizeof(CLUBBY_CMD_READY), simple_cb_run_once, cbv,
                              0)) {
      goto error;
    }
  }

  *res = v7_mk_boolean(v7, 1);
  return V7_OK;

error:
  *res = v7_mk_boolean(v7, 0);
  return V7_OK;
}

#define GET_INT_PARAM(name1, name2)                                     \
  {                                                                     \
    v7_val_t tmp = v7_get(v7, arg, #name2, ~0);                         \
    if (v7_is_undefined(tmp)) {                                         \
      clubby->cfg.name1 = get_cfg()->clubby.name1;                      \
    } else if (v7_is_number(tmp)) {                                     \
      clubby->cfg.name1 = v7_get_double(v7, tmp);                       \
    } else {                                                            \
      sj_free_clubby(clubby);                                           \
      LOG(LL_ERROR, ("Invalid type for %s, expected number", #name2));  \
      return v7_throwf(v7, "TypeError",                                 \
                       "Invalid type for %s, expected number", #name2); \
    }                                                                   \
  }

#define GET_STR_PARAM(name1, name2)                                     \
  {                                                                     \
    v7_val_t tmp = v7_get(v7, arg, #name2, ~0);                         \
    if (v7_is_undefined(tmp)) {                                         \
      if (get_cfg()->clubby.name1 != NULL) {                            \
        clubby->cfg.name1 = strdup(get_cfg()->clubby.name1);            \
        if (clubby->cfg.name1 == NULL) {                                \
          LOG(LL_ERROR, ("Out of memory"));                             \
          rcode = v7_throwf(v7, "Error", "Out of memory");              \
          goto clean;                                                   \
        }                                                               \
      } else {                                                          \
        clubby->cfg.name1 = "";                                         \
      }                                                                 \
    } else if (v7_is_string(tmp)) {                                     \
      clubby->cfg.name1 = strdup(v7_get_cstring(v7, &tmp));             \
      if (clubby->cfg.name1 == NULL) {                                  \
        LOG(LL_ERROR, ("Out of memory"));                               \
        rcode = v7_throwf(v7, "Error", "Out of memory");                \
        goto clean;                                                     \
      }                                                                 \
    } else {                                                            \
      sj_free_clubby(clubby);                                           \
      LOG(LL_ERROR, ("Invalid type for %s, expected string", #name2));  \
      return v7_throwf(v7, "TypeError",                                 \
                       "Invalid type for %s, expected string", #name2); \
    }                                                                   \
  }

#define GET_CB_PARAM(name1, name2)                                           \
  {                                                                          \
    v7_val_t tmp = v7_get(v7, arg, #name1, ~0);                              \
    if (v7_is_callable(v7, tmp)) {                                           \
      register_js_callback(clubby, v7, name2, sizeof(name2), simple_cb, tmp, \
                           0);                                               \
    } else if (!v7_is_undefined(tmp)) {                                      \
      sj_free_clubby(clubby);                                                \
      LOG(LL_ERROR, ("Invalid type for %s, expected function", #name1));     \
      return v7_throwf(v7, "TypeError",                                      \
                       "Invalid type for %s, expected function", #name1);    \
    }                                                                        \
  }

SJ_PRIVATE enum v7_err Clubby_ctor(struct v7 *v7, v7_val_t *res) {
  (void) res;
  enum v7_err rcode = V7_OK;
  v7_val_t arg = v7_arg(v7, 0);

  if (!v7_is_undefined(arg) && !v7_is_object(arg)) {
    LOG(LL_ERROR, ("Invalid arguments"));
    return v7_throwf(v7, "TypeError", "Invalid arguments");
  }

  v7_val_t this_obj = v7_get_this(v7);
  struct clubby *clubby = sj_create_clubby(v7);
  if (clubby == NULL) {
    LOG(LL_ERROR, ("Out of memory"));
    return v7_throwf(v7, "Error", "Out of memory");
  }

  GET_INT_PARAM(reconnect_timeout_min, reconnect_timeout_min);
  GET_INT_PARAM(reconnect_timeout_max, reconnect_timeout_max);
  GET_INT_PARAM(request_timeout, timeout);
  GET_INT_PARAM(max_queue_size, max_queue_size);
  GET_STR_PARAM(device_id, device_id);
  GET_STR_PARAM(device_psk, device_psk);
  GET_STR_PARAM(server_address, server_address);
  GET_CB_PARAM(onopen, CLUBBY_CMD_ONOPEN);
  GET_CB_PARAM(onclose, CLUBBY_CMD_ONCLOSE);
  GET_CB_PARAM(oncmd, S_ONCMD_CMD);
  GET_STR_PARAM(ssl_server_name, ssl_server_name);
  GET_STR_PARAM(ssl_ca_file, ssl_ca_file);
  GET_STR_PARAM(ssl_client_cert_file, ssl_client_cert_file);

  set_clubby(v7, this_obj, clubby);
  v7_val_t connect = v7_get(v7, arg, "connect", ~0);
  if (v7_is_undefined(connect) || v7_is_truthy(v7, connect)) {
    sj_reset_reconnect_timeout(clubby);
    sj_clubby_connect(clubby);
  }

  return rcode;

clean:
  sj_free_clubby(clubby);
  return rcode;
}

void sj_clubby_send_reply(struct clubby_event *evt, int status,
                          const char *error_msg, v7_val_t result_v) {
  if (evt == NULL) {
    LOG(LL_WARN, ("Unable to send clubby reply"));
    return;
  }
  struct clubby *clubby = (struct clubby *) evt->context;

  /* TODO(alashkin): add `len` parameter to ubjserializer */
  char *dst = calloc(1, evt->dst->len + 1);
  if (dst == NULL) {
    LOG(LL_ERROR, ("Out of memory"));
    return;
  }
  memcpy(dst, evt->dst->ptr, evt->dst->len);

  clubby_send_response(clubby, dst, evt->id, status, error_msg, result_v);
  free(dst);
}

void sj_clubby_api_setup(struct v7 *v7) {
  v7_val_t clubby_proto_v, clubby_ctor_v;

  clubby_proto_v = v7_mk_object(v7);
  v7_own(v7, &clubby_proto_v);

  v7_set_method(v7, clubby_proto_v, "sayHello", Clubby_sayHello);
  v7_set_method(v7, clubby_proto_v, "call", Clubby_call);
  v7_set_method(v7, clubby_proto_v, "oncmd", Clubby_oncmd);
  v7_set_method(v7, clubby_proto_v, "ready", Clubby_ready);
  v7_set_method(v7, clubby_proto_v, "onopen", Clubby_onopen);
  v7_set_method(v7, clubby_proto_v, "onclose", Clubby_onclose);
  v7_set_method(v7, clubby_proto_v, "connect", Clubby_connect);

  clubby_ctor_v = v7_mk_function_with_proto(v7, Clubby_ctor, clubby_proto_v);

  v7_set(v7, v7_get_global(v7), "Clubby", ~0, clubby_ctor_v);

  v7_disown(v7, &clubby_proto_v);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !defined(DISABLE_C_CLUBBY) && !defined(CS_DISABLE_JS) */
