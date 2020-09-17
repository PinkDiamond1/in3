#include "nodeselect_def.h"
#include "../core/client/plugin.h"
#include "../core/util/bitset.h"
#include "../core/util/debug.h"
#include "cache.h"

#define BOOT_NODES_MAINNET "{"                                                                \
                           " \"servers\": {"                                                  \
                           "  \"0x1\": {"                                                     \
                           "   \"nodeList\": [{"                                              \
                           "    \"address\": \"0x45d45e6ff99e6c34a235d263965910298985fcfe\"," \
                           "    \"url\": \"https://in3-v2.slock.it/mainnet/nd-1\","           \
                           "    \"props\": \"0xFFFF\""                                        \
                           "   }, {"                                                          \
                           "    \"address\": \"0x1fe2e9bf29aa1938859af64c413361227d04059a\"," \
                           "    \"url\": \"https://in3-v2.slock.it/mainnet/nd-2\","           \
                           "    \"props\": \"0xFFFF\""                                        \
                           "   }, {"                                                          \
                           "    \"address\": \"0x0cea2ff03adcfa047e8f54f98d41d9147c3ccd4d\"," \
                           "    \"url\": \"https://in3-v2.slock.it/mainnet/nd-3\","           \
                           "    \"props\": \"0xFFFF\""                                        \
                           "   }, {"                                                          \
                           "    \"address\": \"0xccd12a2222995e62eca64426989c2688d828aa47\"," \
                           "    \"url\": \"https://in3-v2.slock.it/mainnet/nd-4\","           \
                           "    \"props\": \"0xFFFF\""                                        \
                           "   }, {"                                                          \
                           "    \"address\": \"0x510ee7f6f198e018e3529164da2473a96eeb3dc8\"," \
                           "    \"url\": \"https://in3-v2.slock.it/mainnet/nd-5\","           \
                           "    \"props\": \"0xFFFF\""                                        \
                           "   }]"                                                            \
                           "  }"                                                              \
                           " }"                                                               \
                           "}"

static uint16_t avg_block_time_for_chain_id(chain_id_t id) {
  switch (id) {
    case CHAIN_ID_MAINNET:
    case CHAIN_ID_GOERLI: return 15;
    case CHAIN_ID_KOVAN: return 6;
    default: return 5;
  }
}

static in3_ret_t in3_client_add_node(in3_nodeselect_def_t* data, char* url, in3_node_props_t props, address_t address) {
  assert(data);
  assert(url);
  assert(address);

  in3_node_t*  node       = NULL;
  unsigned int node_index = data->nodelist_length;
  for (unsigned int i = 0; i < data->nodelist_length; i++) {
    if (memcmp(data->nodelist[i].address, address, 20) == 0) {
      node       = data->nodelist + i;
      node_index = i;
      break;
    }
  }
  if (!node) {
    // init or change the size ofthe nodelist
    data->nodelist = data->nodelist
                         ? _realloc(data->nodelist, sizeof(in3_node_t) * (data->nodelist_length + 1), sizeof(in3_node_t) * data->nodelist_length)
                         : _calloc(data->nodelist_length + 1, sizeof(in3_node_t));
    // the weights always have to have the same size
    data->weights = data->weights
                        ? _realloc(data->weights, sizeof(in3_node_weight_t) * (data->nodelist_length + 1), sizeof(in3_node_weight_t) * data->nodelist_length)
                        : _calloc(data->nodelist_length + 1, sizeof(in3_node_weight_t));
    if (!data->nodelist || !data->weights) return IN3_ENOMEM;
    node = data->nodelist + data->nodelist_length;
    memcpy(node->address, address, 20);
    node->index    = data->nodelist_length;
    node->capacity = 1;
    node->deposit  = 0;
    BIT_CLEAR(node->attrs, ATTR_WHITELISTED);
    data->nodelist_length++;
  }
  else
    _free(node->url);

  node->props = props;
  node->url   = _malloc(strlen(url) + 1);
  memcpy(node->url, url, strlen(url) + 1);

  in3_node_weight_t* weight   = data->weights + node_index;
  weight->blacklisted_until   = 0;
  weight->response_count      = 0;
  weight->total_response_time = 0;
  return IN3_OK;
}

