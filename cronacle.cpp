#include <eosio/eosio.hpp>
#include <eosio/system.hpp>

#include "cronacle.hpp"

using namespace eosio;
using namespace std;

const std::string VERSION = "0.2.11";

class [[eosio::contract("cronacle")]] cronacle : public eosio::contract {
public:

  cronacle(name receiver, name code,  datastream<const char*> ds): contract(receiver, code, ds) {}


  // ACTION: VERSION
  [[eosio::action]]
  void version() {

    string version_message = "Version = " + VERSION;

    check(false, version_message);
  }


  // ACTION: INIT
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
    system_table.modify(system_iterator, _self, [&](auto &sys) { 
      sys.init = iterations_start;
      });
  }

}


// ACTION: REGUSER
[[eosio::action]]
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


  // ACTION: STOREBTC
  [[eosio::action]]
  void storebtc(uint32_t btcprice) {
    time_point now = current_time_point();

    // open the btcprice table
    btcprice_index btcprice_table(get_self(), get_self().value);
    
    // add the btc price tick
    btcprice_table.emplace(get_self(), [&](auto &btc) { btc.ticktime = now; btc.usdprice = btcprice; });
  }

  using version_action = action_wrapper<"version"_n, &cronacle::version>;


  // ACTION: STOREID
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


// FUNCTION: CREDIT
#ifdef PRODUCTION
[[eosio::on_notify("xtokens::transfer")]]
#else
[[eosio::on_notify("eosio.token::transfer")]]
#endif
void credit(name user, name to, asset quantity, std::string memo) {
  if (user == get_self()) {
      return;
    }

  // check the symbol
  check(quantity.symbol == CREDIT_CURRENCY_SYMBOL, ERR_CREDIT_CURRENCY_MESSAGE);

  check(to == get_self(), "recipient of credit is incorrect");

  // upsert the user's credit record
  credits_index credits_table(get_self(), user.value);
  auto credits_iterator = credits_table.begin();

  if (credits_iterator == credits_table.end()) {
    // emplace
    credits_table.emplace(get_self(), [&](auto &c) {
          c.amount = quantity;        
    });
  } else {
    // modify
    credits_table.modify(credits_iterator, _self, [&](auto &c) {
      c.amount += quantity;
    });
  }

}

// create_auction - create a new auction record based on system initialisation time, the current time and auction length
void create_current_auction(uint64_t nft_id) {

  // work out the start time from the system init time
  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  check(system_iterator != system_table.end(), "the system record is undefined");

  time_point init = system_iterator->init;

  uint64_t init_secs = init.sec_since_epoch();
  uint64_t now_secs = current_time_point().sec_since_epoch();

  uint64_t start_secs = now_secs - ((now_secs - init_secs) % AUCTION_LENGTH_SECONDS);
  uint64_t end_secs = start_secs + AUCTION_LENGTH_SECONDS;

  time_point start = time_point(seconds(start_secs));
  time_point end = time_point(seconds(end_secs));

  // calculate the number of the auction
  uint32_t last_number = 0;
  auctions_index auctions_table(get_self(), get_self().value);
  auto auction_iterator = auctions_table.rbegin();
  if (auction_iterator != auctions_table.rend()) {
    last_number = auction_iterator->number;
  }
  uint32_t next_number = last_number + 1;

  // write the record
  auctions_table.emplace(get_self(), [&](auto &a) {
    a.number = next_number;
    a.nft_id = nft_id;
    a.start = start;
    a.end = end;
  });

}


// is_auction_current - helper function to determine if an auction is ongoing
// side effect: creates the auction record if it doesn't already exist
bool is_auction_current(uint64_t nft_id) {
  auctions_index auctions_table(get_self(), get_self().value);
  auto nft_idx = auctions_table.get_index<"bynftid"_n>();

  auto auction_iterator = nft_idx.find(nft_id);
  if (auction_iterator == nft_idx.end()) {
    // the auction record is not found, so create it
    create_current_auction(nft_id);
    return true;
  } else {
    // the auction record exists, check that we are within start and end times
    time_point now = current_time_point();
    return (now >= auction_iterator->start && now <= auction_iterator->end) ? true : false;
  }

}

