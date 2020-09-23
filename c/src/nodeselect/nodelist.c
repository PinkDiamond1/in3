/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/slockit/in3-c
 * 
 * Copyright (C) 2018-2020 slock.it GmbH, Blockchains LLC
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

#include "nodelist.h"
#include "../core/client/client.h"
#include "../core/client/context_internal.h"
#include "../core/client/keys.h"
#include "../core/util/bitset.h"
#include "../core/util/data.h"
#include "../core/util/debug.h"
#include "../core/util/log.h"
#include "../core/util/mem.h"
#include "../core/util/utils.h"
#include "cache.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DAY              24 * 3600
#define DIFFTIME(t1, t0) (double) (t1 > t0 ? t1 - t0 : 0)
#define BLACKLISTTIME    DAY
#define BLACKLISTWEIGHT  7 * DAY

NONULL static void free_nodeList(in3_node_t* nodelist, int count) {
  // clean data..
  for (int i = 0; i < count; i++) {
    if (nodelist[i].url) _free(nodelist[i].url);
  }
  _free(nodelist);
}

NONULL static bool postpone_update(const in3_nodeselect_def_t* data) {
  if (data->nodelist_upd8_params && data->nodelist_upd8_params->timestamp)
    if (DIFFTIME(data->nodelist_upd8_params->timestamp, in3_time(NULL)) > 0)
      return true;
  return false;
}

NONULL static inline bool nodelist_exp_last_block_neq(in3_nodeselect_def_t* data, uint64_t exp_last_block) {
  return (data->nodelist_upd8_params != NULL && data->nodelist_upd8_params->exp_last_block != exp_last_block);
}

NONULL static in3_ret_t fill_chain(in3_nodeselect_def_t* data, in3_ctx_t* ctx, d_token_t* result) {
  in3_ret_t      res  = IN3_OK;
  uint64_t       _now = in3_time(NULL);
  const uint64_t now  = (uint64_t) _now;

  // read the nodes
  d_token_t *  nodes = d_get(result, K_NODES), *t;
  unsigned int len   = d_len(nodes);

  if (!nodes || d_type(nodes) != T_ARRAY)
    return ctx_set_error(ctx, "No Nodes in the result", IN3_EINVALDT);

  if (!(t = d_get(result, K_LAST_BLOCK_NUMBER)))
    return ctx_set_error(ctx, "LastBlockNumer is missing", IN3_EINVALDT);

  // update last blockNumber
  const uint64_t last_block = d_long(t);
  if (last_block > data->last_block)
    data->last_block = last_block;
  else
    return IN3_OK; // if the last block is older than the current one we don't update, but simply ignore it.

  // new nodelist
  in3_node_t*        newList = _calloc(len, sizeof(in3_node_t));
  in3_node_weight_t* weights = _calloc(len, sizeof(in3_node_weight_t));
  d_token_t*         node    = NULL;

  // set new values
  for (unsigned int i = 0; i < len; i++) {
    in3_node_t* n = newList + i;
    node          = node ? d_next(node) : d_get_at(nodes, i);
    if (!node) {
      res = ctx_set_error(ctx, "node missing", IN3_EINVALDT);
      break;
    }

    int old_index      = i;
    n->capacity        = d_get_intkd(node, K_CAPACITY, 1);
    n->index           = d_get_intkd(node, K_INDEX, i);
    n->deposit         = d_get_longk(node, K_DEPOSIT);
    n->props           = d_get_longkd(node, K_PROPS, 65535);
    n->url             = d_get_stringk(node, K_URL);
    bytes_t* adr_bytes = d_get_byteskl(node, K_ADDRESS, 20);
    if (adr_bytes && adr_bytes->len == 20)
      memcpy(n->address, adr_bytes->data, 20);
    else {
      res = ctx_set_error(ctx, "missing address in nodelist", IN3_EINVALDT);
      break;
    }
    BIT_CLEAR(n->attrs, ATTR_BOOT_NODE); // nodes are considered boot nodes only until first nodeList update succeeds

    if ((ctx->client->flags & FLAGS_BOOT_WEIGHTS) && (t = d_get(node, K_PERFORMANCE))) {
      weights[i].blacklisted_until   = d_get_longk(t, K_LAST_FAILED) / 1000 + (24 * 3600);
      weights[i].response_count      = d_get_intk(t, K_COUNT);
      weights[i].total_response_time = d_get_intk(t, K_TOTAL);
    }

    // restore the nodeweights if the address was known in the old nodeList
    if (data->nodelist_length <= i || memcmp(data->nodelist[i].address, n->address, 20)) {
      old_index = -1;
      for (unsigned int j = 0; j < data->nodelist_length; j++) {
        if (memcmp(data->nodelist[j].address, n->address, 20) == 0) {
          old_index = j;
          break;
        }
      }
    }
    if (old_index >= 0) memcpy(weights + i, data->weights + old_index, sizeof(in3_node_weight_t));

    // if this is a newly registered node, we wait 24h before we use it, since this is the time where mallicous nodes may be unregistered.
    const uint64_t register_time = d_get_longk(node, K_REGISTER_TIME);
    if (now && register_time + DAY > now && now > register_time)
      weights[i].blacklisted_until = register_time + DAY;

    // clone the url since the src will be freed
    if (n->url)
      n->url = _strdupn(n->url, -1);
    else {
      res = ctx_set_error(ctx, "missing url in nodelist", IN3_EINVALDT);
      break;
    }
  }

  if (res == IN3_OK) {
    // successfull, so we can update the data.
    free_nodeList(data->nodelist, data->nodelist_length);
    _free(data->weights);
    data->nodelist        = newList;
    data->nodelist_length = len;
    data->weights         = weights;
  }
  else {
    free_nodeList(newList, len);
    _free(weights);
  }

  data->dirty = true;
  return res;
}

