#include <eosio/eosio.hpp>
#include <eosio/system.hpp>

#include "cronacle.hpp"

using namespace eosio;
using namespace std;

const std::string VERSION = "0.7.0";

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
  void init(time_point auctions_start) {

  require_auth(get_self());

  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  if (system_iterator == system_table.end()) {
    // insert system record
    system_table.emplace(get_self(), [&](auto &sys) {
      sys.init = auctions_start;
      sys.cls = asset(0, POINT_CURRENCY_SYMBOL);
      });
  } else {
    // modify system record
    system_table.modify(system_iterator, _self, [&](auto &sys) { 
      sys.init = auctions_start;
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


[[eosio::action]]
void withdraw(name user) {

  require_auth(user);

  // calculate unavailable credit i.e. if the user has a currently winning bid in place
  asset bid_amount = asset(0, CREDIT_CURRENCY_SYMBOL);
  bids_index bids_table(get_self(), get_self().value);

  // find the winning bid
  auto bid_amt_idx = bids_table.get_index<"byamount"_n>();
  auto winning_bid_itr = bid_amt_idx.rbegin();

  if (winning_bid_itr != bid_amt_idx.rend()) {
    if (user == winning_bid_itr->bidder) {
      bid_amount = winning_bid_itr->bidamount;
    }
  }

  // get the user's credit balance
  credits_index credits_table(get_self(), user.value);
  auto credit_iterator = credits_table.begin();
  check(credit_iterator != credits_table.end(), "you do not have a credit balance");
  asset credit_amount = credit_iterator->amount;



  // user cannot withdraw the amount for the winning bid, so subtract from credit amount
  check(credit_amount > bid_amount, "you do not have sufficient credit to withdraw");
  asset withdrawal_amount = credit_amount - bid_amount;

  action transfer = action(
      permission_level{get_self(), "active"_n},
      name("xtokens"),
      "transfer"_n,
      std::make_tuple(get_self(), user, withdrawal_amount, std::string("withdraw auction credit")));

    transfer.send();

  // adjust the user's credit balance
  credits_table.modify(credit_iterator, get_self(), [&](auto &c) {
      c.amount -= withdrawal_amount;
  });

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
void create_auction(uint64_t nft_id) {

  // work out the start time from the system init time
  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  check(system_iterator != system_table.end(), "the system record is undefined");

  time_point init = system_iterator->init;

  uint64_t init_secs = init.sec_since_epoch();
  uint64_t now_secs = current_time_point().sec_since_epoch();

  uint64_t start_secs = now_secs - ((now_secs - init_secs) % AUCTION_LENGTH_SECONDS);
  uint64_t bidding_end_secs = start_secs + AUCTION_BIDDING_PERIOD_SECONDS;
  uint64_t end_secs = start_secs + AUCTION_LENGTH_SECONDS;

  time_point start = time_point(seconds(start_secs));
  time_point bidding_end = time_point(seconds(bidding_end_secs));
  time_point end = time_point(seconds(end_secs)); // TODO: adjust to 1 microsecond before this to ensure no overlap with next auction

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
    a.nftid = nft_id;
    a.start = start;
    a.bidding_end = bidding_end;
    a.end = end;
  });

}


// add_bid - helper function to add a bid to the bids table
void add_bid(name user, uint64_t nft_id, asset bidamount) {

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
  auto amt_itr = amt_idx.rbegin();

  asset bid_to_beat = asset(0, CREDIT_CURRENCY_SYMBOL); // initialise to zero bid
  if (amt_itr != amt_idx.rend()) {
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
  
  asset minimum_next_bid = bid_to_beat + BID_INCREMENT;
  const string bid_amount_msg = "the highest bid is currently " + bid_to_beat.to_string() + ". you must bid at least " + minimum_next_bid.to_string();
  check(bidamount >= minimum_next_bid, bid_amount_msg);

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


// close_auction - helper function to close an existing auction
void close_auction(uint64_t nft_id) {

  // find the winning bid
  bids_index bids_table(get_self(), get_self().value);
  auto amt_idx = bids_table.get_index<"byamount"_n>();
  auto bid_itr = amt_idx.rbegin();
  check(bid_itr != amt_idx.rend(), "there is no winning bid");

  name winner = bid_itr->bidder;
  asset bidamount = bid_itr->bidamount;

  // transfer nft to the winner
  vector <uint64_t> nftids;
  nftids.push_back(nft_id);
  string memo = "winner of auction for nft " + to_string(nft_id);

  action transfer = action(
      permission_level{get_self(), "active"_n},
      nft_account,
      "transfer"_n,
      std::make_tuple(get_self(), winner, nftids, memo));

    transfer.send();

  // reduce the winner's credit by the bid amount
  credits_index credit_table(get_self(), winner.value);
  auto credit_iterator = credit_table.begin();
  check(credit_iterator != credit_table.end(), "winning bidder does not have a credit record");
  check(credit_iterator->amount >= bidamount, "winning bidder does not have sufficient credit");
  credit_table.modify(credit_iterator, get_self(), [&](auto &c) {
      c.amount -= bidamount;
    });

  // record the winner and winning bid in the latest auction record
  auctions_index auctions_table(get_self(), get_self().value);
  auto auction_iterator = auctions_table.rbegin();
  check(auction_iterator != auctions_table.rend(), "auction record is undefined");
  auto latest_auction_iterator = auction_iterator.base();
  latest_auction_iterator--;

  auctions_table.modify(latest_auction_iterator, get_self(), [&](auto &a) {
    a.winner = winner;
    a.bidamount = bidamount;
  });
  
  // clear bids table
  auto bid_iterator = bids_table.begin();
  while (bid_iterator != bids_table.end()) {
    bid_iterator = bids_table.erase(bid_iterator);
  }

  // delete the nft record
  nfts_index nfts_table(get_self(), get_self().value);
  auto nft_iterator = nfts_table.begin();
  check(nft_iterator != nfts_table.end(), "nft record is undefined");
  nfts_table.erase(nft_iterator);

}

// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID
// BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID BID

// ACTION: BID
[[eosio::action]]
void oldbid(name user, uint64_t nft_id, asset bidamount) {
  require_auth(user);

  // check that the user is registered
  users_index users_table(get_self(), user.value);
  auto user_iterator = users_table.begin();
  check(user_iterator != users_table.end(), "you must be registered in order to bid");

  // check if the system is open for business
  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  check(system_iterator != system_table.end(), "the system record is undefined");
  time_point init = system_iterator->init;
  check(current_time_point() >= init, "the auction system is not open for business");

  // check that the user has enough credit to support the bid
  credits_index credits_table(get_self(), user.value);
  auto credit_iterator = credits_table.begin();
  check(credit_iterator != credits_table.end(), "you do not have a credit balance");
  check(credit_iterator->amount >= bidamount, "you do not have sufficient credit to place your bid");

  // The user should be bidding on either:
  // 1. The first nft in the nfts table while the current auction is active
  // 2. The second nft in the nfts table. This signals that a new auction has begun

  // what are the first and second nft_ids?
  // If there is no first nft record then nothing is on offer at this time
  // Second nft_id = 0 if there is no second nft record
  nfts_index nfts_table(get_self(), get_self().value);
  auto nft_iterator = nfts_table.begin();
  check(nft_iterator != nfts_table.end(), "no nft is offered for sale at this time");
  uint64_t first_nft = nft_iterator->nftid;
  nft_iterator++;
  uint64_t second_nft = (nft_iterator != nfts_table.end()) ? nft_iterator->nftid : 0;

  // auction/bid algorithm *******************************
  time_point now = current_time_point();
  const string no_bid_msg = "bidding is not open on this nft";

  // check that bidding is for the first or second nft
  check(nft_id == first_nft || nft_id == second_nft, no_bid_msg);

  // is the latest auction record for the nft?
  auctions_index auctions_table(get_self(), get_self().value);
  auto nft_idx = auctions_table.get_index<"bynftid"_n>();
  auto auction_iterator = nft_idx.find(nft_id);

  if (auction_iterator != nft_idx.end()) {
    // Yes, there is an auction record for the bid nft
    // check if bidding is still open; if yes then add the bid
    // check(now >= auction_iterator->start && now <= auction_iterator->bidding_end, "bidding has ended for this nft");
    if (now >= auction_iterator->start && now <= auction_iterator->bidding_end) {
      add_bid(user, nft_id, bidamount);
    }

  } else {
    // No, there is no auction record for the nft

    if (nft_id == first_nft) {
      // create the auction for the first nft
      create_auction(nft_id);

      // add the bid
      add_bid(user, nft_id, bidamount);

    } else {
      // the bid is for the second nft

      // is there an ongoing auction for the first nft? -- i.e. THE WHOLE PERIOD (bidding period + gap)
      auto first_nft_iterator = nft_idx.find(first_nft);
      check(first_nft_iterator != nft_idx.end(), no_bid_msg);
      bool first_auction_ongoing = (now >= first_nft_iterator->start && now <= first_nft_iterator->end) ? true : false;
      check(!first_auction_ongoing, no_bid_msg);

      // valid bid for second nft, which means that bidding for the first nft has ended
      close_auction(first_nft); // clear bids table + close auction record

      // create an auction record for the second nft
      create_auction(nft_id);

      // add the user bid
      add_bid(user, nft_id, bidamount);
    }
  }
}


// ACTION: BID
[[eosio::action]]
void bid(name user, uint64_t nft_id, asset bidamount) {
  require_auth(user);

  // check that the user is registered
  users_index users_table(get_self(), user.value);
  auto user_iterator = users_table.begin();
  check(user_iterator != users_table.end(), "you must be registered in order to bid");

  // check if the system is open for business
  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  check(system_iterator != system_table.end(), "the system record is undefined");
  time_point init = system_iterator->init;
  check(current_time_point() >= init, "the auction system is not open for business");

  // check that the user has enough credit to support the bid
  credits_index credits_table(get_self(), user.value);
  auto credit_iterator = credits_table.begin();
  check(credit_iterator != credits_table.end(), "you do not have a credit balance");
  check(credit_iterator->amount >= bidamount, "you do not have sufficient credit to place your bid");

  // The user should be bidding on either:
  // 1. The first nft in the nfts table while the current auction is active
  // 2. The second nft in the nfts table. This signals that a new auction has begun

  // what are the first and second nft_ids?
  // If there is no first nft record then nothing is on offer at this time
  // Second nft_id = 0 if there is no second nft record
  nfts_index nfts_table(get_self(), get_self().value);
  auto nft_iterator = nfts_table.begin();
  check(nft_iterator != nfts_table.end(), "no nft is offered for sale at this time");
  uint64_t first_nft = nft_iterator->nftid;
  nft_iterator++;
  uint64_t second_nft = (nft_iterator != nfts_table.end()) ? nft_iterator->nftid : 0;

  // auction/bid algorithm *******************************
  time_point now = current_time_point();
  const string no_bid_msg = "bidding is not open on this nft";

  // check that bidding is for the first or second nft
  check(nft_id == first_nft || nft_id == second_nft, no_bid_msg);

  // get the latest auction record
  // IF it exists AND it is for the nftid being bid, allow bid if time open
  auctions_index auctions_table(get_self(), get_self().value);
  auto auction_iterator = auctions_table.rbegin();
  if (auction_iterator != auctions_table.rend() && auction_iterator->nftid == nft_id) {
    if (now >= auction_iterator->start && now <= auction_iterator->bidding_end) {
      add_bid(user, nft_id, bidamount);
    }  

  } else {
    // No, there is no auction record for the nft

    if (nft_id == first_nft) {
      // create the auction for the first nft
      create_auction(nft_id);

      // add the bid
      add_bid(user, nft_id, bidamount);

    } else {
      // the bid is for the second nft

      // is there an ongoing auction for the first nft? -- i.e. THE WHOLE PERIOD (bidding period + gap)
      auctions_index auctions_table(get_self(), get_self().value);
      auto latest_auction_iterator = auctions_table.rbegin();

      check(latest_auction_iterator != auctions_table.rend(), no_bid_msg);
      bool first_auction_ongoing = (latest_auction_iterator->nftid == first_nft && now >= latest_auction_iterator->start && now <= latest_auction_iterator->end) ? true : false;
      check(!first_auction_ongoing, no_bid_msg);

      // valid bid for second nft, which means that bidding for the first nft has ended
      close_auction(first_nft); // clear bids table + close auction record

      // create an auction record for the second nft
      create_auction(nft_id);

      // add the user bid
      add_bid(user, nft_id, bidamount);
    }
  }
}

// ACTION
void tick() {

  // TODO

}


// ACTION: CLAIM
[[eosio::action]]
void claim(name user) {

  // no assert messages, fail silently

  require_auth(user);

  // get the latest auction record
  auctions_index auctions_table(get_self(), get_self().value);
  auto latest_auction_itr = auctions_table.rbegin();

  // if no latest auction record then return silently
  if (latest_auction_itr == auctions_table.rend()) return;

  // check that the auction bidding has finished
  time_point now = current_time_point();

  // if the bidding is ongoing then return silently
  if (now < latest_auction_itr->bidding_end) return;

  // get the winning bid
  bids_index bids_table(get_self(), get_self().value);
  auto amt_idx = bids_table.get_index<"byamount"_n>();
  auto amt_itr = amt_idx.rbegin();
  
  // if no winning bid then return silently
  if (amt_itr == amt_idx.rend()) return;

  // check if the user is the winner
  if (user != amt_itr->bidder) return;

  // get the nft id
  uint64_t nftid = amt_itr->nftid;

  // close the auction and transfer ownership of the nft to the user
  close_auction(nftid);
 
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
          sys.usercount = 1;
          sys.cls = asset(1000000, POINT_CURRENCY_SYMBOL);          
        });
      } else {
        system_table.modify(system_iterator, get_self(), [&](auto &sys) {
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

    if (action == "clear bids") {
      bids_index bids_table(get_self(), get_self().value);
      auto bids_iterator = bids_table.begin();

      while (bids_iterator != bids_table.end()) {
        bids_iterator = bids_table.erase(bids_iterator);
      }
    }

    if (action == "add bids") {
      bids_index bids_table(get_self(), get_self().value);

      bids_table.emplace(get_self(), [&](auto &b) {
          b.bidtime = current_time_point();
          b.bidder = name("alanappleton");
          b.bidamount = asset(2000000, CREDIT_CURRENCY_SYMBOL);
          b.nftid = 4398046576805;
        });

        /*
        bids_table.emplace(get_self(), [&](auto &b) {
          b.bidtime = current_time_point();
          b.bidder = name("billbeaumont");
          b.bidamount = asset(3000000, CREDIT_CURRENCY_SYMBOL);
          b.nftid = 4398046576805;
        });
        */

      bids_table.emplace(get_self(), [&](auto &b) {
          b.bidtime = current_time_point();
          b.bidder = name("celiacollins");
          b.bidamount = asset(1000000, CREDIT_CURRENCY_SYMBOL);
          b.nftid = 4398046576805;
        });
    }

    if (action == "highest bid") {
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
      auto amt_itr = amt_idx.rbegin();

      asset bid_to_beat = asset(0, CREDIT_CURRENCY_SYMBOL); // initialise to zero bid
      if (amt_itr != amt_idx.rend()) {
        bid_to_beat = amt_itr->bidamount;
      }

      string msg = "number of bids = " + to_string(bids_count) + ", winning bid = " + bid_to_beat.to_string();
      check(false, msg);
    }

    if (action == "reset") {
      // clear bids
      bids_index bids_table(get_self(), get_self().value);
      auto bids_iterator = bids_table.begin();

      while (bids_iterator != bids_table.end()) {
        bids_iterator = bids_table.erase(bids_iterator);
      }

      // clear auctions
      auctions_index auctions_table(get_self(), get_self().value);
      auto auctions_iterator = auctions_table.begin();

      while (auctions_iterator != auctions_table.end()) {
        auctions_iterator = auctions_table.erase(auctions_iterator);
      }
    }

}


// ACTION: ADDNFT
[[eosio::action]]
void addnft(name user, uint32_t number, uint64_t nftid) {

  require_auth(user);

  check(user == get_self() || user == name("freeospromo"), "addnft requires a privileged account");

  nfts_index nfts_table(get_self(), get_self().value);

  // check if the nft is not already in the list
  auto nft_idx = nfts_table.get_index<"bynftid"_n>();
  auto nft_present_itr = nft_idx.find(nftid);
  check(nft_present_itr == nft_idx.end(), "nft is already in the table");

  // if 0 passed as the number, then add to the end of the list
  if (number == 0) {    
    // get the current highest number and add 1
    auto latest_itr = nfts_table.rbegin();

    if (latest_itr != nfts_table.rend()) {
      number = latest_itr->number + 1;
    } else {
      number = 1;
    }
  }

  // add the nft to the table
  nfts_table.emplace(get_self(), [&](auto &n) {
    n.number = number;
    n.nftid = nftid;
  });
}

// ACTION: REMOVENFT
[[eosio::action]]
void removenft(name user, uint32_t number) {

  require_auth(user);

  check(user == get_self() || user == name("freeospromo"), "removenft requires a privileged account");

  // find the nft and erase it
  nfts_index nfts_table(get_self(), get_self().value);
  auto nft_itr = nfts_table.find(number);

  check(nft_itr != nfts_table.end(), "nft number not found");
  nfts_table.erase(nft_itr);
}

};