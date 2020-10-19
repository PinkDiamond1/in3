/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/slockit/in3-c
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
#include "zksync.h"
#include "../../core/client/context_internal.h"
#include "../../core/client/keys.h"
#include "../../core/client/plugin.h"
#include "../../core/util/debug.h"
#include "../../core/util/mem.h"
#include "../../third-party/zkcrypto/lib.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static void set_quoted_address(char* c, uint8_t* address) {
  bytes_to_hex(address, 20, c + 3);
  c[0] = c[43] = '"';
  c[1]         = '0';
  c[2]         = 'x';
  c[44]        = 0;
}
static in3_ret_t send_provider_request(in3_ctx_t* parent, zksync_config_t* conf, char* method, char* params, d_token_t** result) {
  if (params == NULL) params = "";
  char* in3 = conf ? alloca(strlen(conf->provider_url) + 26) : NULL;
  if (in3) sprintf(in3, "{\"rpc\":\"%s\"}", conf->provider_url);
  return ctx_send_sub_request(parent, method, params, in3, result);
}

static in3_ret_t zksync_get_account(zksync_config_t* conf, in3_ctx_t* ctx, uint8_t** account) {
  if (!conf->account) {
    in3_sign_account_ctx_t sctx = {.ctx = ctx, .accounts = NULL, .accounts_len = 0};
    if (in3_plugin_execute_first(ctx, PLGN_ACT_SIGN_ACCOUNT, &sctx) || !sctx.accounts_len) {
      if (sctx.accounts) _free(sctx.accounts);
      return ctx_set_error(ctx, "No account configured or signer set", IN3_ECONFIG);
    }
    conf->account = (uint8_t*) sctx.accounts;
  }

  if (account) *account = conf->account;
  return IN3_OK;
}

static in3_ret_t zksync_update_account(zksync_config_t* conf, in3_ctx_t* ctx) {
  uint8_t* account;
  TRY(zksync_get_account(conf, ctx, &account))
  d_token_t* result;
  char       adr[45];
  set_quoted_address(adr, account);
  TRY(send_provider_request(ctx, conf, "account_info", adr, &result))
  d_token_t* committed = d_get(result, key("committed"));
  conf->account_id     = d_get_intk(result, K_ID);
  conf->nonce          = d_get_longk(committed, K_NONCE);
  char* kh             = d_get_stringk(committed, key("pubKeyHash"));
  if (kh && strlen(kh) == 45)
    hex_to_bytes(kh + 5, 40, conf->pub_key_hash, 20);

  // clean up
  //  ctx_remove_required(ctx, ctx_find_required(ctx, "account_info"), false);
  return IN3_OK;
}

static in3_ret_t zksync_get_account_id(zksync_config_t* conf, in3_ctx_t* ctx, uint32_t* account_id) {
  uint8_t* account    = NULL;
  char*    cache_name = NULL;
  TRY(zksync_get_account(conf, ctx, &account))

  if (in3_plugin_is_registered(ctx->client, PLGN_ACT_CACHE) && account) {
    cache_name = alloca(60);
    strcpy(cache_name, "zksync_ac_");
    bytes_to_hex(account, 20, cache_name + 9);
    in3_cache_ctx_t cctx = {.ctx = ctx, .key = cache_name, .content = NULL};
    TRY(in3_plugin_execute_first_or_none(ctx, PLGN_ACT_CACHE_GET, &cctx))
    if (cctx.content) {
      conf->account_id = bytes_to_int(cctx.content->data, 4);
      b_free(cctx.content);
    }
  }

  if (!conf->account_id) TRY(zksync_update_account(conf, ctx))
  if (!conf->account_id) return ctx_set_error(ctx, "This user has no account yet!", IN3_EFIND);
  if (account_id) *account_id = conf->account_id;

  // add to cache
  if (cache_name) {
    uint8_t data[4];
    bytes_t content = bytes(data, 4);
    int_to_bytes(conf->account_id, data);
    in3_cache_ctx_t cctx = {.ctx = ctx, .key = cache_name, .content = &content};
    TRY(in3_plugin_execute_first_or_none(ctx, PLGN_ACT_CACHE_SET, &cctx))
  }

  return IN3_OK;
}