NONULL void in3_client_run_chain_whitelisting(in3_nodeselect_def_t* data) {
  if (!data->whitelist)
    return;

  for (unsigned int j = 0; j < data->nodelist_length; ++j)
    BIT_CLEAR(data->nodelist[j].attrs, ATTR_WHITELISTED);

  for (size_t i = 0; i < data->whitelist->addresses.len / 20; i += 20) {
    for (unsigned int j = 0; j < data->nodelist_length; ++j)
      if (!memcmp(data->whitelist->addresses.data + i, data->nodelist[j].address, 20))
        BIT_SET(data->nodelist[j].attrs, ATTR_WHITELISTED);
  }
}

NONULL static in3_ret_t in3_client_fill_chain_whitelist(in3_nodeselect_def_t* data, in3_ctx_t* ctx, d_token_t* result) {
  in3_whitelist_t* wl    = data->whitelist;
  int              i     = 0;
  d_token_t *      nodes = d_get(result, K_NODES), *t = NULL;

  if (!wl) return ctx_set_error(ctx, "No whitelist set", IN3_EINVALDT);
  if (!nodes || d_type(nodes) != T_ARRAY) return ctx_set_error(ctx, "No Nodes in the result", IN3_EINVALDT);

  const int len = d_len(nodes);
  if (!(t = d_get(result, K_LAST_BLOCK_NUMBER)))
    return ctx_set_error(ctx, "LastBlockNumer is missing", IN3_EINVALDT);

  // update last blockNumber
  const uint64_t last_block = d_long(t);
  if (last_block > wl->last_block)
    wl->last_block = last_block;
  else
    return IN3_OK; // if the last block is older than the current one we don't update, but simply ignore it.

  // now update the addresses
  if (wl->addresses.data) _free(wl->addresses.data);
  wl->addresses = bytes(_malloc(len * 20), len * 20);

  for (d_iterator_t iter = d_iter(nodes); iter.left; d_iter_next(&iter), i += 20)
    d_bytes_to(iter.token, wl->addresses.data + i, 20);

  in3_client_run_chain_whitelisting(data);
  return IN3_OK;
}

