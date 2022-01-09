// oldbid has the original bid logic
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

  // check the user has enough credit to support the bid
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