static in3_ret_t in3_client_remove_node(in3_nodeselect_def_t* data, address_t address) {
  assert(data);
  assert(address);

  int node_index = -1;
  for (unsigned int i = 0; i < data->nodelist_length; i++) {
    if (memcmp(data->nodelist[i].address, address, 20) == 0) {
      node_index = i;
      break;
    }
  }
  if (node_index == -1) return IN3_EFIND;
  if (data->nodelist[node_index].url)
    _free(data->nodelist[node_index].url);

  if (node_index < ((signed) data->nodelist_length) - 1) {
    memmove(data->nodelist + node_index, data->nodelist + node_index + 1, sizeof(in3_node_t) * (data->nodelist_length - 1 - node_index));
    memmove(data->weights + node_index, data->weights + node_index + 1, sizeof(in3_node_weight_t) * (data->nodelist_length - 1 - node_index));
  }
  data->nodelist_length--;
  if (!data->nodelist_length) {
    _free(data->nodelist);
    _free(data->weights);
    data->nodelist = NULL;
    data->weights  = NULL;
  }
  return IN3_OK;
}

static in3_ret_t in3_client_clear_nodes(in3_nodeselect_def_t* data) {
  assert(data);

  in3_nodelist_clear(data);
  data->nodelist        = NULL;
  data->weights         = NULL;
  data->nodelist_length = 0;
  return IN3_OK;
}

static in3_ret_t nl_config_set(in3_nodeselect_def_t* data, in3_configure_ctx_t* ctx) {
  char*       res   = NULL;
  json_ctx_t* json  = ctx->json;
  d_token_t*  token = ctx->token;
  in3_t*      c     = ctx->client;

  if (token->key == key("servers") || token->key == key("nodes")) {
    for (d_iterator_t ct = d_iter(token); ct.left; d_iter_next(&ct)) {
      bytes_t* wl_contract = d_get_byteskl(ct.token, key("whiteListContract"), 20);

      if (wl_contract && wl_contract->len == 20) {
        data->whitelist                 = _malloc(sizeof(in3_whitelist_t));
        data->whitelist->addresses.data = NULL;
        data->whitelist->addresses.len  = 0;
        data->whitelist->needs_update   = true;
        data->whitelist->last_block     = 0;
        memcpy(data->whitelist->contract, wl_contract->data, 20);
      }

      bool has_wlc = false, has_man_wl = false;
      for (d_iterator_t cp = d_iter(ct.token); cp.left; d_iter_next(&cp)) {
        if (cp.token->key == key("whiteListContract")) {
          EXPECT_TOK_ADDR(cp.token);
          EXPECT_CFG(!has_man_wl, "cannot specify manual whiteList and whiteListContract together!");
          has_wlc = true;
          in3_whitelist_clear(data->whitelist);
          data->whitelist               = _calloc(1, sizeof(in3_whitelist_t));
          data->whitelist->needs_update = true;
          memcpy(data->whitelist->contract, cp.token->data, 20);
        }
        else if (cp.token->key == key("whiteList")) {
          EXPECT_TOK_ARR(cp.token);
          EXPECT_CFG(!has_wlc, "cannot specify manual whiteList and whiteListContract together!");
          has_man_wl = true;
          int len = d_len(cp.token), i = 0;
          in3_whitelist_clear(data->whitelist);
          data->whitelist            = _calloc(1, sizeof(in3_whitelist_t));
          data->whitelist->addresses = bytes(_calloc(1, len * 20), len * 20);
          for (d_iterator_t n = d_iter(cp.token); n.left; d_iter_next(&n), i += 20) {
            EXPECT_TOK_ADDR(n.token);
            const uint8_t* whitelist_address = d_bytes(n.token)->data;
            for (uint32_t j = 0; j < data->whitelist->addresses.len; j += 20) {
              if (!memcmp(whitelist_address, data->whitelist->addresses.data + j, 20)) {
                in3_whitelist_clear(data->whitelist);
                data->whitelist = NULL;
                EXPECT_TOK(cp.token, false, "duplicate address!");
              }
            }
            d_bytes_to(n.token, data->whitelist->addresses.data + i, 20);
          }
        }
        else if (cp.token->key == key("needsUpdate")) {
          EXPECT_TOK_BOOL(cp.token);
          if (!d_int(cp.token)) {
            if (data->nodelist_upd8_params) {
              _free(data->nodelist_upd8_params);
              data->nodelist_upd8_params = NULL;
            }
          }
          else if (!data->nodelist_upd8_params)
            data->nodelist_upd8_params = _calloc(1, sizeof(*(data->nodelist_upd8_params)));
        }
        else if (cp.token->key == key("avgBlockTime")) {
          EXPECT_TOK_U16(cp.token);
          data->avg_block_time = (uint16_t) d_int(cp.token);
        }
        else if (cp.token->key == key("nodeList")) {
          EXPECT_TOK_ARR(cp.token);
          if (in3_client_clear_nodes(data) < 0) goto cleanup;
          int i = 0;
          for (d_iterator_t n = d_iter(cp.token); n.left; d_iter_next(&n), i++) {
            EXPECT_CFG(d_get(n.token, key("url")) && d_get(n.token, key("address")), "expected URL & address");
            EXPECT_TOK_STR(d_get(n.token, key("url")));
            EXPECT_TOK_ADDR(d_get(n.token, key("address")));
            EXPECT_CFG(in3_client_add_node(data, d_get_string(n.token, "url"),
                                           d_get_longkd(n.token, key("props"), 65535),
                                           d_get_byteskl(n.token, key("address"), 20)->data) == IN3_OK,
                       "add node failed");
#ifndef __clang_analyzer__
            BIT_SET(data->nodelist[i].attrs, ATTR_BOOT_NODE);
#endif
          }
        }
        else {
          EXPECT_TOK(cp.token, false, "unsupported config option!");
        }
      }
      in3_client_run_chain_whitelisting(data);
    }
  }
cleanup:
  ctx->error_msg = res;
  return IN3_OK;
}

