#include <eosio/eosio.hpp>
#include <eosio/system.hpp>
#include <eosio/asset.hpp>
#include <stdlib.h>

#include "cronacle.hpp"

using namespace eosio;
using namespace std;

const std::string VERSION = "0.13.0";

class [[eosio::contract("cronacle")]] cronacle : public eosio::contract {
public:

  cronacle(name receiver, name code,  datastream<const char*> ds): contract(receiver, code, ds) {}

  /**
   * version action prints the version of the contract
   */
  [[eosio::action]]
  void version() {
    extended_symbol currency = get_currency();
    symbol currency_symbol = currency.get_symbol();
    unsigned multiplier;

    multiplier = intPower(10, currency_symbol.precision());

    string version_message = "Version = " + VERSION
     + " (" + to_string(currency_symbol.precision())
     + "," + currency_symbol.code().to_string()
     + "," + currency.get_contract().to_string()
     + ")";

    check(false, version_message);
  }

  
  /**
   * init action is called by the contract owner to set the start time of the auctions
   * 
   * @pre requires authority of the contract
   * 
   * @param auctions_start The time when the first auction starts.
   */
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



/**
 * reguser function is called by the credit function after notification of a transfer of FREEOS to the contract.
 * On receipt of credit, adds a new user to the users table and updates the system table with the new user count and CLS
 * 
 * @param user the account name of the user making the transfer of tokens
 */
void reguser(name user) {

  // is the user already registered?
  // find the account in the user table
  users_index users_table(get_self(), user.value);
  auto user_iterator = users_table.begin();

  // if record already exists then nothing to do 
  if (user_iterator != users_table.end()) return;

  // it's a new user so add record to the users table
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

  using version_action = action_wrapper<"version"_n, &cronacle::version>;


/**
 * withdraw action returns the user's available credit balance
 * 
 * @param user the user who is withdrawing credit
 */
[[eosio::action]]
void withdraw(name user) {

  require_auth(user);

  asset zero_amount = asset(0, get_currency().get_symbol());

  asset withdrawal_amount = get_available_credit(user);

  check(withdrawal_amount > zero_amount, "you do not have credit to withdraw");

  action transfer = action(
      permission_level{get_self(), "active"_n},
      name(CREDIT_CURRENCY_CONTRACT),
      "transfer"_n,
      std::make_tuple(get_self(), user, withdrawal_amount, std::string("withdraw auction credit")));

    transfer.send();

  // adjust the user's credit balance
  credits_index credits_table(get_self(), user.value);
  auto user_credit_iterator = credits_table.begin();
  check(user_credit_iterator != credits_table.end(), "internal error, user's credit balance is undefined");
  credits_table.modify(user_credit_iterator, get_self(), [&](auto &c) {
      c.amount -= withdrawal_amount;
  });

}


/**
 * If the user sends a token other than FREEOS to the contract, the contract will reject the
 * transaction
 * 
 * @param user the account that sent the token
 * @param to The account that is receiving the tokens
 * @param quantity The amount of the asset being transferred.
 * @param memo The memo field is a string that is sent along with the transfer. It is not used by the
 * contract, but it is available for the user to send information to the contract.
 */
[[eosio::on_notify("xtokens::transfer")]]
void refusefoobar(name user, name to, asset quantity, std::string memo) {
  if (to == get_self()) {
    check(false, "The auction accepts FREEOS as credit");
  }
  
}


/**
 * credit is a notification function that adds the amount of credit received to the user's credit balance
 * It checks that the token is FREEOS and throws an assert error if not.
 * On success it updates the user's credit in the credits table.
 * 
 * @param user the account that sent the tokens
 * @param to The account that is receiving the credit
 * @param quantity The amount of credit being transferred.
 * @param memo The memo is a string that is passed along with the transfer. It is not used in this
 * contract, but it is a good idea to include it in your contract.
 * 
 * @return Nothing is being returned.
 */
[[eosio::on_notify("*::transfer")]]
void credit(name user, name to, asset quantity, std::string memo) {
  if (user == get_self()) {
      return;
    }

  // check the symbol
  extended_symbol currency = get_currency();
  symbol currency_symbol = currency.get_symbol();
  check(quantity.symbol == currency_symbol, "You must credit your account with " + currency_symbol.code().to_string());
  check(currency.get_contract() == get_first_receiver(), "source of token is not valid");

  check(to == get_self(), "recipient of credit is incorrect");

  // upsert the user's credit record
  credits_index credits_table(get_self(), user.value);
  auto credits_iterator = credits_table.begin();

  if (credits_iterator == credits_table.end()) {
    // emplace
    credits_table.emplace(get_self(), [&](auto &c) {
          c.amount = quantity;        
    });

    // add user to the users table (auto-registration)
    reguser(user);

  } else {
    // modify
    credits_table.modify(credits_iterator, _self, [&](auto &c) {
      c.amount += quantity;
    });
  }

}


/**
 * create_auction function creates a new auction record for the NFT with the specified ID
 * The auction record contains the auction start, end and end-of-bidding times.
 * This function is called by the bid action, i.e. the system responds to user activity
 * 
 * @param nft_id The ID of the NFT to be auctioned.
 */
void create_auction(uint64_t nft_id) {

  // get the auction length and bidding period length
  parameters_index parameters_table(get_self(), get_self().value);
  auto parameter_iterator = parameters_table.find(name("auctperiod").value);
  check(parameter_iterator != parameters_table.end(), "auction period is undefined");
  const uint32_t AUCTION_LENGTH_SECONDS = stoi(parameter_iterator->value);

  parameter_iterator = parameters_table.find(name("bidperiod").value);
  check(parameter_iterator != parameters_table.end(), "bidding period is undefined");
  const uint32_t AUCTION_BIDDING_PERIOD_SECONDS = stoi(parameter_iterator->value);

  // work out the start time from the system init time
  system_index system_table(get_self(), get_self().value);
  auto system_iterator = system_table.begin();
  check(system_iterator != system_table.end(), "the system record is undefined");

  time_point init = system_iterator->init;

  uint64_t init_secs = init.sec_since_epoch();
  uint64_t now_secs = current_time_point().sec_since_epoch();

  // if we are in the cooldown period then abandon attempt to start a new auction
  uint64_t elapsed_secs = ((now_secs - init_secs) % AUCTION_LENGTH_SECONDS);
  check(elapsed_secs <= AUCTION_BIDDING_PERIOD_SECONDS, "bidding is not permitted outside of the bidding period");

  uint64_t start_secs = now_secs - elapsed_secs;
  uint64_t bidding_end_secs = start_secs + AUCTION_BIDDING_PERIOD_SECONDS;
  uint64_t end_secs = start_secs + AUCTION_LENGTH_SECONDS;

  time_point start = time_point(seconds(start_secs));
  time_point bidding_end = time_point(seconds(bidding_end_secs));
  time_point end = time_point(milliseconds((end_secs * 1000) - 1)); // 1 millisecond before possible next auction

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



/**
 * add_bid function is called by the bid action. It adds a bid to the bids table, but only if the bid is higher
 * than the current highest bid
 * 
 * @param user the user who is placing the bid
 * @param nft_id the id of the NFT that is being bid on
 * @param bidamount the amount of the bid
 */
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

  asset bid_to_beat = asset(0, get_currency().get_symbol()); // initialise to zero bid
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

  extended_symbol currency = get_currency();
  int currency_multiplier = intPower(10, currency.get_symbol().precision());

  // get the minimum bid increment parameter
  name minimumbid = name("minimumbid");
  parameters_index parameters_table(get_self(), get_self().value);
  auto minbid_itr = parameters_table.find(minimumbid.value);
  check(minbid_itr != parameters_table.end(), "minimumbid parameter is not defined");
  asset MINIMUM_BID_INCREMENT = asset(stoi(minbid_itr->value) * currency_multiplier, currency.get_symbol());

  // get the bidstep parameter
  name bidstep = name("bidstep");
  auto bidstep_itr = parameters_table.find(bidstep.value);
  check(bidstep_itr != parameters_table.end(), "bidstep parameter is not defined");
  asset BIDSTEP_INCREMENT = asset(stoi(bidstep_itr->value) * currency_multiplier, currency.get_symbol());

  asset minimum_next_bid;
  if (bid_to_beat.amount == 0) {
    minimum_next_bid = MINIMUM_BID_INCREMENT;
  } else {
    minimum_next_bid = bid_to_beat + BIDSTEP_INCREMENT;
  }
  
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


/**
 * get_available_credit function returns the user's total credit minus the amount of the user's winning bid.
 * Called by the bid and withdraw actions.
 * 
 * @param user the user's account name
 * 
 * @return The user's available credit, i.e. number of tokens deposited.
 */
asset get_available_credit(name user) {
  // default values
  asset zero_credit = asset(0, get_currency().get_symbol());
  asset user_total_credit = zero_credit;
  asset winning_bid_amount = zero_credit;

  // get the user's credit
  credits_index credit_table(get_self(), user.value);
  auto credit_iterator = credit_table.begin();
  if (credit_iterator == credit_table.end()) {  // no credit record
    user_total_credit = zero_credit;
  } else {
    user_total_credit = credit_iterator->amount;
  }

  // get the winning bid amount
  bids_index bids_table(get_self(), get_self().value);
  auto amt_idx = bids_table.get_index<"byamount"_n>();
  auto bid_itr = amt_idx.rbegin();
  if (bid_itr != amt_idx.rend()) {
    if (bid_itr->bidder == user) {
      winning_bid_amount = bid_itr->bidamount;
    }
  }

  return user_total_credit - winning_bid_amount;  
}


/**
 * 
 * close_auction function is called to clean up after an auction has ended.
 * 
 * It finds the winning bid, transfers the NFT to the winner, reduces the winner's credit by the bid
 * amount, records the winner and winning bid in the latest auction record, clears the bids table, and
 * deletes the NFT record
 * 
 * @param nft_id the id of the nft being auctioned
 */
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


/**
 * bid action records the details of a user bid.
 * The system takes the opportunity to also store the BTC and FREEOS prices.
 * 
 * If the user is registered, the system is open for business, the user has enough credit, and the
 * auction is open for bidding, then add the user's bid to the bids table
 * 
 * @param user the user who is bidding
 * @param nft_id the id of the nft being bid on
 * @param bidamount the amount of credit the user is bidding
 */
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

  // check the user has enough available credit to support the bid
  asset user_available_credit = get_available_credit(user);
  check(user_available_credit >= bidamount, "you do not have sufficient credit to place your bid");

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
    } else {
      check(false, "bidding has ended for the nft");
    }

  } else {
    // No, there is no auction record for the nft

    if (nft_id == first_nft) {
      // create the auction for the first nft
      create_auction(nft_id); // will throw 'assert error' if in the cooldown period

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
      create_auction(nft_id);  // will throw 'assert error' if in the cooldown period

      // add the user bid
      add_bid(user, nft_id, bidamount);
    }
  }
}