// ACTION: BID
[[eosio::action]]
void bid(name user, uint64_t nft_id, asset bidamount) {
  require_auth(user);

  // check if the system is open for business
  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  check(system_iterator != system_table.end(), "the system record is undefined");
  time_point init = system_iterator->init;
  check(current_time_point() >= init, "the auction system is not open for business");

  // check that the user is registered
  users_index users_table(get_self(), get_self().value);
  auto user_iterator = users_table.begin();
  check(user_iterator != users_table.end(), "you must be registered in order to bid");

  // check that the user has enough credit to support the bid
  credits_index credits_table(get_self(), user.value);
  auto credit_iterator = credits_table.begin();
  check(credit_iterator != credits_table.end(), "you do not have a credit balance");
  check(credit_iterator->amount >= bidamount, "you do not have sufficient credit to place your bid");

  // The user should be bidding on either:
  // 1. The first nft in the nfts table while the current auction is active
  // 2. The second nft in the nfts table. This signals that a new auction has begun.
  nfts_index nfts_table(get_self(), get_self().value);
  auto nft_iterator = nfts_table.begin();
  check(nft_iterator != nfts_table.end(), "no nft is offered for sale at this time");
  uint64_t first_nft = nft_iterator->nftid;

  if (nft_id == first_nft) {
    check(is_auction_current(first_nft), "bidding has ended on this nft");
  }
  
  // **********************

  // check that the first nft is still open for bidding
  auctions_index auctions_table(get_self(), get_self().value);
  auto auction_iterator = auctions_table.rbegin();

  // scenario 1: bidding on the first nft
  if (nft_id == first_nft) {
    if (auction_iterator == auctions_table.rend()) {
    // bidding on first nft but there is no auction record, so create it
    create_auction(nft_id);
    // ...and point the iterator to the new record
    auction_iterator = auctions_table.rbegin();
  } else {
    // there is an auction record - check that the auction is ongoing
    check(is_auction_current)
  }

  }

  
  // advance to the second nft
  //nft_iterator++;
  //check()

  


  

  // TODO: housekeeping - tidy up after the previous auction
  tick();



  bids_index bids_table(get_self(), get_self().value);
  // count the number of bids
  uint8_t bids_count = 0;
  auto bids_itr = bids_table.begin();
  while (bids_itr != bids_table.end()) {
    bids_count++;
    bids_itr++;
  }

  // find the winning bid
  auto amt_idx = bids_table.get_index<"byamount"_n>();
  auto amt_itr = amt_idx.end();
  amt_itr--;

  asset bid_to_beat = asset(0, CREDIT_CURRENCY_SYMBOL); // zero bid
  if (amt_itr != amt_idx.cend()) {
    bid_to_beat = amt_itr->bidamount;
  }

  /* debugging code: 

  const string bidstr = bidamount.to_string();
  const string incrstr = smallest_increment.to_string();
  const string bidtobeatstr = bid_to_beat.to_string();
  const string diffstr = (bidamount - bid_to_beat).to_string();
  string result = "";
  if ((bidamount - bid_to_beat) >= smallest_increment) {
    result = "success";
  } else {
    result = "fail";
  }
  const string debugstr = "bid: " + bidstr + " increment: "
   + incrstr + " bid to beat: " + bidtobeatstr + " difference: " + diffstr
   + " result: " + result;

  check(false, debugstr);

  end of debugging code */
  

  const string bid_error = "you must start bidding with, or increase the highest bid by at least " + BID_INCREMENT.to_string();
  check((bidamount > bid_to_beat) && ((bidamount - bid_to_beat) >= BID_INCREMENT), bid_error);

  // check if we are replacing a previous bid by the same user
  auto userbid_itr = bids_table.find(user.value);
  if (userbid_itr != bids_table.end()) {
    // modify user's bid
    bids_table.modify(userbid_itr, get_self(), [&](auto &b) {
      b.bidamount = bidamount;
    });
  } else {
    // at this point we are adding a top bid that from a user who has not bid before
    // drop the lowest bid from the bids table (if necessary) and emplace the new bid

    // TODO: if there are 3 bids then erase the lowest one
    if (bids_count == 3) {
      auto lowest_bid_itr = amt_idx.begin();
      amt_idx.erase(lowest_bid_itr);
    }

    // add new top bid
    bids_table.emplace(
      get_self(), [&](auto &b) {
        b.bidtime = current_time_point();
        b.bidder = user;
        b.bidamount = bidamount;
        b.nftid = nft_id;        
      });

  }

}

// which auction are we in?
uint16_t current_auction() {

  uint16_t iteration = 0;

  // get the start of freeos system time
  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  check(system_iterator != system_table.end(), "system record is undefined");
  time_point init = system_iterator->init;

  // how far are we into the current iteration?
  uint64_t now_secs = current_time_point().sec_since_epoch();
  uint64_t init_secs = init.sec_since_epoch();

  if (now_secs >= init_secs) {
    iteration = ((now_secs - init_secs) / AUCTION_LENGTH_SECONDS) + 1;
  }
  
  return iteration;
}

