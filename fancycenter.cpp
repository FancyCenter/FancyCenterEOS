// edit: Oct 28th, 2019

#include <eosio/eosio.hpp>
#include <eosio/system.hpp>
#include <eosio/crypto.hpp>
#include <eosio/permission.hpp>
#include <eosio/action.hpp>
#include <eosio/asset.hpp>
#include <eosio/symbol.hpp>

#define REF_BONUS_QUANTITY 5000
#define CORE_SYMBOL symbol("EOS", 4)
#define MAX_REVEAL_WAIT (60 * 60 * 24)        // 24 hours. Max wait time until allow anyone to reveal game instead of '_self'  
#define FANCYCENTER_SEED_SECRET_STR_LENGTH 72 // proof that fancyCenter won't play with seed to adjust winner. This var prevents length extensions attack from fancycenter on generated fancycenter_seed. (https://en.wikipedia.org/wiki/Length_extension_attack, https://github.com/iagox86/hash_extender/blob/master/README.md) 
#define FANCYCENTER_SECRET_STR_LENGTH 65
#define MAX_DISCOUNT_TIME (5 * 60)            // discount only 5 mins after purchase

using namespace std;
using namespace eosio;

class [[eosio::contract]] fancycenter : public eosio::contract {
  private:

    struct [[eosio::table]] state_data {     
      uint64_t id;                           // 0 - use as singleton
      bool is_contract_active = true;        // constract is active by default
      uint64_t games_table_last_index = 0;   // store index of last entry in available_games here, because of optimization
      int64_t total_players_payout = 0;      // total payout to players w/o refs
      uint64_t hashes_last_used = 0;
      uint64_t hashes_last_index = 0;
      uint64_t longgames_bets_count = 0;     // total number of bets (for long games)
      int64_t longgame_fee_avail = 0;        // dev's fee (long games)
      uint64_t primary_key() const {return 0;}
    };

    struct [[eosio::table]] available_items {
      uint64_t id;                      // website id. https://fancy.center/items/{id}
      uint64_t chance;                  // win chance 1:N
      int64_t bet;                      // price of bet
      int64_t item_price;               // user will get this amount
      uint64_t ends_at;                 // when item expire
      uint64_t total_games_played = 0;  // number  of games
      uint8_t total_days_duration;      // lifetime (in days)
      uint32_t added_at;                // init date
      checksum256 trx_added;
      bool allowed_free_try;             // is 'free_try' allowed?
      uint64_t primary_key() const {return id;}
      uint64_t get_secondary_1() const {return ends_at;}
    };

    struct [[eosio::table]] available_games {
      uint64_t id;                      // id of current game
      uint64_t id_ref_item;             // ref to available_items's id
      uint32_t user_bet_time;           // bet placement time
      uint64_t status;                  // 0 - not active, 1 - active, waiting for player, 2 - player made bet, wait for server reveal, 3 - player is winner, 4 - fancycenter is winner
      name player;                      // who played this game
      
      checksum256 fancycenter_check_hash; // sha256(fancycenter_seed + fancycenter_secret)
      // the goal of "fancycenter_check_hash" it to store sha256 checksum of (fancycenter_seed + fancycenter_secret)
      // when fancycenter call "reveal" action it contains seed and secret.
      // if sha256(seed + secret) is equal to fancycenter_check_hash, then 
      // blockchain confirm that seed and secret correct and resoult should be fair

      // more details regarding to algorithm available at http://fancy.center/how-it-works and github repository https://github.com/FancyCenter/FancyCenterEOS/tree/master
      uint32_t fancycenter_seed;      // fancycenter's random between 1,000,000 and 5,000,000 inclusive
      std::string fancycenter_secret; // fancycenter's secret
      // both fancycenter_seed and fancycenter_secret are filled out when call "reveal" action
      uint32_t player_seed;         // player's random between 1,000,000 and 5,000,000 inclusive
      uint32_t player_lucky_number; // player's random number between 0 and CHANCE - 1 inclusive. Chance related to available_items' table "chance" field
      uint32_t calculated_lucky_win_number; // calculated win number,
      // calculated_lucky_win_number = (fancycenter_seed + player_seed) % available_items.chance
      // if (calculated_lucky_win_number == player_lucky_number) - player is winner!
      