static in3_ret_t zksync_get_sync_key(zksync_config_t* conf, in3_ctx_t* ctx, uint8_t* sync_key) {
  if (!conf) return IN3_EUNKNOWN;
  if (!memiszero(conf->sync_key, 32)) {
    memcpy(sync_key, conf->sync_key, 32);
    return IN3_OK;
  }
  uint8_t* account = NULL;
  bytes_t  signature;
  char*    message = "\x19"
                  "Ethereum Signed Message:\n68"
                  "Access zkSync account.\n\nOnly sign this message for a trusted client!";
  TRY(zksync_get_account(conf, ctx, &account))
  assert(account);
  TRY(ctx_require_signature(ctx, SIGN_EC_HASH, &signature, bytes((uint8_t*) message, strlen(message)), bytes(account, 20)))
  if (signature.len == 65)
    signature.data[64] += 27;
  zkcrypto_pk_from_seed(signature, conf->sync_key);
  memcpy(sync_key, conf->sync_key, 32);
  return IN3_OK;
}

static in3_ret_t zksync_get_contracts(zksync_config_t* conf, in3_ctx_t* ctx, uint8_t** main) {

  char* cache_name = NULL;
  if (!conf->main_contract) {
    // check cache first
    if (in3_plugin_is_registered(ctx->client, PLGN_ACT_CACHE)) {
      cache_name = alloca(100);
      sprintf(cache_name, "zksync_contracts_%x", key(conf->provider_url));
      in3_cache_ctx_t cctx = {.ctx = ctx, .key = cache_name, .content = NULL};
      TRY(in3_plugin_execute_first_or_none(ctx, PLGN_ACT_CACHE_GET, &cctx))
      if (cctx.content) {
        conf->main_contract = _malloc(20);
        conf->gov_contract  = _malloc(20);
        memcpy(conf->main_contract, cctx.content->data, 20);
        memcpy(conf->gov_contract, cctx.content->data + 20, 20);
        b_free(cctx.content);
      }
    }
  }

  if (!conf->main_contract) {
    d_token_t* result;
    TRY(send_provider_request(ctx, conf, "contract_address", "", &result))
    bytes_t* main_contract = d_get_bytesk(result, key("mainContract"));
    if (!main_contract || main_contract->len != 20) return ctx_set_error(ctx, "could not get the main_contract from provider", IN3_ERPC);
    memcpy(conf->main_contract = _malloc(20), main_contract->data, 20);

    bytes_t* gov_contract = d_get_bytesk(result, key("govContract"));
    if (!gov_contract || gov_contract->len != 20) return ctx_set_error(ctx, "could not get the gov_contract from provider", IN3_ERPC);
    memcpy(conf->gov_contract = _malloc(20), gov_contract->data, 20);

    if (cache_name) {
      uint8_t data[40];
      bytes_t content = bytes(data, 40);
      memcpy(data, main_contract->data, 20);
      memcpy(data + 20, gov_contract->data, 20);
      in3_cache_ctx_t cctx = {.ctx = ctx, .key = cache_name, .content = &content};
      TRY(in3_plugin_execute_first_or_none(ctx, PLGN_ACT_CACHE_SET, &cctx))
    }

    // clean up
    ctx_remove_required(ctx, ctx_find_required(ctx, "contract_address"), false);
  }

  if (main) *main = conf->main_contract;
  return IN3_OK;
}

static in3_ret_t zksync_get_nonce(zksync_config_t* conf, in3_ctx_t* ctx, d_token_t* nonce_in, uint32_t* nonce) {
  if (nonce_in && (d_type(nonce_in) == T_INTEGER || d_type(nonce_in) == T_BYTES)) {
    *nonce = d_long(nonce_in);
    return IN3_OK;
  }
  TRY(zksync_update_account(conf, ctx))
  *nonce = conf->nonce;
  return IN3_OK;
}

