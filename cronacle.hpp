#include <eosio/eosio.hpp>
#include <eosio/system.hpp>
#include <eosio/asset.hpp>

using namespace eosio;

// SYSTEM
// system table
struct[[ eosio::table("system"), eosio::contract("cronacle") ]] system_record {
time_point init;
uint16_t iteration;
uint32_t usercount;
uint32_t claimevents;
uint32_t participants;
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