      checksum256 trx_created;                         
      checksum256 trx_revealed;                         
      uint8_t who_revealed = 0;      // 0 - n/a, 1 - backend, 2 - user
      std::string memo_card = "";    // memo to support creditcards payments
      uint64_t primary_key() const {return id;}
      uint64_t get_secondary_1() const {return status;}
      checksum256 get_secondary_2() const {return trx_created;}
      uint64_t get_secondary_3() const {return id_ref_item;}
      uint64_t get_secondary_4() const {return player.value;}
    };

    struct [[eosio::table]] available_hashes {
      uint64_t id;
      checksum256 seed_hash;
      uint64_t primary_key() const {return id;}
    };

    struct [[eosio::table]] avail_long_games {
      uint64_t id;                    // website id. https://fancy.center/items/{id}
      int64_t bet;                    // price of bet
      int64_t item_price;             // full price of item
      uint64_t ends_at;               // when item expire
      uint8_t total_days_duration;    // lifetime (in days)
      uint32_t added_at;              // when item was added to table
      checksum256 trx_added;

      uint64_t status;              // 1 - active, 2 - winner is defined, no new bets

      uint64_t tickets_total;       // total number of tickets
      uint64_t tickets_sold = 0;    // sold tickets
      uint64_t tickets_left;        // number of available tickets
      uint64_t first_ticket_index;  // index of first ticket sold. for speed up search in long_games_bets
      int64_t most_likely_winner_payout = 0; // payout to winner. increases with every sold ticket

      // Win data
      uint64_t winner_id; // [1...tickets_total]
      checksum256 result_tx;
      int64_t winner_payout;
      name winner_name;

      // Randomize data
      uint64_t players_seed_sum;          // sum of all players's seeds (0 - 100,000)
      checksum256 fancycenter_check_hash; // sha256(random_seed_backend + secret); 
      uint32_t fancycenter_seed;          // fancycenter's random between 1,000,000 and 5,000,000
      std::string fancycenter_secret;
      uint8_t who_revealed = 0;   // 0 - n/a, 1 - backend, 2 - user

      // keys
      uint64_t primary_key() const {return id;}
      uint64_t get_secondary_1() const {return status;}
    };

    struct [[eosio::table]] long_games_bets { 
      uint64_t id;
      uint64_t longgame_ref_id;
      uint64_t longgame_betid;  
      name player;                    // player's account
      uint32_t bet_at;                // when user placed a bet
      checksum256 bet_tx;
      std::string memo_card = "";     // memo to support creditcards payments
      uint64_t primary_key() const {return id;}
      uint64_t get_secondary_1() const {return longgame_betid;}
      uint64_t get_secondary_2() const {return longgame_ref_id;}
      uint64_t get_secondary_3() const {return player.value;}
    };

    // store here users who got free ticket
    struct [[eosio::table]] free_games {
      name player;
      uint32_t when;
      uint64_t primary_key() const {return player.value;}
    };

    struct  [[eosio::table]] refs_data {
      name player;
      name who_invited;
      uint64_t earned;
      bool was_invited = false;
      int32_t invited_at = 0;
      uint64_t primary_key() const {return player.value;}
      uint64_t get_secondary_1() const {return earned;}
    };

