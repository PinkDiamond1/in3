/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/blockchainsllc/in3
 *
 * Copyright (C) 2018-2019 slock.it GmbH, Blockchains LLC
 *
 *
 * COMMERCIAL LICENSE USAGE
 *
 * Licensees holding a valid commercial license may use this file in accordance
 * with the commercial license agreement provided with the Software or, alternatively,
 * in accordance with the terms contained in a written agreement between you and
 * slock.it GmbH/Blockchains LLC. For licensing terms and conditions or further
 * information please contact slock.it at in3@slock.it.
 *
 * Alternatively, this file may be used under the AGPL license as follows:
 *
 * AGPL LICENSE USAGE
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 * [Permissions of this strong copyleft license are conditioned on making available
 * complete source code of licensed works and modifications, which include larger
 * works using a licensed work, under the same license. Copyright and license notices
 * must be preserved. Contributors provide an express grant of patent rights.]
 * You should have received a copy of the GNU Affero General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *******************************************************************************/
#ifndef CONTEXT_INTERNAL_H
#define CONTEXT_INTERNAL_H

#include "context.h"
#include "plugin.h"

#ifdef LOGGING
#define ctx_set_error(c, msg, err) ctx_set_error_intern(c, msg, err)
#else
#define ctx_set_error(c, msg, err) ctx_set_error_intern(c, NULL, err)
#endif

/**
 * creates a request-object, which then need to be filled with the responses.
 *
 * each request object contains a array of reponse-objects. In order to set the response, you need to call
 *
 * ```c
 * // set a succesfull response
 * sb_add_chars(&request->results[0].result, my_response);
 * // set a error response
 * sb_add_chars(&request->results[0].error, my_error);
 * ```
 */
NONULL in3_request_t* in3_create_request(
    in3_req_t* ctx /**< [in] the request context. */
);

/**
 * frees a previuosly allocated request.
 */
NONULL void request_free(
    in3_request_t* req /**< [in] the request. */
);

/**
 * sets the error message in the context.
 *
 * If there is a previous error it will append it.
 * the return value will simply be passed so you can use it like
 *
 * ```c
 *   return ctx_set_error(ctx, "wrong number of arguments", IN3_EINVAL)
 * ```
 */
in3_ret_t ctx_set_error_intern(
    in3_req_t* c,        /**< [in] the current request context. */
    char*      msg,      /**< [in] the error message. (This string will be copied) */
    in3_ret_t  errnumber /**< [in] the error code to return */
);

/**
 * handles a failable context
 *
 * This context *MUST* be freed with ctx_free(ctx) after usage to release the resources.
*/
in3_ret_t ctx_handle_failable(
    in3_req_t* ctx /**< [in] the current request context. */
);

NONULL_FOR((1, 2, 3, 5))
in3_ret_t        ctx_send_sub_request(in3_req_t* parent, char* method, char* params, char* in3, d_token_t** result);
NONULL in3_ret_t ctx_require_signature(in3_req_t* ctx, d_signature_type_t type, bytes_t* sig, bytes_t raw_data, bytes_t from);
NONULL in3_ret_t in3_retry_same_node(in3_req_t* ctx);

#define assert_in3_ctx(ctx)                                                                    \
  assert(ctx);                                                                                 \
  assert_in3(ctx->client);                                                                     \
  assert(ctx->signers_length <= (ctx->type == RT_RPC ? ctx->client->signature_count + 1 : 0)); \
  assert(ctx->signers_length ? (ctx->signers != NULL) : (ctx->signers == NULL));               \
  assert(ctx->len >= 1 || ctx->error);                                                         \
  assert(ctx->attempt <= ctx->client->max_attempts);                                           \
  assert(!ctx->len || ctx->request_context);                                                   \
  assert(!ctx->len || ctx->requests);                                                          \
  assert(!ctx->len || ctx->requests[0]);                                                       \
  assert(!ctx->len || ctx->requests[ctx->len - 1]);                                            \
  assert(ctx->error ? (ctx->verification_state < 0) : (ctx->verification_state == IN3_OK || ctx->verification_state == IN3_WAITING));

#define assert_in3_response(r) \
  assert(r);                   \
  assert(r->state != IN3_OK || r->data.data);

NONULL void in3_ctx_free_nodes(node_match_t* c);
int         ctx_nodes_len(node_match_t* root);
NONULL bool ctx_is_method(const in3_req_t* ctx, const char* method);

#endif // CONTEXT_INTERNAL_H