// ACTION
void tick() {

  // do we need to tidy up after the previous auction has closed?
  time_point now = current_time_point();

  // get the current auction record
  // look for highest auction number - check if it is still active
  auctions_index auctions_table(get_self(), get_self().value);
  auto latest_itr = auctions_table.rbegin();

  if (latest_itr != auctions_table.rend()) {
    // there is an existing auction record, so clean up if necessary

    // are
  }


  /* are we on a new auction?
  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  check(system_iterator != system_table.end(), "system record is undefined");

  uint16_t recorded_auction = system_iterator->auction;
  uint16_t actual_auction = current_auction();

  if (recorded_auction != actual_auction) {
    // update the recorded iteration
    system_table.modify(system_iterator, _self, [&](auto &sys) {
      sys.auction = actual_auction;
    });

    // run the new auction service routine
    trigger_new_auction(actual_auction);
  }
  */
}


// ACTION: CLAIM
[[eosio::action]]
void claim(name user) {
  require_auth(user);

  // check that the auction claim time has been reached
 
}


// ACTION: AUCTION
[[eosio::action]]
void auction(
    uint32_t number,
    uint64_t nftid,
    time_point start,
    time_point end,
    name winner,
    asset bidamount) {

  
  // check to see if an auction is already underway
  //time_point now = current_time_point();

  // look for highest auction number - check if it is still active
  auctions_index auctions_table(get_self(), get_self().value);
  //auto latest_itr = auctions_table.rbegin();

  //if (latest_itr == auctions_table.rend()) {
    // table is empty, so create
    auctions_table.emplace(
      get_self(), [&](auto &a) {
        a.number = number;
        a.nftid = nftid;
        a.start = start;
        a.end = end;
        a.winner = winner;
        a.bidamount = bidamount;
      });
  //} else {
  //  check(false, latest_itr->number);
  //}

}


void trigger_new_auction(uint16_t auction) {

}

// ACTION: MAINTAIN
[[eosio::action]]
void maintain(string action, name user) {

  require_auth(get_self());

  if (action == "set cls") {
    system_index system_table(get_self(), get_self().value);
    auto system_iterator = system_table.begin();

    system_table.modify(system_iterator, get_self(), [&](auto &s) {
      s.cls = asset(1000000, POINT_CURRENCY_SYMBOL);
    });
  }

  if (action == "clear users") {
      users_index users_table(get_self(), get_self().value);
      auto user_iterator = users_table.begin();

      while (user_iterator != users_table.end()) {
        user_iterator = users_table.erase(user_iterator);
      }
    }

    if (action == "clear system") {
      system_index system_table(get_self(), get_self().value);
      auto system_iterator = system_table.begin();
      system_table.erase(system_iterator);
    }

    if (action == "set system") {
      system_index system_table(get_self(), get_self().value);
      auto system_iterator = system_table.begin();

      if (system_iterator == system_table.end()) {
        system_table.emplace(get_self(), [&](auto &sys) {
          // sys.init = time_point("2021-12-05T00:00:00.000");
          sys.usercount = 1;
          sys.cls = asset(1000000, POINT_CURRENCY_SYMBOL);          
        });
      } else {
        system_table.modify(system_iterator, get_self(), [&](auto &sys) {
          // sys.auction = current_auction();
          sys.usercount = 1;
          sys.cls = asset(1000000, POINT_CURRENCY_SYMBOL);
        });
      }
    }

    if (action == "clear auctions") {
      auctions_index auctions_table(get_self(), get_self().value);
      auto auctions_iterator = auctions_table.begin();

      while (auctions_iterator != auctions_table.end()) {
        auctions_iterator = auctions_table.erase(auctions_iterator);
      }
    }

}


// ACTION: ADDNFT
[[eosio::action]]
void addnft(uint64_t nftid) {

  require_auth(get_self());

  uint32_t last_number = 0;

  // get the current highest number - or 0 if the nft table is empty
  nfts_index nfts_table(get_self(), get_self().value);
  auto latest_itr = nfts_table.rbegin();

  if (latest_itr != nfts_table.rend()) {
    last_number = latest_itr->number;
  }

  // add the nft to the table
  nfts_table.emplace(get_self(), [&](auto &n) {
    n.number = last_number + 1;
    n.nftid = nftid;
  });
}



private:

  
};