    typedef eosio::multi_index<name("items"), available_items,
      indexed_by<name("byendsat"), const_mem_fun<available_items, uint64_t, &available_items::get_secondary_1>>
    > items_table;
    typedef eosio::multi_index<name("games"), available_games, 
      indexed_by<name("bystatus"), const_mem_fun<available_games, uint64_t, &available_games::get_secondary_1>>,
      indexed_by<name("bytrxcreated"), const_mem_fun<available_games, checksum256, &available_games::get_secondary_2>>,
      indexed_by<name("byrefid"), const_mem_fun<available_games, uint64_t, &available_games::get_secondary_3>>,
      indexed_by<name("byname"), const_mem_fun<available_games, uint64_t, &available_games::get_secondary_4>>
    > games_table;
    typedef eosio::multi_index<name("state"), state_data> state_table;
    typedef eosio::multi_index<name("hashes"), available_hashes> hashes_table;
    typedef eosio::multi_index<name("longgames"), avail_long_games,
      indexed_by<name("bystat"), const_mem_fun<avail_long_games, uint64_t, &avail_long_games::get_secondary_1>>
    > longgames_table;
    typedef eosio::multi_index<name("longbets"), long_games_bets,
      indexed_by<name("byinid"), const_mem_fun<long_games_bets, uint64_t, &long_games_bets::get_secondary_1>>,
      indexed_by<name("berefid"), const_mem_fun<long_games_bets, uint64_t, &long_games_bets::get_secondary_2>>,
      indexed_by<name("byname"), const_mem_fun<long_games_bets, uint64_t, &long_games_bets::get_secondary_3>>
    > longbets_table;
    typedef eosio::multi_index<name("freegames"), free_games> freegames_table;
    typedef eosio::multi_index<name("refstable"), refs_data,
      indexed_by<name("byearn"), const_mem_fun<refs_data, uint64_t, &refs_data::get_secondary_1>>
    > refs_table;

    items_table _items;
    games_table _games;
    state_table _state;
    hashes_table _hashes;
    longgames_table _longgames;
    longbets_table _longbets;
    freegames_table _freegames;
    refs_table _refs;

    checksum256 pick_fancycenter_backend_hash() {
      auto iterator_state = _state.find(0);
      uint64_t hash_to_pick_up_index = (*iterator_state).hashes_last_used + 1;
      check((*iterator_state).hashes_last_index >= hash_to_pick_up_index, "Unable to init game. Try again later. Sorry.");
      
      auto iterator_hash = _hashes.find(hash_to_pick_up_index);
      check(iterator_hash != _hashes.end(), "Unable to init game. Sorry. Try again later");

      checksum256 result = (*iterator_hash).seed_hash;

      _hashes.erase(iterator_hash);

      _state.modify(iterator_state, _self, [&](auto& state_row) {
        state_row.hashes_last_used = hash_to_pick_up_index;
      });

      return result;
    }

    checksum256 get_trx_id() {
      size_t size = transaction_size();
      char buf[size];
      size_t read = read_transaction(buf, size);
      check(size == read, "read_transaction failed");
      return eosio::sha256(buf, read);
    }

    checksum256 generate_fc_hash(std::string data) {
      checksum256 digest = eosio::sha256(&data[0], data.size());
      return digest;
    }

    string generate_fc_string(uint64_t _lucky_number, std::string _secret) {
      // lucky_secret_hash is: sha256(<_lucky_number><_secret>)
      std::string data = std::to_string(_lucky_number);
      data.append(_secret);
      return data;
    }