static in3_ret_t zksync_get_fee(zksync_config_t* conf, in3_ctx_t* ctx, d_token_t* fee_in, bytes_t to, d_token_t* token, char* type,
#ifdef ZKSYNC_256
                                uint8_t* fee
#else
                                uint64_t* fee
#endif
) {
  if (fee_in && (d_type(fee_in) == T_INTEGER || d_type(fee_in) == T_BYTES)) {
#ifdef ZKSYNC_256
    bytes_t b = d_to_bytes(fee_in);
    memcpy(fee + 32 - b.len, b.data, b.len);
#else
    *fee = d_long(fee_in);
#endif
    return IN3_OK;
  }
  d_token_t* result;
  int        ss = strlen(type) + 4 + 50 * 2;
  sb_t       sb = {.allocted = ss, .data = alloca(ss), .len = 0};
  sb_add_char(&sb, '"');
  sb_add_chars(&sb, type);
  sb_add_bytes(&sb, "\",", &to, 1, false);
  sb_add_char(&sb, ',');
  switch (d_type(token)) {
    case T_BYTES:
      sb_add_bytes(&sb, ",", d_bytes(token), 1, false);
      break;
    case T_STRING: {
      sb_add_char(&sb, '"');
      sb_add_chars(&sb, d_string(token));
      sb_add_char(&sb, '"');
      break;
    }
    default:
      return ctx_set_error(ctx, "invalid token-value", IN3_EINVAL);
  }
  TRY(send_provider_request(ctx, conf, "get_tx_fee", sb.data, &result))
#ifdef ZKSYNC_256
  memset(fee, 0, 32);
  long_to_bytes(d_get_longk(result, key("totalFee")), fee + 24);
#else
  *fee = d_get_longk(result, key("totalFee"));
#endif
  return IN3_OK;
}

in3_ret_t resolve_tokens(zksync_config_t* conf, in3_ctx_t* ctx, d_token_t* token_src, zksync_token_t** token_dst) {
  char* cache_name = NULL;
  if (!conf->token_len) {
    // check cache first
    if (in3_plugin_is_registered(ctx->client, PLGN_ACT_CACHE)) {
      cache_name = alloca(100);
      sprintf(cache_name, "zksync_tokens_%x", key(conf->provider_url));
      in3_cache_ctx_t cctx = {.ctx = ctx, .key = cache_name, .content = NULL};
      TRY(in3_plugin_execute_first_or_none(ctx, PLGN_ACT_CACHE_GET, &cctx))
      if (cctx.content) {
        conf->token_len = cctx.content->len / sizeof(zksync_token_t);
        conf->tokens    = (void*) cctx.content->data;
        _free(cctx.content);
      }
    }
  }

  // still no tokenlist?
  if (!conf->token_len) {
    d_token_t* result;
    TRY(send_provider_request(ctx, conf, "tokens", "", &result))
    conf->token_len = d_len(result);
    conf->tokens    = _calloc(conf->token_len, sizeof(zksync_token_t));
    int i           = 0;
    for (d_iterator_t it = d_iter(result); it.left; d_iter_next(&it), i++) {
      conf->tokens[i].id       = d_get_intk(it.token, K_ID);
      conf->tokens[i].decimals = d_get_intk(it.token, key("decimals"));
      char* name               = d_get_stringk(it.token, key("symbol"));
      if (!name || strlen(name) > 7) return ctx_set_error(ctx, "invalid token name", IN3_EINVAL);
      strcpy(conf->tokens[i].symbol, name);
      bytes_t* adr = d_get_bytesk(it.token, K_ADDRESS);
      if (!adr || !adr->data || adr->len != 20) return ctx_set_error(ctx, "invalid token addr", IN3_EINVAL);
      memcpy(conf->tokens[i].address, adr->data, 20);
    }

    // clean up
    ctx_remove_required(ctx, ctx_find_required(ctx, "tokens"), false);

    if (cache_name) {
      bytes_t         data = bytes((void*) conf->tokens, conf->token_len * sizeof(zksync_token_t));
      in3_cache_ctx_t cctx = {.ctx = ctx, .key = cache_name, .content = &data};
      TRY(in3_plugin_execute_first_or_none(ctx, PLGN_ACT_CACHE_SET, &cctx))
    }
  }

  if (!token_dst) return IN3_OK;

  for (unsigned int i = 0; i < conf->token_len; i++) {
    if (d_type(token_src) == T_STRING) {
      if (strcmp(d_string(token_src), conf->tokens[i].symbol) == 0) {
        *token_dst = conf->tokens + i;
        return IN3_OK;
      }
    }
    else if (d_type(token_src) == T_BYTES && token_src->len == 20 && memcmp(token_src->data, conf->tokens[i].address, 20) == 0) {
      *token_dst = conf->tokens + i;
      return IN3_OK;
    }
  }

  return ctx_set_error(ctx, "could not find the specifed token", IN3_EFIND);
}