/**
 * Reserved for future implementation.
 * The tick function will conduct scheduled (e.g. hourly, daily...) activities in response to user activity
 */
void tick() {

  // TODO

}


/**
 * claim action is called by the user who is the winner of the latest auction, then closes the auction and
 * transfers ownership of the nft to the user
 * 
 * @param user the name of the user who is claiming the NFT
 */
[[eosio::action]]
void claim(name user) {

  // no assert messages, fail silently

  require_auth(user);

  // get the latest auction record
  auctions_index auctions_table(get_self(), get_self().value);
  auto latest_auction_itr = auctions_table.rbegin();

  // if no latest auction record then halt
  check(latest_auction_itr != auctions_table.rend(), "there are no active auctions");

  // check that the auction bidding has finished
  time_point now = current_time_point();

  // if the bidding is ongoing then halt
  check(now > latest_auction_itr->bidding_end, "the bidding period has not ended");

  // get the winning bid
  bids_index bids_table(get_self(), get_self().value);
  auto amt_idx = bids_table.get_index<"byamount"_n>();
  auto amt_itr = amt_idx.rbegin();
  
  // if no winning bid then return silently
  check(amt_itr != amt_idx.rend(), "there was no winning bid");

  // check if the user is the winner
  check(user == amt_itr->bidder, "you do not have the winning bid");

  // get the nft id
  uint64_t nftid = amt_itr->nftid;

  // close the auction and transfer ownership of the nft to the user
  close_auction(nftid);
 
}