    void play_instant_game(uint64_t game_type, int64_t _player_sent_amount, name _player, uint64_t _item_id, uint32_t _player_seed, uint32_t _player_lucky_number, uint64_t _discount_ref, bool free_try) {
      // get time
      uint32_t current_time = eosio::current_time_point().sec_since_epoch();

      // state
      auto iterator_state = _state.find(0);
      check((*iterator_state).is_contract_active == true, "Contract is not active now");

      // items
      auto iterator_items = _items.find(_item_id);
      check(iterator_items != _items.end(), "Item not found");
      check((*iterator_items).ends_at > current_time, "Item expired");

      // let's calc the bet price
      int64_t bet_required = 9999999999999; 

      if (game_type == 1) { // regular instant game
        bet_required = (*iterator_items).bet;
      } else if (game_type == 2) { // instant game with discount
        bet_required = static_cast<int64_t>(((*iterator_items).bet * 3) / 4);
        // check ref and timeout
        auto iterator_games = _games.find(_discount_ref);
        check(_discount_ref != 0, "Ref is 0");
        check((*iterator_games).player == _player, "Wrong player");
        check(iterator_games != _games.end(), "Ref not found");
        check(((*iterator_games).user_bet_time + MAX_DISCOUNT_TIME) > current_time, "Ref bet too old");
      } else {
        return;
      }

      // is free try allowed?
      if (free_try == true && (*iterator_items).allowed_free_try == true) {
        bet_required = 0; 
      }
      
      check(_player_sent_amount >= bet_required, "Wrong amount sent");
      check(_player_lucky_number >= 0 && _player_lucky_number < (*iterator_items).chance, "Wrong _player_lucky_number. Should be >= 0 & < CHANCE");
      check(_player_seed >= 1000000 && _player_seed <= 5000000, "Wrong _player_seed. Should be >= 1,000,000 and <= 5,000,000");

      const uint64_t next_game_id = (*iterator_state).games_table_last_index + 1;

      // get lucky number hash & original msg length
      checksum256 new_hash = pick_fancycenter_backend_hash();

      // update game
      _games.emplace(free_try ? _player : _self, [&](auto& new_game) {
        // init new game
        new_game.id = next_game_id;
        new_game.id_ref_item = _item_id;
        new_game.status = 1;
        new_game.fancycenter_check_hash = new_hash;
        new_game.trx_created = get_trx_id();

        // insert user related data
        new_game.player_seed = _player_seed;
        new_game.player_lucky_number = _player_lucky_number;
        new_game.status = 2;
        new_game.user_bet_time = eosio::current_time_point().sec_since_epoch();
        new_game.player = _player;
      });

      // update index
      _state.modify(iterator_state, free_try ? _player : _self, [&](auto& state_row) {
        state_row.games_table_last_index = next_game_id;
      });

      // update item
      _items.modify(iterator_items, free_try ? _player : _self, [&](auto& items_row) {
        items_row.total_games_played += 1;
      });
    }

    void split_memo(std::vector<std::string> &results, std::string memo) {
      auto end = memo.cend();
      auto start = memo.cbegin();
      for (auto it = memo.cbegin(); it != end; ++it) {
        if (*it == ';') {
          results.emplace_back(start, it);
          start = it + 1;
        }
      }
      if (start != end)
        results.emplace_back(start, end);
    }

    void buy_long_ticket(uint64_t game_type, int64_t _player_sent_amount, name _player, uint64_t _game_id, uint32_t _player_seed) {
      uint32_t current_time = eosio::current_time_point().sec_since_epoch();

      auto iterator_state = _state.find(0);
      check((*iterator_state).is_contract_active == true, "Contract is not active now");

      auto iterator_games = _longgames.find(_game_id);
      check(iterator_games != _longgames.end(), "Long game not found");
      check((*iterator_games).ends_at > current_time, "Long game expired");
      check((*iterator_games).status == 1, "Long game is not active");
      check((*iterator_games).tickets_left > 0, "No tickets left");

      int64_t bet_required = (*iterator_games).bet;

      check(_player_sent_amount >= bet_required, "Wrong amount sent");
      check(_player_seed >= 0 && _player_seed <= 100000, "Wrong _player_seed. Should be >= 0 and <= 100,000");

      bool is_first_bet = (*iterator_games).tickets_sold == 0;
      uint64_t new_longbet_games_count = (*iterator_state).longgames_bets_count + 1;
      uint64_t new_longgame_bet = (*iterator_games).tickets_sold + 1;

      // calc dev's fee & inc winner's payout
      const int64_t winner_payout_inc = static_cast<int64_t>((bet_required * 7) / 10);
      const int64_t dev_fee = _player_sent_amount - winner_payout_inc;

      _state.modify(iterator_state, _self, [&](auto& state_row) {
        state_row.longgames_bets_count = new_longbet_games_count;
        state_row.longgame_fee_avail += dev_fee;
      });

      _longgames.modify(iterator_games, _self, [&](auto& row_game) {
        row_game.tickets_sold += 1;
        row_game.tickets_left -= 1;
        row_game.players_seed_sum += _player_seed;
        row_game.most_likely_winner_payout += winner_payout_inc;
        if (is_first_bet == true) {
          row_game.first_ticket_index = new_longbet_games_count;
        }
      });

      _longbets.emplace(_self, [&](auto& bet_item) {
        bet_item.id = new_longbet_games_count;
        bet_item.longgame_ref_id = _game_id;
        bet_item.longgame_betid = new_longgame_bet;
        bet_item.player = _player;
        bet_item.bet_at = current_time;
        bet_item.bet_tx = get_trx_id();
      });
    }