//static in3_ret_t payin_approve()
static d_token_t* params_get(d_token_t* params, d_key_t k, uint32_t index) {
  return d_type(params + 1) == T_OBJECT
             ? d_get(params + 1, k)
             : d_get_at(params, index);
}

static in3_ret_t payin(zksync_config_t* conf, in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  //  amount
  d_token_t* tmp           = NULL;
  bytes_t    amount        = d_to_bytes(params_get(params, key("amount"), 0));
  d_token_t* token         = params_get(params, key("token"), 1);
  bool       approve       = d_int(params_get(params, key("approveDepositAmountForERC20"), 2));
  uint8_t*   main_contract = conf->main_contract;

  // make sure we have an account
  uint8_t* account = conf->account;
  if ((tmp = params_get(params, key("depositTo"), 3))) {
    if (tmp->len != 20) return ctx_set_error(ctx->ctx, "invalid depositTo", IN3_ERPC);
    account = tmp->data;
  }
  else if (!account)
    TRY(zksync_get_account(conf, ctx->ctx, &account))

  // check main_contract
  if (!main_contract) TRY(zksync_get_contracts(conf, ctx->ctx, &main_contract))

  d_token_t*      tx_receipt = NULL;
  zksync_token_t* token_conf = NULL;
  TRY(resolve_tokens(conf, ctx->ctx, token, &token_conf))
  if (!token_conf) return IN3_EUNKNOWN;

  if (memiszero(token_conf->address, 20)) { // is eth
    sb_t sb = {0};
    sb_add_rawbytes(&sb, "{\"to\":\"0x", bytes(main_contract, 20), 0);
    sb_add_rawbytes(&sb, "\",\"data\":\"0x2d2da806", bytes(account, 20), 32);
    sb_add_rawbytes(&sb, "\",\"value\":\"0x", amount, 0);
    sb_add_chars(&sb, "\",\"gas\":\"0x30d40\"}");
    TRY_FINAL(send_provider_request(ctx->ctx, NULL, "eth_sendTransactionAndWait", sb.data, &tx_receipt), _free(sb.data))
  }
  else {

    if (approve) {
      sb_t sb = {0};
      sb_add_rawbytes(&sb, "{\"to\":\"0x", bytes(token_conf->address, 20), 20);
      sb_add_rawbytes(&sb, "\",\"data\":\"0x095ea7b3", bytes(main_contract, 20), 32);
      sb_add_rawbytes(&sb, NULL, amount, 32);
      sb_add_chars(&sb, "\",\"gas\":\"0x30d40\"}");

      TRY_FINAL(send_provider_request(ctx->ctx, NULL, "eth_sendTransactionAndWait", sb.data, &tx_receipt), _free(sb.data))
    }

    sb_t sb = {0};
    sb_add_rawbytes(&sb, "{\"to\":\"0x", bytes(main_contract, 20), 20);
    sb_add_rawbytes(&sb, "\",\"data\":\"0xe17376b5", bytes(token_conf->address, 20), 32);
    sb_add_rawbytes(&sb, NULL, amount, 32);
    sb_add_rawbytes(&sb, NULL, bytes(account, 20), 32);
    sb_add_chars(&sb, "\",\"gas\":\"0xffd40\"}");

    TRY_FINAL(send_provider_request(ctx->ctx, NULL, "eth_sendTransactionAndWait", sb.data, &tx_receipt), _free(sb.data))
  }

  // now that we have the receipt, we need to find the opId in the log
  bytes32_t event_hash;
  hex_to_bytes("d0943372c08b438a88d4b39d77216901079eda9ca59d45349841c099083b6830", -1, event_hash, 32);
  for (d_iterator_t iter = d_iter(d_get(tx_receipt, K_LOGS)); iter.left; d_iter_next(&iter)) {
    bytes_t* ev = d_get_bytes_at(d_get(iter.token, K_TOPICS), 0);
    if (ev && ev->len == 32 && memcmp(event_hash, ev->data, 32) == 0) {
      bytes_t* data = d_get_bytesk(iter.token, K_DATA);
      if (data && data->len > 64) {
        str_range_t r  = d_to_json(tx_receipt);
        sb_t*       sb = in3_rpc_handle_start(ctx);
        sb_add_chars(sb, "{\"receipt\":");
        sb_add_range(sb, r.data, 0, r.len);
        sb_add_chars(sb, ",\"priorityOpId\":");
        sb_add_int(sb, bytes_to_long(data->data + 64 - 8, 8));
        sb_add_chars(sb, "}");
        ctx_remove_required(ctx->ctx, ctx_find_required(ctx->ctx, "eth_sendTransactionAndWait"), true);
        return in3_rpc_handle_finish(ctx);
      }
    }
  }

  return ctx_set_error(ctx->ctx, "Could not find the serial in the receipt", IN3_EFIND);
}