NONULL static in3_ret_t update_nodelist(in3_t* c, in3_nodeselect_def_t* data, in3_ctx_t* parent_ctx) {
  // is there a useable required ctx?
  in3_ctx_t* ctx = ctx_find_required(parent_ctx, "in3_nodeList");

  if (ctx) {
    if (in3_ctx_state(ctx) == CTX_ERROR || (in3_ctx_state(ctx) == CTX_SUCCESS && !d_get(ctx->responses[0], K_RESULT))) {
      // blacklist node that gave us an error response for nodelist (if not first update)
      // and clear nodelist params
      if (nodelist_not_first_upd8(data))
        blacklist_node_addr(data, data->nodelist_upd8_params->node, BLACKLISTTIME);
      _free(data->nodelist_upd8_params);
      data->nodelist_upd8_params = NULL;

      // if first update return error otherwise return IN3_OK, this is because first update is
      // always from a boot node which is presumed to be trusted
      return nodelist_first_upd8(data)
                 ? ctx_set_error(parent_ctx, "Error updating node_list", ctx_set_error(parent_ctx, ctx->error, IN3_ERPC))
                 : IN3_OK;
    }

    switch (in3_ctx_state(ctx)) {
      case CTX_ERROR: return IN3_OK; /* already handled before*/
      case CTX_WAITING_FOR_RESPONSE:
      case CTX_WAITING_TO_SEND:
        return IN3_WAITING;
      case CTX_SUCCESS: {
        d_token_t* r = d_get(ctx->responses[0], K_RESULT);
        // if the `lastBlockNumber` != `exp_last_block`, we can be certain that `data->nodelist_upd8_params->node` lied to us
        // about the nodelist update, so we blacklist it for an hour
        if (nodelist_exp_last_block_neq(data, d_get_longk(r, K_LAST_BLOCK_NUMBER)))
          blacklist_node_addr(data, data->nodelist_upd8_params->node, BLACKLISTTIME);
        _free(data->nodelist_upd8_params);
        data->nodelist_upd8_params = NULL;

        const in3_ret_t res = fill_chain(data, ctx, r);
        if (res < 0)
          return ctx_set_error(parent_ctx, "Error updating node_list", ctx_set_error(parent_ctx, ctx->error, res));
        in3_cache_store_nodelist(ctx->client, data);
        ctx_remove_required(parent_ctx, ctx, true);
        in3_client_run_chain_whitelisting(data);
        return IN3_OK;
      }
    }
  }

  in3_log_debug("update the nodelist...\n");

  // create random seed
  char seed[67] = {'0', 'x'};
  for (int i = 0, j = 2; i < 8; ++i, j += 8)
    sprintf(seed + j, "%08x", in3_rand(NULL) % 0xFFFFFFFF);

  sb_t* in3_sec = sb_new("{");
  if (nodelist_not_first_upd8(data)) {
    bytes_t addr_ = (bytes_t){.data = data->nodelist_upd8_params->node, .len = 20};
    sb_add_bytes(in3_sec, "\"dataNodes\":", &addr_, 1, true);
  }

  // create request
  char* req = _malloc(350);
  sprintf(req, "{\"method\":\"in3_nodeList\",\"jsonrpc\":\"2.0\",\"params\":[%i,\"%s\",[]%s],\"in3\":%s}",
          c->node_limit, seed,
          ((c->flags & FLAGS_BOOT_WEIGHTS) && nodelist_first_upd8(data)) ? ",true" : "",
          sb_add_char(in3_sec, '}')->data);
  sb_free(in3_sec);

  // new client
  return ctx_add_required(parent_ctx, ctx_new(c, req));
}

NONULL static in3_ret_t update_whitelist(in3_t* c, in3_nodeselect_def_t* data, in3_ctx_t* parent_ctx) {
  // is there a useable required ctx?
  in3_ctx_t* ctx = ctx_find_required(parent_ctx, "in3_whiteList");

  if (ctx)
    switch (in3_ctx_state(ctx)) {
      case CTX_ERROR:
        return ctx_set_error(parent_ctx, "Error updating white_list", ctx_set_error(parent_ctx, ctx->error, IN3_ERPC));
      case CTX_WAITING_FOR_RESPONSE:
      case CTX_WAITING_TO_SEND:
        return IN3_WAITING;
      case CTX_SUCCESS: {

        d_token_t* result = d_get(ctx->responses[0], K_RESULT);
        if (result) {
          // we have a result....
          const in3_ret_t res = in3_client_fill_chain_whitelist(data, ctx, result);
          if (res < 0)
            return ctx_set_error(parent_ctx, "Error updating white_list", ctx_set_error(parent_ctx, ctx->error, res));
          in3_cache_store_whitelist(ctx->client, data);
          in3_client_run_chain_whitelisting(data);
          ctx_remove_required(parent_ctx, ctx, true);
          return IN3_OK;
        }
        else
          return ctx_set_error(parent_ctx, "Error updating white_list", ctx_check_response_error(ctx, 0));
      }
    }

  in3_log_debug("update the whitelist...\n");

  // create request
  char* req     = _malloc(300);
  char  tmp[41] = {0};
  bytes_to_hex(data->whitelist->contract, 20, tmp);
  sprintf(req, "{\"method\":\"in3_whiteList\",\"jsonrpc\":\"2.0\",\"params\":[\"0x%s\"]}", tmp);

  // new client
  return ctx_add_required(parent_ctx, ctx_new(c, req));
}

in3_ret_t update_nodes(in3_t* c, in3_nodeselect_def_t* data) {
  in3_ctx_t* ctx          = _calloc(1, sizeof(in3_ctx_t));
  ctx->verification_state = IN3_EIGNORE;
  ctx->error              = _calloc(1, 1);
  ctx->client             = c;
  if (data->nodelist_upd8_params) {
    _free(data->nodelist_upd8_params);
    data->nodelist_upd8_params = NULL;
  }

  in3_ret_t ret = update_nodelist(c, data, ctx);
  if (ret == IN3_WAITING && ctx->required) {
    ret = in3_send_ctx(ctx->required);
    if (!ret) ret = update_nodelist(c, data, ctx);
  }

  ctx_free(ctx);
  return ret;
}

