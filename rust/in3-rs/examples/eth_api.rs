use std::convert::TryInto;

use async_std::task;
use in3::eth1::*;
use in3::json_rpc::json::{from_str, from_value, json};
use in3::logging;
use in3::prelude::*;

fn main() -> In3Result<()> {
    // enable logging
    logging::enable();
    logging::set_level(logging::FilterLevel::Debug);

    // configure client and API
    let mut eth_api = Api::new(Client::new(chain::MAINNET));
    eth_api
        .client()
        .configure(r#"{"autoUpdateList":false,"nodeRegistry":{"needsUpdate":false}}}"#)?;

    // eth_getStorageAt
    let address: Address = from_str(r#""0x0123456789012345678901234567890123456789""#)?;
    let key: U256 = 0u64.into();
    let storage: u64 = task::block_on(eth_api.get_storage_at(address, key, BlockNumber::Latest))?
        .try_into()
        .expect("cannot convert to u64");
    println!("Storage value => {:?}", storage);

    // eth_getCode
    let address: Address = from_str(r#""0xac1b824795e1eb1f6e609fe0da9b9af8beaab60f""#)?;
    let code: Bytes = task::block_on(eth_api.get_code(address, BlockNumber::Latest))?
        .try_into()
        .expect("cannot convert to Bytes");
    println!("Code at address {:?} => {:?}", address, code);

    // eth_blockNumber
    let latest_blk_num: u64 = task::block_on(eth_api.block_number())?
        .try_into()
        .expect("cannot convert to u64");
    println!("Latest block number => {:?}", latest_blk_num);

    // eth_gasPrice
    let gas_price: u64 = task::block_on(eth_api.gas_price())?
        .try_into()
        .expect("cannot convert to u64");
    println!("Gas price => {:?}", gas_price);

    // eth_getBalance
    let address: Address = from_str(r#""0x0123456789012345678901234567890123456789""#)?;
    let balance: u64 = task::block_on(
        eth_api.get_balance(address, BlockNumber::Number((latest_blk_num - 10).into())),
    )?
    .try_into()
    .expect("cannot convert to u64");
    println!("Balance of address {:?} is {:?} wei", address, balance);

    // eth_getBlockByNumber
    let block: Block = task::block_on(eth_api.get_block_by_number(BlockNumber::Latest, false))?;
    println!("Block => {:?}", block);

    // eth_getBlockByHash
    let hash: Hash =
        from_str(r#""0xa2ad3d67e3a09d016ab72e40fc1e47d6662f9156f16ce1cce62d5805a62ffd02""#)?;
    let block: Block = task::block_on(eth_api.get_block_by_hash(hash, false))?;
    println!("Block => {:?}", block);

    // eth_getLogs
    let logs: Vec<Log> = task::block_on(eth_api.get_logs(json!({
    "blockHash": "0x468f88ed8b40d940528552f093a11e4eb05991c787608139c931b0e9782ec5af",
    "topics": ["0xa61b5dec2abee862ab0841952bfbc161b99ad8c14738afa8ed8d5c522cd03946"]
    })))?;
    println!("Logs => {:?}", logs);

    // eth_call
    let contract: Address = from_str(r#""0x2736D225f85740f42D17987100dc8d58e9e16252""#).unwrap(); // cannot fail
    let mut abi = abi::In3EthAbi::new();
    let params = task::block_on(abi.encode("totalServers():uint256", json!([])))
        .expect("failed to ABI encode params");

    let txn = CallTransaction {
        to: Some(contract),
        data: Some(params),
        ..Default::default()
    };
    let output: Bytes =
        task::block_on(eth_api.call(txn, BlockNumber::Latest)).expect("ETH call failed");
    let output =
        task::block_on(abi.decode("uint256", output)).expect("failed to ABI decode output");
    let total_servers: U256 = from_value(output).unwrap(); // cannot fail if ABI decode succeeds
    println!("Total servers => {:?}", total_servers);

    Ok(())
}