static in3_ret_t emergency_withdraw(zksync_config_t* conf, in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  uint8_t         aid[4];
  zksync_token_t* token_conf    = NULL;
  uint8_t*        main_contract = conf->main_contract;
  uint8_t*        account       = conf->account;
  uint32_t        account_id    = 0;
  d_token_t*      tx_receipt    = NULL;
  sb_t            sb            = {0};

  // check main_contract
  TRY(zksync_get_contracts(conf, ctx->ctx, &main_contract))
  TRY(resolve_tokens(conf, ctx->ctx, params_get(params, key("token"), 0), &token_conf))
  TRY(zksync_get_account_id(conf, ctx->ctx, &account_id))
  TRY(zksync_get_account(conf, ctx->ctx, &account))

  int_to_bytes(account_id, aid);
  sb_add_rawbytes(&sb, "{\"to\":\"0x", bytes(main_contract, 20), 0);
  sb_add_rawbytes(&sb, "\",\"data\":\"0x000000e2", bytes(aid, 4), 32);
  sb_add_rawbytes(&sb, "", bytes(token_conf->address, 20), 32);
  sb_add_rawbytes(&sb, "\",\"from\":\"0x", bytes(account, 20), 20);
  sb_add_chars(&sb, "\",\"gas\":\"0x7a120\"}");
  TRY_FINAL(send_provider_request(ctx->ctx, NULL, "eth_sendTransactionAndWait", sb.data, &tx_receipt), _free(sb.data))
  if (d_type(tx_receipt) != T_OBJECT) return ctx_set_error(ctx->ctx, "no txreceipt found, which means the transaction was not succesful", IN3_EFIND);
  str_range_t r = d_to_json(tx_receipt);
  r.data[r.len] = 0;
  return in3_rpc_handle_with_string(ctx, r.data);
}