/**
 * maintain action enables the contract owner to perform various maintenance tasks on the contract.
 * 
 * @pre requires authority of the contract
 * 
 * @param action the action to perform
 * @param user the user's account name
 */
[[eosio::action]]
void maintain(string action, name user) {

  require_auth(get_self());

  if (action == "unregister") {
    users_index users_table(get_self(), user.value);
    auto user_itr = users_table.begin();
    check(user_itr != users_table.end(), "no user record");
    users_table.erase(user_itr);

    credits_index credits_table(get_self(), user.value);
    auto credit_itr = credits_table.begin();

    check(credit_itr != credits_table.end(), "no credit record");
    credits_table.erase(credit_itr);
  }

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

        /*
        bids_table.emplace(get_self(), [&](auto &b) {
          b.bidtime = current_time_point();
          b.bidder = name("billbeaumont");
          b.bidamount = asset(3000000, CREDIT_CURRENCY_SYMBOL);
          b.nftid = 4398046576805;
        });
        */
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

      asset bid_to_beat = asset(0, get_currency().get_symbol()); // initialise to zero bid
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

    if (action == "clear credit") {
      credits_index credits_table(get_self(), user.value);
      auto credit_iterator = credits_table.begin();

      if (credit_iterator != credits_table.end()) {
        credits_table.erase(credit_iterator);
      }
    }

}


/**
 * addnft adds an NFT to the nfts table
 * 
 * @pre requires authority of the contract
 * 
 * @param user the account that is calling the action
 * @param number the number of the nft in the auction list
 * @param nftid the id of the nft to add
 */