IN3_EXPORT_TEST bool in3_node_props_match(const in3_node_props_t np_config, const in3_node_props_t np) {
  if (((np_config & np) & 0xFFFFFFFF) != (np_config & 0XFFFFFFFF)) return false;
  uint32_t min_blk_ht_conf = in3_node_props_get(np_config, NODE_PROP_MIN_BLOCK_HEIGHT);
  uint32_t min_blk_ht      = in3_node_props_get(np, NODE_PROP_MIN_BLOCK_HEIGHT);
  return min_blk_ht_conf ? (min_blk_ht <= min_blk_ht_conf) : true;
}

uint32_t in3_node_calculate_weight(in3_node_weight_t* n, uint32_t capa, uint64_t now) {
  // calculate the averge response time
  const uint32_t avg = (n->response_count > 4 && n->total_response_time)
                           ? (n->total_response_time / n->response_count)
                           : (10000 / (max(capa, 100) + 100));

  // and the weights based factore
  const uint32_t blacklist_factor = ((now - n->blacklisted_until) < BLACKLISTWEIGHT)
                                        ? ((now - n->blacklisted_until) * 100 / (BLACKLISTWEIGHT))
                                        : 100;
  return (0xFFFF / avg) * blacklist_factor / 100;
}

node_match_t* in3_node_list_fill_weight(in3_t* c, in3_nodeselect_def_t* data, in3_node_t* all_nodes, in3_node_weight_t* weights,
                                        int len, uint64_t now, uint32_t* total_weight, int* total_found,
                                        in3_node_filter_t filter) {

  int                found      = 0;
  uint32_t           weight_sum = 0;
  in3_node_t*        node_def   = NULL;
  in3_node_weight_t* weight_def = NULL;
  node_match_t*      prev       = NULL;
  node_match_t*      current    = NULL;
  node_match_t*      first      = NULL;
  *total_found                  = 0;

  for (int i = 0; i < len; i++) {
    node_def   = all_nodes + i;
    weight_def = weights + i;

    if (filter.nodes != NULL) {
      bool in_filter_nodes = false;
      for (d_iterator_t it = d_iter(filter.nodes); it.left; d_iter_next(&it)) {
        if (memcmp(d_bytesl(it.token, 20)->data, node_def->address, 20) == 0) {
          in_filter_nodes = true;
          break;
        }
      }
      if (!in_filter_nodes)
        continue;
    }
    if (weight_def->blacklisted_until > (uint64_t) now) continue;
    if (BIT_CHECK(node_def->attrs, ATTR_BOOT_NODE)) goto SKIP_FILTERING;
    if (data->whitelist && !BIT_CHECK(node_def->attrs, ATTR_WHITELISTED)) continue;
    if (node_def->deposit < c->min_deposit) continue;
    if (!in3_node_props_match(filter.props, node_def->props)) continue;

  SKIP_FILTERING:
    current = _malloc(sizeof(node_match_t));
    if (!first) first = current;
    current->index   = i;
    current->blocked = false;
    current->next    = NULL;
    current->s       = weight_sum;
    current->w       = in3_node_calculate_weight(weight_def, node_def->capacity, now);
    weight_sum += current->w;
    found++;
    if (prev) prev->next = current;
    prev = current;
  }
  *total_weight = weight_sum;
  *total_found  = found;
  return first;
}

static bool update_in_progress(const in3_ctx_t* ctx) {
  return ctx_is_method(ctx, "in3_nodeList");
}

in3_ret_t in3_node_list_get(in3_ctx_t* ctx, in3_nodeselect_def_t* data, bool update, in3_node_t** nodelist, int* nodelist_length, in3_node_weight_t** weights) {
  in3_ret_t res = IN3_EFIND;

  // do we need to update the nodelist?
  if (data->nodelist_upd8_params || update || ctx_find_required(ctx, "in3_nodeList")) {
    // skip update if update has been postponed or there's already one in progress
    if (postpone_update(data) || update_in_progress(ctx))
      goto SKIP_UPDATE;

    // now update the nodeList
    res = update_nodelist(ctx->client, data, ctx);
    if (res < 0) return res;
  }

SKIP_UPDATE:
  // do we need to update the whiitelist?
  if (data->whitelist                                                                         // only if we have a whitelist
      && (data->whitelist->needs_update || update || ctx_find_required(ctx, "in3_whiteList")) // which has the needs_update-flag (or forced) or we have already sent the request and are now picking up the result
      && !memiszero(data->whitelist->contract, 20)) {                                         // and we need to have a contract set, zero-contract = manual whitelist, which will not be updated.
    data->whitelist->needs_update = false;
    // now update the whiteList
    res = update_whitelist(ctx->client, data, ctx);
    if (res < 0) return res;
  }

  // now update the results
  *nodelist_length = data->nodelist_length;
  *nodelist        = data->nodelist;
  *weights         = data->weights;
  return IN3_OK;
}