static in3_ret_t transfer(zksync_config_t* conf, in3_rpc_handle_ctx_t* ctx, d_token_t* params, zk_msg_type_t type) {
  bytes32_t sync_key;
  TRY(zksync_get_sync_key(conf, ctx->ctx, sync_key));
  // prepare tx data
  zksync_tx_data_t tx_data = {0};
  bytes_t          to      = d_to_bytes(params_get(params, type == ZK_WITHDRAW ? key("ethAddress") : K_TO, 0));
  if (!to.data || to.len != 20) return ctx_set_error(ctx->ctx, "invalid to address", IN3_EINVAL);
  memcpy(tx_data.to, to.data, 20);
  tx_data.type = type;

#ifdef ZKSYNC_256
  bytes_t amount = d_to_bytes(params_get(params, key("amount"), 1));
  if (amount.len > 33) return ctx_set_error(ctx->ctx, "invalid to amount", IN3_EINVAL);
  memcpy(tx_data.amount + 32 - amount.len, amount.data, amount.len);
#else
  tx_data.amount = d_long(params_get(params, key("amount"), 1));
#endif

  // prepare tx_data
  TRY(zksync_get_account_id(conf, ctx->ctx, &tx_data.account_id))
  TRY(resolve_tokens(conf, ctx->ctx, params_get(params, key("token"), 2), &tx_data.token))
  TRY(zksync_get_nonce(conf, ctx->ctx, params_get(params, K_NONCE, 4), &tx_data.nonce))
  TRY(zksync_get_fee(conf, ctx->ctx, params_get(params, key("fee"), 3), to, params_get(params, key("token"), 2), type == ZK_WITHDRAW ? "Withdraw" : "Transfer",
#ifdef ZKSYNC_256
                     tx_data.fee
#else
                     &tx_data.fee
#endif
                     ))
  memcpy(tx_data.from, conf->account, 20);

  // create payload
  cache_entry_t* cached = ctx->ctx->cache;
  while (cached) {
    if (cached->props & 0x10) break;
    cached = cached->next;
  }
  if (!cached) {
    sb_t      sb  = {0};
    in3_ret_t ret = zksync_sign_transfer(&sb, &tx_data, ctx->ctx, sync_key);
    if (ret && sb.data) _free(sb.data);
    if (!sb.data) return IN3_EUNKNOWN;
    TRY(ret)
    cached        = in3_cache_add_entry(&ctx->ctx->cache, bytes(NULL, 0), bytes((void*) sb.data, strlen(sb.data)));
    cached->props = CACHE_PROP_MUST_FREE | 0x10;
  }

  d_token_t* result = NULL;
  in3_ret_t  ret    = send_provider_request(ctx->ctx, conf, "tx_submit", (void*) cached->value.data, &result);
  if (ret == IN3_OK && cached && cached->value.data) {
    sb_t* sb = in3_rpc_handle_start(ctx);
    sb_add_range(sb, (void*) cached->value.data, 0, strlen((void*) cached->value.data) - 177);
    sb_add_chars(sb, ",\"txHash\":\"");
    sb_add_chars(sb, d_string(result));
    sb_add_chars(sb, "\"}");
    return in3_rpc_handle_finish(ctx);
  }
  return ret;
}

