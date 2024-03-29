#include <eosio/eosio.hpp>
#include <eosio/system.hpp>
#include <eosio/asset.hpp>

using namespace eosio;
using namespace std;

const string POINT_CURRENCY_CODE = "POINT";
const uint8_t POINT_CURRENCY_PRECISION = 4;
const symbol POINT_CURRENCY_SYMBOL = symbol(POINT_CURRENCY_CODE, POINT_CURRENCY_PRECISION);

const std::string CREDIT_CURRENCY_CONTRACT = "freeostokens";

// atomicassets constants
const name nft_account = name("atomicassets");

// User contribution to Conditionally Limited Supply
const asset UCLS = asset(1000000, POINT_CURRENCY_SYMBOL);


// SYSTEM
// system table
struct[[ eosio::table("system"), eosio::contract("cronacle") ]] system_record {
time_point init;
uint32_t usercount;
asset cls;

uint64_t primary_key() const { return 0; } // return a constant to ensure a single-row table
};
using system_index = eosio::multi_index<"system"_n, system_record>;


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
    time_point  bidding_end;
    time_point  end;
    name        winner;
    asset       bidamount;

    uint64_t primary_key() const { return number; }
    uint64_t get_secondary() const { return nftid; }
    uint64_t get_tertiary() const { return winner.value; }
};
using auctions_index = eosio::multi_index<"auctions"_n, auction,
indexed_by<"bynftid"_n, const_mem_fun<auction, uint64_t, &auction::get_secondary>>,
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

// PARAMETERS
// parameters table
struct[[ eosio::table("parameters"), eosio::contract("cronacle") ]] parameter {
name paramname;
string value;

uint64_t primary_key() const { return paramname.value; }
};
using parameters_index = eosio::multi_index<"parameters"_n, parameter>;

// ADMIN WHITELIST
// admin accounts table - a whitelist of which accounts can perform privileged actions: e.g. addnft and removenft
struct[[ eosio::table("admins"), eosio::contract("cronacle") ]] admin_whitelist {
  name account;

  uint64_t primary_key() const { return account.value; }
};
using admins_index = eosio::multi_index<"admins"_n, admin_whitelist>;