    void process_ref_bonus(name current_player, name who_invited) {

      if (current_player == who_invited)
        return;

      if (is_account(who_invited) == false)
        return;

      // check if record exists
      auto iterator_current_player = _refs.find(current_player.value);
      auto iterator_who_invited = _refs.find(who_invited.value);

      bool need_send_bonus_to_who_invited = false;

      if (iterator_current_player == _refs.end()) {
        // nobody invited this user before as well this user didn't invite anyone

        // put record to _refs that someone invited this player
        _refs.emplace(_self, [&](auto& ref_row) {
          ref_row.player = current_player;
          ref_row.who_invited = who_invited;
          ref_row.earned = 0;
          ref_row.was_invited = true;
          ref_row.invited_at = eosio::current_time_point().sec_since_epoch();
        });

        need_send_bonus_to_who_invited = true;

      } else if ( (*iterator_current_player).was_invited == false ) {
        // this user wasn't invited by someone before
        // but is active player and invited at least one person
        // don't pay in this case because according to policy FancyCenter pays only for new users
      }

      // let's deal with who_invited

      if (need_send_bonus_to_who_invited == true) {

        if (iterator_who_invited == _refs.end()) {
          // no one invited this user before and this user didn't invite anyone before
          _refs.emplace(_self, [&](auto& ref_row) {
            ref_row.player = who_invited;
            ref_row.earned = REF_BONUS_QUANTITY;
          });

        } else {
          // this user was invited before by someone or already invited someone
          _refs.modify(iterator_who_invited, _self, [&](auto& ref_row) {
            ref_row.earned += REF_BONUS_QUANTITY;
          });

        }

        // and finally send 0.5000 EOS to who_invited

        std:string memo_str = "Ref bonus, u invited: ";
        const std::string string_player = current_player.to_string();
        memo_str.append(string_player);

        asset quantity = asset{REF_BONUS_QUANTITY, CORE_SYMBOL};

        action{
          permission_level{_self, "active"_n},
          "eosio.token"_n, "transfer"_n,
          std::make_tuple(_self, who_invited, quantity, memo_str)
        }.send();

      }
    }

  public:
    fancycenter(name receiver, name code, datastream<const char*> ds):contract(receiver, code, ds),
      _items(receiver, receiver.value),
      _games(receiver, receiver.value),
      _state(receiver, receiver.value),
      _hashes(receiver, receiver.value),
      _longgames(receiver, receiver.value),
      _longbets(receiver, receiver.value),
      _freegames(receiver, receiver.value),
      _refs(receiver, receiver.value) {}

    // player, game request
    [[eosio::on_notify("eosio.token::transfer")]]
    void gamereq(name player, name to, asset quantity, std::string memo) {
      if (to != _self || player == _self) {
        print("No game for you, Sorry.");
        return;
      }

      check(quantity.symbol == CORE_SYMBOL, "We do not accept this token, sorry");
      check(quantity.amount >= 0, "Should be >= 0");

      // simple transfer, not a game definitely 
      if (memo == "") return;

      std::vector<std::string> results;
      split_memo(results, memo);

      if (results.size() < 5) return;

      // if this is a bet memo pattern should be: game_type;item_id;player_seed;player_lucky_number;ref_id;<who_invited - optional>

      uint64_t game_type = std::stoull(results[0]);
      // 1 - instant game
      // 2 - instant game with a discount
      // 3 - long game
      uint64_t item_id = std::stoull(results[1]);
      uint64_t player_seed = std::stoull(results[2]);
      uint64_t player_lucky_number = std::stoull(results[3]); 
      uint64_t discount_ref = std::stoull(results[4]);

      require_auth(player);
      auto iterator = _state.find(0);
      check((*iterator).is_contract_active == true, "Contract is not active now");

      check(game_type == 1 || game_type == 2 || game_type == 3, "We support only instant_games now (1,2) and long (3)");

      if (game_type == 1 || game_type == 2) {
        play_instant_game(game_type, quantity.amount, player, item_id, player_seed, player_lucky_number, discount_ref, false);
      } else if (game_type == 3) {
        buy_long_ticket(game_type, quantity.amount, player, item_id, player_seed);
      }

      // let's deal with ref bonus now
      if (results.size() == 6)
        process_ref_bonus(player, name(results[5]));
    }