in3_ret_t in3_node_list_pick_nodes(in3_ctx_t* ctx, in3_nodeselect_def_t* data, node_match_t** nodes, int request_count, in3_node_filter_t filter) {

  // get all nodes from the nodelist
  uint64_t           now       = in3_time(NULL);
  in3_node_t*        all_nodes = NULL;
  in3_node_weight_t* weights   = NULL;
  uint32_t           total_weight;
  int                all_nodes_len, total_found;

  in3_ret_t res = in3_node_list_get(ctx, data, false, &all_nodes, &all_nodes_len, &weights);
  if (res < 0)
    return ctx_set_error(ctx, "could not find the data", res);

  // filter out nodes
  node_match_t* found = in3_node_list_fill_weight(
      ctx->client, data, all_nodes, weights, all_nodes_len,
      now, &total_weight, &total_found, filter);

  if (total_found == 0) {
    // no node available, so we should check if we can retry some blacklisted
    int blacklisted = 0;
    for (int i = 0; i < all_nodes_len; i++) {
      if (weights[i].blacklisted_until > (uint64_t) now) blacklisted++;
    }

    // if morethan 50% of the nodes are blacklisted, we remove the mark and try again
    if (blacklisted > all_nodes_len / 2) {
      for (int i = 0; i < all_nodes_len; i++)
        weights[i].blacklisted_until = 0;
      found = in3_node_list_fill_weight(ctx->client, data, all_nodes, weights, all_nodes_len, now, &total_weight, &total_found, filter);
    }

    if (total_found == 0)
      return ctx_set_error(ctx, "No nodes found that match the criteria", IN3_EFIND);
  }

  int filled_len = total_found < request_count ? total_found : request_count;
  if (total_found == filled_len) {
    *nodes = found;
    return IN3_OK;
  }

  uint32_t      r;
  int           added   = 0;
  node_match_t* last    = NULL;
  node_match_t* first   = NULL;
  node_match_t* next    = NULL;
  node_match_t* current = NULL;

  // we want ot make sure this loop is run only max 10xthe number of requested nodes
  for (int i = 0; added < filled_len && i < filled_len * 10; i++) {
    // pick a random number
    r = total_weight ? (in3_rand(NULL) % total_weight) : 0;

    // find the first node matching it.
    current = found;
    while (current) {
      if (current->s <= r && current->s + current->w >= r) break;
      current = current->next;
    }

    if (current) {
      // check if we already added it,
      next = first;
      while (next) {
        if (next->index == current->index) break;
        next = next->next;
      }

      if (!next) {
        added++;
        next        = _calloc(1, sizeof(node_match_t));
        next->s     = current->s;
        next->w     = current->w;
        next->index = current->index;

        if (!first) first = next;
        if (last) {
          last->next = next;
          last       = last->next;
        }
        else
          last = first;
      }
    }
  }

  *nodes = first;
  if (found) in3_ctx_free_nodes(found);

  // select them based on random
  return res;
}

/** removes all nodes and their weights from the nodelist */
void in3_nodelist_clear(in3_nodeselect_def_t* data) {
  for (unsigned int i = 0; i < data->nodelist_length; i++) {
    if (data->nodelist[i].url) _free(data->nodelist[i].url);
  }
  _free(data->nodelist);
  _free(data->weights);
  data->dirty = true;
}

void in3_whitelist_clear(in3_whitelist_t* wl) {
  if (!wl) return;
  if (wl->addresses.data) _free(wl->addresses.data);
  _free(wl);
}

void in3_node_props_set(in3_node_props_t* node_props, in3_node_props_type_t type, uint8_t value) {
  if (type == NODE_PROP_MIN_BLOCK_HEIGHT) {
    const uint64_t dp_ = value;
    *node_props        = (*node_props & 0xFFFFFFFF) | (dp_ << 32U);
  }
  else {
    (value != 0) ? ((*node_props) |= type) : ((*node_props) &= ~type);
  }
}
