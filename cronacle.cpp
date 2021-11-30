#include <eosio/eosio.hpp>
#include <eosio/system.hpp>

#include "cronacle.hpp"

using namespace eosio;
using namespace std;

const std::string VERSION = "0.1.0";

class [[eosio::contract("cronacle")]] cronacle : public eosio::contract {
public:

  cronacle(name receiver, name code,  datastream<const char*> ds): contract(receiver, code, ds) {}

  [[eosio::action]]
  void version() {

    string version_message = "Version = " + VERSION;

    check(false, version_message);
  }

  [[eosio::action]]
  void init(time_point iterations_start) {

  require_auth(get_self());

  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  if (system_iterator == system_table.end()) {
    // insert system record
    system_table.emplace(get_self(), [&](auto &sys) {
      sys.init = iterations_start;
      sys.cls = asset(0, POINT_CURRENCY_SYMBOL);
      });
  } else {
    // modify system record
    system_table.modify(system_iterator, _self, [&](auto &sys) { sys.init = iterations_start; });
  }

}

void reguser(name user) {

  require_auth(user);

  // is the user already registered?
  // find the account in the user table
  users_index users_table(get_self(), user.value);
  auto user_iterator = users_table.begin();

  check(user_iterator == users_table.end(), "user is already registered");

  // add record to the users table
  users_table.emplace(get_self(), [&](auto &u) {  
    u.time = current_time_point();  
    u.proton_account = user;
  });

  // update the system record - number of users and CLS
  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  if (system_iterator == system_table.end()) {
    // emplace
    system_table.emplace(
        get_self(), [&](auto &sys) {
          sys.init = current_time_point();
          sys.usercount = 1;
          sys.cls = UCLS; // the CLS for the first verified user          
        });
  } else {
    // modify
    system_table.modify(system_iterator, _self, [&](auto &sys) {
      sys.usercount += 1;
      sys.cls += UCLS; // add to the CLS for the verified user
    });
  }
}

  [[eosio::action]]
  void storebtc(uint32_t btcprice) {
    time_point now = current_time_point();

    // open the btcprice table
    btcprice_index btcprice_table(get_self(), get_self().value);
    
    // add the btc price tick
    btcprice_table.emplace(get_self(), [&](auto &btc) { btc.ticktime = now; btc.usdprice = btcprice; });
  }

  using version_action = action_wrapper<"version"_n, &cronacle::version>;

  [[eosio::action]]
  void storeid(name user, std::string principal) {
    time_point now = current_time_point();

    // open the user table
    users_index users_table(get_self(), get_self().value);

    auto user_iterator = users_table.find(user.value);

    if (user_iterator != users_table.end()) {
      // modify
      users_table.modify(user_iterator, get_self(), [&](auto &usr) { usr.time = now; usr.dfinity_principal = principal; });
    } else {
      // emplace
      users_table.emplace(get_self(), [&](auto &usr) { usr.time = now; usr.proton_account = user; usr.dfinity_principal = principal; });
    }

    
  }

private:

  
};