static in3_ret_t set_key(zksync_config_t* conf, in3_rpc_handle_ctx_t* ctx) {
  bytes32_t pk;
  address_t pub_hash;
  uint32_t  nonce;
  TRY(zksync_get_nonce(conf, ctx->ctx, NULL, &nonce))
  TRY(zksync_get_sync_key(conf, ctx->ctx, pk))
  zkcrypto_pk_to_pubkey(pk, pub_hash);
  if (memcmp(pub_hash, conf->pub_key_hash, 20) == 0) return ctx_set_error(ctx->ctx, "Signer key is already set", IN3_EINVAL);
  if (!conf->account_id) return ctx_set_error(ctx->ctx, "No Account set yet", IN3_EINVAL);

  // create payload
  cache_entry_t* cached = ctx->ctx->cache;
  while (cached) {
    if (cached->props & 0x10) break;
    cached = cached->next;
  }
  if (!cached) {
    sb_t      sb  = {0};
    in3_ret_t ret = zksync_sign_change_pub_key(&sb, ctx->ctx, pub_hash, nonce, conf->account, conf->account_id);
    if (ret && sb.data) _free(sb.data);
    if (!sb.data) return IN3_EUNKNOWN;
    TRY(ret)
    cached        = in3_cache_add_entry(&ctx->ctx->cache, bytes(NULL, 0), bytes((void*) sb.data, strlen(sb.data)));
    cached->props = CACHE_PROP_MUST_FREE | 0x10;
  }

  d_token_t* result = NULL;
  in3_ret_t  ret    = send_provider_request(ctx->ctx, conf, "tx_submit", (void*) cached->value.data, &result);
  if (ret == IN3_OK) {
    sb_t* sb = in3_rpc_handle_start(ctx);
    sb_add_rawbytes(sb, "\"sync:", bytes(pub_hash, 20), 20);
    sb_add_char(sb, '\"');
    return in3_rpc_handle_finish(ctx);
  }
  return ret;

  sb_t      sb = {0};
  in3_ret_t r  = zksync_sign_change_pub_key(&sb, ctx->ctx, pub_hash, nonce, conf->account, conf->account_id);
  if (r < 0) {
  }
}

static in3_ret_t zksync_rpc(zksync_config_t* conf, in3_rpc_handle_ctx_t* ctx) {
  char*      method = d_get_stringk(ctx->ctx->requests[0], K_METHOD);
  d_token_t* params = d_get(ctx->ctx->requests[0], K_PARAMS);
  if (strncmp(method, "zksync_", 7)) return IN3_EIGNORE;

  // do we have a provider?
  if (!conf->provider_url) {
    switch (ctx->ctx->client->chain.chain_id) {
      case CHAIN_ID_MAINNET:
        conf->provider_url = _strdupn("https://api.zksync.io/jsrpc", -1);
        break;
      default:
        return ctx_set_error(ctx->ctx, "no provider_url in config", IN3_EINVAL);
    }
  }
  if (strcmp(method, "zksync_depositToSyncFromEthereum") == 0 || strcmp(method, "zksync_deposit") == 0) return payin(conf, ctx, params);
  if (strcmp(method, "zksync_syncTransfer") == 0 || strcmp(method, "zksync_transfer") == 0) return transfer(conf, ctx, params, ZK_TRANSFER);
  if (strcmp(method, "zksync_withdraw") == 0) return transfer(conf, ctx, params, ZK_WITHDRAW);
  if (strcmp(method, "zksync_setKey") == 0) return set_key(conf, ctx);
  if (strcmp(method, "zksync_emergencyWithdraw") == 0) return emergency_withdraw(conf, ctx, params);
  if (strcmp(method, "zksync_getKey") == 0) {
    bytes32_t k;
    TRY(zksync_get_sync_key(conf, ctx->ctx, k))
    return in3_rpc_handle_with_bytes(ctx, bytes(k, 32));
  }
  if (strcmp(method, "zksync_contract_address") == 0) {
    uint8_t* adr;
    TRY(zksync_get_contracts(conf, ctx->ctx, &adr))
    sb_t* sb = in3_rpc_handle_start(ctx);
    sb_add_rawbytes(sb, "{\"govContract\":\"0x", bytes(conf->gov_contract, 20), 0);
    sb_add_rawbytes(sb, "\",\"mainContract\":\"0x", bytes(conf->main_contract, 20), 0);
    sb_add_chars(sb, "\"}");
    return in3_rpc_handle_finish(ctx);
  }
  if (strcmp(method, "zksync_tokens") == 0) {
    TRY(resolve_tokens(conf, ctx->ctx, NULL, NULL))
    sb_t* sb = in3_rpc_handle_start(ctx);
    sb_add_char(sb, '{');
    for (unsigned int i = 0; i < conf->token_len; i++) {
      if (i) sb_add_char(sb, ',');
      sb_add_char(sb, '\"');
      sb_add_chars(sb, conf->tokens[i].symbol);
      sb_add_rawbytes(sb, "\":{\"address\":\"0x", bytes(conf->tokens[i].address, 20), 0);
      sb_add_chars(sb, "\",\"decimals\":");
      sb_add_int(sb, conf->tokens[i].decimals);
      sb_add_chars(sb, ",\"id\":");
      sb_add_int(sb, conf->tokens[i].id);
      sb_add_chars(sb, ",\"symbol\":\"");
      sb_add_chars(sb, conf->tokens[i].symbol);
      sb_add_chars(sb, "\"}");
    }
    sb_add_char(sb, '}');
    return in3_rpc_handle_finish(ctx);
  }
  str_range_t p            = d_to_json(params);
  char*       param_string = alloca(p.len - 1);
  memcpy(param_string, p.data + 1, p.len - 2);
  param_string[p.len - 2] = 0;

  if (strcmp(method, "zksync_account_info") == 0 && *param_string == 0) {
    TRY(zksync_get_account(conf, ctx->ctx, NULL))
    param_string = alloca(45);
    set_quoted_address(param_string, conf->account);
  }
  if (strcmp(method, "zksync_ethop_info") == 0)
    sprintf(param_string, "%i", d_get_int_at(params, 0));

  d_token_t* result;
  TRY(send_provider_request(ctx->ctx, conf, method + 7, param_string, &result))

  char* json = d_create_json(NULL, result);
  in3_rpc_handle_with_string(ctx, json);
  _free(json);
  return IN3_OK;
}