    // anyone - play with 'free try'
    [[eosio::action]]
    void freetry(name player, string memo) {
      require_auth(player);

      // maybe already tried?
      auto iterator_free_try = _freegames.find(player.value);
      check(iterator_free_try == _freegames.end(), "Already Tried Free Game");

      // put record to _freegames
      _freegames.emplace(player, [&](auto& new_row) {
        new_row.player = player;
        new_row.when = eosio::current_time_point().sec_since_epoch();
      });

      // same as 'gamereq' 
      if (memo == "") return;
      std::vector<std::string> results;
      split_memo(results, memo);
      if (results.size() < 5) return;

      uint64_t game_type = std::stoull(results[0]);
      uint64_t item_id = std::stoull(results[1]);
      uint64_t player_seed = std::stoull(results[2]);
      uint64_t player_lucky_number = std::stoull(results[3]); 

      check(game_type == 1, "Only game_type === 1");

      play_instant_game(1, 0, player, item_id, player_seed, player_lucky_number, 0, true);
    }

    // only owner - init EOS smart contract 
    /*[[eosio::action]]
    void initcontract() {
      require_auth(_self);
      auto iterator = _state.find(0);
      check(iterator == _state.end(), "Already Initialized");

      // 0 - general state
      _state.emplace(_self, [&](auto& new_state) {
        new_state.id = 0;
      });

      // add default hash, used 0
      _hashes.emplace(_self, [&](auto& new_hash){
        new_hash.id = 0;
      });
    }*/

    // only owner - pause | resume smart contract
    [[eosio::action]]
    void setcstatus(bool _status) {
      require_auth(_self);
      auto iterator = _state.find(0);
      _state.modify(iterator, _self, [&](auto& row_state) {
        row_state.is_contract_active = _status;
      });
    }

    // only owner - add item to "available_items" table and activate first game for this item
    [[eosio::action]]
    void additem(uint64_t _id, uint64_t _chance, int64_t _bet, int64_t _item_price, uint8_t _duration_days, bool _allowed_free_try) {
      require_auth(_self);
      
      auto iterator = _items.find(_id);
      check(iterator == _items.end(), "Item already exists");

      const uint32_t current_time = eosio::current_time_point().sec_since_epoch(); 
      
      // Create Item
      _items.emplace(_self, [&](auto& new_item) {
        new_item.id = _id;
        new_item.chance = _chance;
        new_item.bet = _bet;
        new_item.item_price = _item_price;
        new_item.ends_at = current_time + _duration_days * 24 * 60 * 60;
        new_item.added_at = current_time;
        new_item.total_days_duration = _duration_days;
        new_item.trx_added = get_trx_id();
        new_item.allowed_free_try = _allowed_free_try;
      });
    }

    // only owner - add hash to "available_hashes"
    [[eosio::action]]
    void addhash(checksum256 _seed_hash) {
      require_auth(_self);
      auto iterator = _state.find(0);
      uint64_t new_hashes_last_index = (*iterator).hashes_last_index + 1;

      _hashes.emplace(_self, [&](auto& new_hash) {
        new_hash.id = new_hashes_last_index;
        new_hash.seed_hash = _seed_hash;
      });

      _state.modify(iterator, _self, [&](auto& new_state) {
        new_state.hashes_last_index = new_hashes_last_index;
      });
    }

