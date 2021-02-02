/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/blockchainsllc/in3
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

#include "cache.h"
#include "../util/bitset.h"
#include "../util/log.h"
#include "../util/mem.h"
#include "../util/utils.h"
#include "nodelist.h"
#include "plugin.h"
#include "stdio.h"
#include <assert.h>
#include <inttypes.h>
#include <string.h>

#define NODE_LIST_KEY   "nodelist_%d"
#define WHITTE_LIST_KEY "_0x%s"
#define CACHE_VERSION   6
#define MAX_KEYLEN      200

/**
 * generates and writes the cachekey
 */
static void write_cache_key(char* key, chain_id_t chain_id, const address_t whitelist_contract) {
  assert(key);
  assert(chain_id > 0);

  if (whitelist_contract) {                                           //  only  whitelistnodelists contain the address.
    char contract_[41];                                               // currently we have a max with of 40 which is more than the chain_id could hold
    bytes_to_hex(whitelist_contract, 20, contract_);                  // contract is appended as hex
    sprintf(key, NODE_LIST_KEY WHITTE_LIST_KEY, chain_id, contract_); // we need to append both to be unique
  }
  else
    sprintf(key, NODE_LIST_KEY, chain_id);
}

/**
 * initializes the cache by trying to read the nodelist and whitelist.
 */
in3_ret_t in3_cache_init(in3_t* c) {
  assert_in3(c);
  for (int i = 0; i < c->chains_length; i++) {
    // the reason why we ignore the error here, is because we want to ignore errors if the cache is able to update.
    if (in3_cache_update_nodelist(c, c->chains + i) != IN3_OK) { in3_log_debug("Failed to update cached nodelist\n"); }
    if (in3_cache_update_whitelist(c, c->chains + i) != IN3_OK) { in3_log_debug("Failed to update cached whitelist\n"); }
    in3_client_run_chain_whitelisting(c->chains + i);
  }
  return IN3_OK;
}

/**
 * updates the nodlist from the cache.
 */
in3_ret_t in3_cache_update_nodelist(in3_t* c, in3_chain_t* chain) {
  assert_in3(c);
  assert(chain);

  // it is ok not to have a storage
  if (!in3_plugin_is_registered(c, PLGN_ACT_CACHE_GET)) return IN3_OK;

  // define the key to use
  char key[MAX_KEYLEN];
  write_cache_key(key, chain->chain_id, chain->contract->data);

  // get from cache
  in3_cache_ctx_t cctx = {.ctx = NULL, .content = NULL, .key = key};
  in3_plugin_execute_all(c, PLGN_ACT_CACHE_GET, &cctx);
  bytes_t* b = cctx.content;
  if (!b) return IN3_OK;

  // so we have a result... let's decode it.
  int    node_count;
  size_t pos = 0;

  // version check
  if (b_read_byte(b, &pos) != CACHE_VERSION) {
    b_free(b);
    return IN3_EVERS;
  }

  // clean up old
  in3_nodelist_clear(chain);
  if (chain->contract) b_free(chain->contract);
  if (chain->nodelist_upd8_params) _free(chain->nodelist_upd8_params);

  // fill data
  chain->contract             = b_new_fixed_bytes(b, &pos, 20);
  chain->last_block           = b_read_long(b, &pos);
  chain->nodelist_length      = (node_count = b_read_int(b, &pos));
  chain->nodelist             = _calloc(node_count, sizeof(in3_node_t));
  chain->weights              = _calloc(node_count, sizeof(in3_node_weight_t));
  chain->nodelist_upd8_params = NULL;
  memcpy(chain->weights, b->data + pos, node_count * sizeof(in3_node_weight_t));
  pos += node_count * sizeof(in3_node_weight_t);

  for (int i = 0; i < node_count; i++) {
    in3_node_t* n = chain->nodelist + i;
    n->capacity   = b_read_int(b, &pos);
    n->index      = b_read_int(b, &pos);
    n->deposit    = b_read_long(b, &pos);
    n->props      = b_read_long(b, &pos);
    memcpy(n->address, b->data + pos, 20);
    pos += 20;
    n->url = b_new_chars(b, &pos);
    BIT_CLEAR(n->attrs, ATTR_WHITELISTED);
  }

  // read verified hashes
  const unsigned int hashes = b_read_int(b, &pos);
  if (!chain->verified_hashes && hashes) chain->verified_hashes = _calloc(c->max_verified_hashes, sizeof(in3_verified_hash_t));
  if (hashes)
    memcpy(chain->verified_hashes, b->data + pos, sizeof(in3_verified_hash_t) * (min(hashes, c->max_verified_hashes)));

  b_free(b);
  chain->dirty = false;
  return IN3_OK;
}

