#include <eosio/eosio.hpp>
#include <eosio/system.hpp>

using namespace eosio;

// btc table
  struct[[ eosio::table("btcprice"), eosio::contract("cronacle") ]] btctick {
    time_point  ticktime;
    uint32_t    usdprice;

    uint64_t primary_key() const { return ticktime.sec_since_epoch(); }
  };
  using btcprice_index = eosio::multi_index<"btcprice"_n, btctick>;

  // user table
  struct[[ eosio::table("users"), eosio::contract("cronacle") ]] user {
    time_point  time;
    name        proton_account;
    std::string dfinity_principal;

    uint64_t primary_key() const { return proton_account.value; }
  };
  using users_index = eosio::multi_index<"users"_n, user>;