    // only owner (OR player if game was not revealed for more than MAX_REVEAL_WAIT - 24h)
    [[eosio::action]]
    void reveal(uint64_t _game_id, uint64_t _fancycenter_seed, std::string _fancycenter_secret) {
      auto iterator_games = _games.find(_game_id);
      check(iterator_games != _games.end(), "Game not found");
      check((*iterator_games).status == 2, "Wrong game status");

      auto iterator_items = _items.find((*iterator_games).id_ref_item);
      check(iterator_items != _items.end(), "Parent item not found.");

      const uint32_t current_time = eosio::current_time_point().sec_since_epoch(); 
      const uint32_t game_age = current_time - (*iterator_games).user_bet_time;

      const bool need_check_auth_and_seed = game_age < MAX_REVEAL_WAIT;
      // if game was not revealed during MAX_REVEAL_WAIT (1 day), then anyone can reveal with ANY seed and secret. Nice to win!
      // but we (fancycenter) guarantee reveal interval around 10 seconds. 
      if (need_check_auth_and_seed == true) {
        require_auth(_self);
        check(_fancycenter_seed >= 1000000 && _fancycenter_seed <= 5000000, "Wrong _fancycenter_seed. Should be >= 1,000,000 and <= 5,000,000");
        std::string seed_and_secret_string = generate_fc_string(_fancycenter_seed, _fancycenter_secret);
        const checksum256 seed_and_secret_hash = generate_fc_hash(seed_and_secret_string);
        check(_fancycenter_secret.length() == FANCYCENTER_SECRET_STR_LENGTH, "Wrong _fancycenter_secret length");
        check(seed_and_secret_string.length() == FANCYCENTER_SEED_SECRET_STR_LENGTH, "Wrong seed_and_secret_string length");
        check(seed_and_secret_hash == (*iterator_games).fancycenter_check_hash, "Wrong _lucky_number or _secret");
      }

      uint32_t calculated_lucky_win_number = ((*iterator_games).player_seed + _fancycenter_seed) % (*iterator_items).chance;

      const bool is_user_winner = calculated_lucky_win_number == (*iterator_games).player_lucky_number;

      _games.modify(iterator_games, _self, [&](auto& game_row) {
        //game_row.backend_secret = _secret;
        //game_row.lucky_number = _lucky_number;
        game_row.fancycenter_seed = _fancycenter_seed;
        game_row.fancycenter_secret = _fancycenter_secret;
        game_row.calculated_lucky_win_number = calculated_lucky_win_number;

        game_row.trx_revealed = get_trx_id();
        game_row.status = is_user_winner ? 3 : 4;
        game_row.who_revealed = need_check_auth_and_seed ? 1 : 2;
      });

      if (is_user_winner == true) {
        std:string memo_win = "Win from FancyCenter! Game id: ";
        const std::string string_game_id = std::to_string(_game_id);
        memo_win.append(string_game_id);
        asset quantity = asset{(*iterator_items).item_price, CORE_SYMBOL};
        action{
          permission_level{_self, "active"_n},
          "eosio.token"_n, "transfer"_n,
          std::make_tuple(_self, (*iterator_games).player, quantity, memo_win)
        }.send();

        auto iterator_state = _state.find(0);
        _state.modify(iterator_state, _self, [&](auto& state_row) {
          state_row.total_players_payout += (*iterator_items).item_price;
        });
      }
    }

    // only owner - add and activate long games
    [[eosio::action]]
    void addlonggame(uint64_t _id, int64_t _bet, int64_t _item_price, uint8_t _duration_days, uint64_t _tickets_total, checksum256 _check_hash) {
      require_auth(_self);
      
      auto iterator = _longgames.find(_id);
      check(iterator == _longgames.end(), "Item already exists");

      const uint32_t current_time = eosio::current_time_point().sec_since_epoch(); 
      
      // Create Item
      _longgames.emplace(_self, [&](auto& new_game) {
        new_game.id = _id;
        new_game.bet = _bet;
        new_game.item_price = _item_price;
        new_game.ends_at = current_time + _duration_days * 24 * 60 * 60;
        new_game.total_days_duration = _duration_days;
        new_game.added_at = current_time;
        new_game.trx_added = get_trx_id();
        new_game.status = 1;
        new_game.tickets_total = _tickets_total;
        new_game.tickets_sold = 0;
        new_game.tickets_left = _tickets_total;
        new_game.players_seed_sum = 0;
        new_game.fancycenter_check_hash = _check_hash;
        new_game.most_likely_winner_payout = 0;
      });
    }