in3_ret_t in3_cache_store_nodelist(in3_t* c, in3_chain_t* chain) {
  assert_in3(c);
  assert(chain);

  // it is ok not to have a storage
  if (!in3_plugin_is_registered(c, PLGN_ACT_CACHE_SET) || !chain->dirty) return IN3_OK;

  // write to bytes_buffer
  bytes_builder_t* bb = bb_new();
  bb_write_byte(bb, CACHE_VERSION);          // Version flag
  bb_write_fixed_bytes(bb, chain->contract); // 20 bytes fixed
  bb_write_long(bb, chain->last_block);
  bb_write_int(bb, chain->nodelist_length);
  bb_write_raw_bytes(bb, chain->weights, chain->nodelist_length * sizeof(in3_node_weight_t));

  for (unsigned int i = 0; i < chain->nodelist_length; i++) {
    in3_node_t* n    = chain->nodelist + i;
    bytes_t     addr = bytes(n->address, 20);
    bb_write_int(bb, n->capacity);
    bb_write_int(bb, n->index);
    bb_write_long(bb, n->deposit);
    bb_write_long(bb, n->props);
    bb_write_fixed_bytes(bb, &addr);
    bb_write_chars(bb, n->url, strlen(n->url));
  }

  // verified hashes
  int count = 0;
  if (chain->verified_hashes) {
    count = c->max_verified_hashes;
    for (int i = 0; i < count; i++) {
      if (!chain->verified_hashes[i].block_number) {
        count = i;
        break;
      }
    }
    bb_write_int(bb, count);
    bb_write_raw_bytes(bb, chain->verified_hashes, count * sizeof(in3_verified_hash_t));
  }
  else
    bb_write_int(bb, 0);

  // create key
  char key[200];
  write_cache_key(key, chain->chain_id, chain->contract->data);

  // store it and ignore return value since failing when writing cache should not stop us.
  in3_cache_ctx_t cctx = {.ctx = NULL, .content = &bb->b, .key = key};
  in3_plugin_execute_all(c, PLGN_ACT_CACHE_SET, &cctx);

  chain->dirty = false;

  // clear buffer
  bb_free(bb);
  return IN3_OK;
}

in3_ret_t in3_cache_update_whitelist(in3_t* c, in3_chain_t* chain) {
  assert_in3(c);
  assert(chain);

  // it is ok not to have a storage
  if (!in3_plugin_is_registered(c, PLGN_ACT_CACHE_SET) || !chain->whitelist) return IN3_OK;

  in3_whitelist_t* wl = chain->whitelist;

  // define the key to use
  char key[MAX_KEYLEN];
  write_cache_key(key, chain->chain_id, wl->contract);

  // get from cache
  in3_cache_ctx_t cctx = {.ctx = NULL, .content = NULL, .key = key};
  in3_plugin_execute_all(c, PLGN_ACT_CACHE_GET, &cctx);
  bytes_t* data = cctx.content;
  if (data) {
    size_t pos = 0;

    // version check
    if (b_read_byte(data, &pos) != CACHE_VERSION) {
      b_free(data);
      return IN3_EVERS;
    }

    // clean up old
    if (wl->addresses.data) _free(wl->addresses.data);

    // fill data
    wl->last_block         = b_read_long(data, &pos);
    uint32_t adress_length = b_read_int(data, &pos) * 20;
    wl->addresses          = bytes(_malloc(adress_length), adress_length);
    memcpy(wl->addresses.data, data->data + pos, adress_length);
    b_free(data);
  }
  return IN3_OK;
}

in3_ret_t in3_cache_store_whitelist(in3_ctx_t* ctx, in3_chain_t* chain) {
  assert(ctx);
  assert_in3(ctx->client);
  assert(chain);

  // write to bytes_buffer
  if (!in3_plugin_is_registered(ctx->client, PLGN_ACT_CACHE_SET) || !chain->whitelist) return IN3_OK;

  const in3_whitelist_t* wl = chain->whitelist;
  bytes_builder_t*       bb = bb_new();
  bb_write_byte(bb, CACHE_VERSION); // Version flag
  bb_write_long(bb, wl->last_block);
  bb_write_int(bb, wl->addresses.len / 20);
  bb_write_fixed_bytes(bb, &wl->addresses);

  // create key
  char key[MAX_KEYLEN];
  write_cache_key(key, chain->chain_id, wl->contract);

  // store it and ignore return value since failing when writing cache should not stop us.
  in3_cache_ctx_t cctx = {.ctx = ctx, .key = key, .content = &bb->b};
  in3_plugin_execute_first_or_none(ctx, PLGN_ACT_CACHE_SET, &cctx);

  // clear buffer
  bb_free(bb);
  return IN3_OK;
}
