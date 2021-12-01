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

// CREDITS
struct[[ eosio::table("credits"), eosio::contract("cronacle") ]] credit {
asset amount;
uint64_t primary_key() const { return 0; }  // ensures single record per user
};
using credits_index = eosio::multi_index<"credits"_n, credit>;