[[eosio::action]]
void addnft(name user, uint32_t number, uint64_t nftid) {

  require_auth(user);

  if (!isadmin(user)) {
    check(user == get_self(), "action requires authority of the contract or an account listed in the admins table");
  }
  
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


/**
 * removenft removes an NFT from the nfts table
 * 
 * @pre requires authority of the contract
 * 
 * @param user the account that is calling the action
 * @param number the unique number of the NFT
 */
[[eosio::action]]
void removenft(name user, uint32_t number) {

  require_auth(user);

  if (!isadmin(user)) {
    check(user == get_self(), "action requires authority of the contract or an account listed in the admins table");
  }

  // find the nft and erase it
  nfts_index nfts_table(get_self(), get_self().value);
  auto nft_itr = nfts_table.find(number);

  check(nft_itr != nfts_table.end(), "nft number not found");
  nfts_table.erase(nft_itr);
}


/**
 * paramupsert action takes a parameter name and a value, and either inserts or updates the parameter in the parameters table
 * 
 * @pre requires authority of the contract
 * 
 * @param paramname The name of the parameter.
 * @param value The value of the parameter.
 */
[[eosio::action]]
void paramupsert(name paramname, std::string value) {

  require_auth(_self);
  parameters_index parameters_table(get_self(), get_self().value);
  auto parameter_iterator = parameters_table.find(paramname.value);

  // check if the parameter is in the table or not
  if (parameter_iterator == parameters_table.end()) {
    // the parameter is not in the table, so insert
    parameters_table.emplace(_self, [&](auto &parameter) {
      parameter.paramname = paramname;
      parameter.value = value;
    });

  } else {
    // the parameter is in the table, so update
    parameters_table.modify(parameter_iterator, _self, [&](auto &parameter) {
      parameter.value = value;
    });
  }
}


/**
 * paramerase action deletes a parameter from the parameters table
 * 
 * @pre requires authority of the contract
 * 
 * @param paramname The name of the parameter to be removed.
 */
[[eosio::action]]
void paramerase(name paramname) {
  require_auth(_self);

  parameters_index parameters_table(get_self(), get_self().value);
  auto parameter_iterator = parameters_table.find(paramname.value);

  // check if the parameter is in the table or not
  check(parameter_iterator != parameters_table.end(),
        "config parameter does not exist");

  // the parameter is in the table, so delete
  parameters_table.erase(parameter_iterator);
}


/**
 * isadmin function checks whether the account is in the admins table
 * 
 * @param account The account name
 * @return True if account is in the admins table, false if not in the admins table
 */
bool isadmin(name account) {
  admins_index admins_table(get_self(), get_self().value);
  auto admin_iterator = admins_table.find(account.value);

  return (admin_iterator != admins_table.end());
}



/**
 * updateadmin action adds or removes an admin account to/from the admins table
 * 
 * @pre requires authority of the contract
 * 
 * @param account The name of the account to be added or removed.
 * @param remove Set to false to add account, true to remove account
 */
[[eosio::action]]
void updateadmin(name account, bool remove) {
  require_auth(_self);

  admins_index admins_table(get_self(), get_self().value);
  auto admin_iterator = admins_table.find(account.value);

  if (!remove) {  // add admin
    // check if the account is already in the table
    check(admin_iterator == admins_table.end(), "account is already in the admins table");
    admins_table.emplace(get_self(), [&](auto &admin) { admin.account = account; });
  } else {        // remove admin
    // check if the account is in the table
    check(admin_iterator != admins_table.end(), "account is not in the admins table");

    // the account is in the table, so delete
    admins_table.erase(admin_iterator);
  }  
}


/**
 * get_currency function reads and parses the 'currency' parameter
 * 
 * @pre The currency parameter must be defined. The format is precision,code e.g. 4,FREEOS
 * @return An extended_symbol representing the currency
 */
extended_symbol get_currency() {

  char code[13];
  uint8_t precision;
  char contract[13];

  name paramname = name("currency");
  parameters_index parameters_table(get_self(), get_self().value);
  auto parameter_iterator = parameters_table.find(paramname.value);

  // check if the parameter is in the table or not
  check(parameter_iterator != parameters_table.end(), "currency parameter is not defined");

  string currency = parameter_iterator->value;
  char* curr = currency.data();
  sscanf(curr, "%d %s %s", &precision, code, contract);

  // check(false, "get_currency: <" + to_string(precision) + "> <" + code + "> <" + contract + ">");

  symbol currency_symbol = symbol(code, precision);
  return extended_symbol(currency_symbol, name(contract));
}


/**
 * intPower helper function to calculate exponent of an integer
 * 
 * @param x The integer to be raised to a power
 * @param p The power to raise x by
 */
int intPower(int x, int p) {
  if (p == 0) return 1;
  if (p == 1) return x;
  return x * intPower(x, p-1);
}

};