static in3_ret_t handle_zksync(void* cptr, in3_plugin_act_t action, void* arg) {
  zksync_config_t* conf = cptr;
  switch (action) {
    case PLGN_ACT_TERM: {
      if (conf->provider_url) _free(conf->provider_url);
      if (conf->main_contract) _free(conf->main_contract);
      if (conf->account) _free(conf->account);
      _free(conf);
      return IN3_OK;
    }

    case PLGN_ACT_CONFIG_GET: {
      in3_get_config_ctx_t* ctx = arg;
      sb_add_chars(ctx->sb, ",\"zksync\":{\"provider_url\":\"");
      sb_add_chars(ctx->sb, conf->provider_url ? conf->provider_url : "");
      sb_add_char(ctx->sb, '\"');
      if (conf->account) {
        bytes_t ac = bytes(conf->account, 20);
        sb_add_bytes(ctx->sb, ",\"account\"=", &ac, 1, false);
      }
      sb_add_char(ctx->sb, '}');
      return IN3_OK;
    }

    case PLGN_ACT_CONFIG_SET: {
      in3_configure_ctx_t* ctx = arg;
      if (ctx->token->key == key("zksync")) {
        char* provider = d_get_string(ctx->token, "provider_url");
        if (provider) conf->provider_url = _strdupn(provider, -1);
        bytes_t* account = d_get_bytes(ctx->token, "account");
        if (account && account->len == 20) memcpy(conf->account = _malloc(20), account->data, 20);
        bytes_t* main_contract = d_get_bytes(ctx->token, "main_contract");
        if (main_contract && main_contract->len == 20) memcpy(conf->main_contract = _malloc(20), main_contract->data, 20);
        return IN3_OK;
      }
      return IN3_EIGNORE;
    }

    case PLGN_ACT_RPC_HANDLE:
      return zksync_rpc(conf, arg);

    default:
      return IN3_ENOTSUP;
  }

  return IN3_EIGNORE;
}

in3_ret_t in3_register_zksync(in3_t* c) {
  return plugin_register(c, PLGN_ACT_RPC_HANDLE | PLGN_ACT_TERM | PLGN_ACT_CONFIG_GET | PLGN_ACT_CONFIG_SET, handle_zksync, _calloc(sizeof(zksync_config_t), 1), false);
}