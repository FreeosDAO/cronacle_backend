#include <eosio/eosio.hpp>
#include <eosio/system.hpp>
#include <eosio/asset.hpp>

using namespace eosio;
using namespace std;

const string POINT_CURRENCY_CODE = "POINT";
const uint8_t POINT_CURRENCY_PRECISION = 4;
const symbol POINT_CURRENCY_SYMBOL = symbol(POINT_CURRENCY_CODE, POINT_CURRENCY_PRECISION);

#ifdef PRODUCTION
const std::string CREDIT_CURRENCY_CODE = "FOOBAR";
const std::string ERR_CREDIT_CURRENCY_MESSAGE = "you must credit your account with FOOBAR";
const uint8_t CREDIT_CURRENCY_PRECISION = 6;
const symbol CREDIT_CURRENCY_SYMBOL = symbol(CREDIT_CURRENCY_CODE, CREDIT_CURRENCY_PRECISION);
const std::string CREDIT_CURRENCY_CONTRACT = "xtokens";
#else
const std::string CREDIT_CURRENCY_CODE = "XPR";
const std::string ERR_CREDIT_CURRENCY_MESSAGE = "you must credit your account with XPR";
const uint8_t CREDIT_CURRENCY_PRECISION = 4;
const symbol CREDIT_CURRENCY_SYMBOL = symbol(CREDIT_CURRENCY_CODE, CREDIT_CURRENCY_PRECISION);
const std::string CREDIT_CURRENCY_CONTRACT = "eosio.token";
#endif

// User contribution to Conditionally Limited Supply
const asset UCLS = asset(1000000, POINT_CURRENCY_SYMBOL);

// timing system
const uint32_t AUCTION_LENGTH_SECONDS = 600;    // 10 minutes
const uint32_t AUCTION_START = 0;
const uint32_t AUCTION_END = 540;

#ifdef PRODUCTION
  asset BID_INCREMENT = asset(20000000, CREDIT_CURRENCY_SYMBOL);
#else
  asset BID_INCREMENT = asset(200000, CREDIT_CURRENCY_SYMBOL);
#endif


// SYSTEM
// system table
struct[[ eosio::table("system"), eosio::contract("cronacle") ]] system_record {
time_point init;
uint32_t usercount;
asset cls;

uint64_t primary_key() const { return 0; } // return a constant to ensure a single-row table
};
using system_index = eosio::multi_index<"system"_n, system_record>;


// BTC
struct[[ eosio::table("btcprice"), eosio::contract("cronacle") ]] btctick {
    time_point  ticktime;
    uint32_t    usdprice;

    uint64_t primary_key() const { return ticktime.sec_since_epoch(); }
};
using btcprice_index = eosio::multi_index<"btcprice"_n, btctick>;


// USERS
struct[[ eosio::table("users"), eosio::contract("cronacle") ]] user {
    time_point  time;
    name        proton_account;
    std::string dfinity_principal;
uint64_t primary_key() const { return proton_account.value; }
};
using users_index = eosio::multi_index<"users"_n, user>;


// CREDITS
struct[[ eosio::table("credits"), eosio::contract("cronacle") ]] credit {
    asset amount;
    uint64_t primary_key() const { return 0; }  // ensures single record per user
};
using credits_index = eosio::multi_index<"credits"_n, credit>;


// BIDS - contains top 3 bids
struct[[ eosio::table("bids"), eosio::contract("cronacle") ]] userbid {
    time_point  bidtime;
    name        bidder;
    asset       bidamount;
    uint64_t    nftid;

    uint64_t primary_key() const { return bidder.value; }
    uint64_t get_secondary() const { return bidamount.amount; }
};
using bids_index = eosio::multi_index<"bids"_n, userbid,
indexed_by<"byamount"_n, const_mem_fun<userbid, uint64_t, &userbid::get_secondary>>>;


// AUCTIONS
struct[[ eosio::table("auctions"), eosio::contract("cronacle") ]] auction {
    uint32_t    number;
    uint64_t    nftid;
    time_point  start;
    time_point  end;
    name        winner;
    asset       bidamount;

    uint64_t primary_key() const { return number; }
    uint64_t get_secondary() const { return nftid; }
    uint64_t get_tertiary() const { return winner.value; }
};
using auctions_index = eosio::multi_index<"auctions"_n, auction,
indexed_by<"byndftid"_n, const_mem_fun<auction, uint64_t, &auction::get_secondary>>,
indexed_by<"bywinner"_n, const_mem_fun<auction, uint64_t, &auction::get_tertiary>>
>;

// NFTs for offer
struct[[ eosio::table("nfts"), eosio::contract("cronacle") ]] nft {
    uint32_t    number;
    uint64_t    nftid;
    
    uint64_t primary_key() const { return number; }
    uint64_t get_secondary() const { return nftid; }
};
using nfts_index = eosio::multi_index<"nfts"_n, nft,
indexed_by<"bynftid"_n, const_mem_fun<nft, uint64_t, &nft::get_secondary>>>;