    // only owner (OR player if game was not revealed for more than MAX_REVEAL_WAIT - 24h) - choose long game winner
    [[eosio::action]]
    void finalizelong(uint64_t _id, uint32_t _seed, std::string _secret) {
      uint32_t current_time = eosio::current_time_point().sec_since_epoch();

      auto iterator_state = _state.find(0);
      check((*iterator_state).is_contract_active == true, "Contract is not active now");

      auto iterator_games = _longgames.find(_id);
      check(iterator_games != _longgames.end(), "Long game not found");
      check((*iterator_games).status == 1, "Game is not active!");

      bool all_tickets_sold = (*iterator_games).tickets_left == 0;
      bool is_timedout = current_time > (*iterator_games).ends_at;

      check(all_tickets_sold == true || is_timedout == true, "Long game is active, not possible to finalize");

      const int64_t game_age = current_time - (*iterator_games).ends_at;
      const bool need_check_auth = game_age < MAX_REVEAL_WAIT;

      if (need_check_auth == true) {
        require_auth(_self);
        check(_seed >= 1000000 && _seed <= 5000000, "Wrong _seed. Should be >= 1,000,000 and <= 5,000,000");
        std::string seed_and_secret_string = generate_fc_string(_seed, _secret);
        const checksum256 seed_and_secret_hash = generate_fc_hash(seed_and_secret_string);
        check(_secret.length() == FANCYCENTER_SECRET_STR_LENGTH, "Wrong _fancycenter_secret length");
        check(seed_and_secret_string.length() == FANCYCENTER_SEED_SECRET_STR_LENGTH, "Wrong seed_and_secret_string length");
        check(seed_and_secret_hash == (*iterator_games).fancycenter_check_hash, "Wrong _seed or _secret");
      }

      uint64_t result_seed = (*iterator_games).players_seed_sum + _seed;
      uint64_t winner_id = result_seed % (*iterator_games).tickets_sold + 1; 

      // lets find winner by id secondary index "longgame_betid"
      auto iterator_bets = _longbets.find((*iterator_games).first_ticket_index);
      bool found_winner = false;

      while (found_winner == false && iterator_bets != _longbets.end()) {
        if ((*iterator_bets).longgame_ref_id == _id &&  (*iterator_bets).longgame_betid == winner_id) {
          found_winner = true;
        } else {
          iterator_bets++;
        }
      } 

      check(found_winner == true, "Winner should be defined");

      const int64_t payout = (*iterator_games).most_likely_winner_payout;

      _longgames.modify(iterator_games, _self, [&](auto& game_row) {
        game_row.status = 2;
        game_row.winner_id = winner_id;
        game_row.result_tx = get_trx_id();
        game_row.winner_payout = payout;
        game_row.winner_name = (*iterator_bets).player;

        game_row.fancycenter_seed = _seed;
        game_row.fancycenter_secret = _secret;
        game_row.who_revealed = need_check_auth == true ? 1 : 2;
      });

      // let's find winner in table 'games'

      std:string memo_win = "Win from FancyCenter! Game id: ";
      const std::string string_game_id = std::to_string(_id);
      memo_win.append(string_game_id);
      asset quantity = asset{payout, CORE_SYMBOL};
      action{
        permission_level{_self, "active"_n},
        "eosio.token"_n, "transfer"_n,
        std::make_tuple(_self, (*iterator_bets).player, quantity, memo_win)
      }.send();

      _state.modify(iterator_state, _self, [&](auto& state_row) {
        state_row.total_players_payout += payout;
      });
    }

};