static in3_ret_t nl_config_get(in3_nodeselect_def_t* data, in3_get_config_ctx_t* ctx) {
  sb_t*  sb = ctx->sb;
  in3_t* c  = ctx->client;
  sb_add_chars(sb, ",\"nodes\":{");
  for (int i = 0; i < c->chains_length; i++) {
    in3_chain_t* chain = c->chains + i;
    if (i) sb_add_char(sb, ',');
    sb_add_char(sb, '"');
    sb_add_hexuint(sb, chain->chain_id);
    sb_add_chars(sb, "\":");
    add_hex(sb, '{', "contract", *chain->contract);
    if (data->whitelist)
      add_hex(sb, ',', "whiteListContract", bytes(data->whitelist->contract, 20));
    add_hex(sb, ',', "registryId", bytes(chain->registry_id, 32));
    add_bool(sb, ',', "needsUpdate", data->nodelist_upd8_params != NULL);
    add_uint(sb, ',', "avgBlockTime", data->avg_block_time);
    sb_add_chars(sb, ",\"nodeList\":[");
    for (unsigned int j = 0; j < data->nodelist_length; j++) {
      if ((data->nodelist[j].attrs & ATTR_BOOT_NODE) == 0) continue;
      if (sb->data[sb->len - 1] != '[') sb_add_char(sb, ',');
      add_string(sb, '{', "url", data->nodelist[j].url);
      add_uint(sb, ',', "props", data->nodelist[j].props);
      add_hex(sb, ',', "address", bytes(data->nodelist[j].address, 20));
      sb_add_char(sb, '}');
    }
    if (sb->data[sb->len - 1] == '[') {
      sb->len -= 13;
      sb_add_char(sb, '}');
    }
    else
      sb_add_chars(sb, "]}");
  }
  sb_add_chars(sb, "}");
  return IN3_OK;
}

static in3_ret_t nl_pick_data(in3_nodeselect_def_t* data, void* ctx) {
  return IN3_OK;
}

static in3_ret_t nl_pick_signer(in3_nodeselect_def_t* data, void* ctx) {
  return IN3_OK;
}

static in3_ret_t nl_pick_followup(in3_nodeselect_def_t* data, void* ctx) {
  return IN3_OK;
}

static in3_ret_t nodeselect(void* plugin_data, in3_plugin_act_t action, void* plugin_ctx) {
  in3_nodeselect_def_t* data = plugin_data;
  switch (action) {
    case PLGN_ACT_INIT:
      data->avg_block_time = avg_block_time_for_chain_id(((in3_configure_ctx_t*) plugin_ctx)->client->chain_id);
      return IN3_OK;
    case PLGN_ACT_TERM:
      in3_whitelist_clear(data->whitelist);
      in3_nodelist_clear(data);
      _free(data);
      return IN3_OK;
    case PLGN_ACT_CONFIG_SET:
      return nl_config_set(data, (in3_configure_ctx_t*) plugin_ctx);
    case PLGN_ACT_CONFIG_GET:
      return nl_config_get(data, (in3_get_config_ctx_t*) plugin_ctx);
    case PLGN_ACT_NL_PICK_DATA:
      return nl_pick_data(data, plugin_ctx);
    case PLGN_ACT_NL_PICK_SIGNER:
      return nl_pick_signer(data, plugin_ctx);
    case PLGN_ACT_NL_PICK_FOLLOWUP:
      return nl_pick_followup(data, plugin_ctx);
    default: break;
  }
  return IN3_EIGNORE;
}

in3_ret_t in3_register_nodeselect_def(in3_t* c) {
  in3_nodeselect_def_t* data = _calloc(1, sizeof(*data));
  return in3_plugin_register(c, PLGN_ACT_LIFECYCLE | PLGN_ACT_NODELIST | PLGN_ACT_CONFIG, nodeselect, data, false);
}
