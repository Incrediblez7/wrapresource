#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <string>
#include <eosio/system.hpp>
namespace eosiosystem {
	class system_contract;
}
namespace eosio {
		using std::string;
		class [[eosio::contract("token")]] token : public contract {
		private:
	  	struct [[eosio::table]] account {
			asset	balance;
			uint64_t primary_key()const { return balance.symbol.code().raw(); }
	  	};
	  	struct [[eosio::table]] currency_stats {
			asset	supply;
			asset	max_supply;
			name	issuer;
			uint64_t primary_key()const { return supply.symbol.code().raw(); }
	  	};
      		struct [[eosio::table]] ramusage {
			name 	owner;
			asset 	net_weight;
			asset 	cpu_weight;
      			uint64_t ram_bytes;
			uint64_t primary_key()const { return owner.value; }
	  	};
	  	typedef eosio::multi_index< "accounts"_n, account > accounts;
	  	typedef eosio::multi_index< "stat"_n, currency_stats > stats;
      		typedef eosio::multi_index< "userres"_n, ramusage > rams;
		public:
		using contract::contract;
	  	static asset get_supply(const name& token_contract_account, const symbol_code& sym_code)
	  	{
		 	stats statstable(token_contract_account, sym_code.raw());
		 	const auto& st = statstable.get(sym_code.raw());
		 	return st.supply;
	  	}
	  	static asset get_balance(const name& token_contract_account, const name& owner, const symbol_code& sym_code)
	  	{
		 	accounts accountstable(token_contract_account, owner.value);
		 	const auto& ac = accountstable.get(sym_code.raw());
		 	return ac.balance;
	  	}
		void sub_balance(const name& owner, const asset& value) 
		{
			accounts from_acnts(_self, owner.value);
			const auto& from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
			check(from.balance.amount >= value.amount, "overdrawn balance");
			from_acnts.modify(from, owner, [&](auto& a) {
				a.balance -= value;
			});
		}
		void add_balance(const name& owner, const asset& value, const name& ram_payer)
		{
			accounts to_acnts(_self, owner.value);
			auto to = to_acnts.find(value.symbol.code().raw());
			if(to == to_acnts.end()) {
				to_acnts.emplace(ram_payer, [&](auto& a){
					a.balance = value;
				});
			} else {
				to_acnts.modify(to, same_payer, [&](auto& a) {
					a.balance += value;
				});
			}
		}
	  	[[eosio::action]]
  		void create(const name& issuer, const asset& maximum_supply)
		{
			require_auth(_self);

			auto sym = maximum_supply.symbol;
			check(sym.is_valid(), "invalid symbol name");
			check(maximum_supply.is_valid(), "invalid supply");
			check(maximum_supply.amount > 0, "max-supply must be positive");

			stats statstable(_self, sym.code().raw());
			auto existing = statstable.find(sym.code().raw());
			check(existing == statstable.end(), "token with symbol already exists");

			statstable.emplace(_self, [&](auto& s) {
				s.supply.symbol	= maximum_supply.symbol;
				s.max_supply	= maximum_supply;
				s.issuer		= issuer;
			});
		}
		[[eosio::action]]
		void issue(const name& to, const asset& quantity, const string& memo)
		{
			auto sym = quantity.symbol;
			check(sym.is_valid(), "invalid symbol name");
			check(memo.size() <= 256, "memo has more than 256 bytes");

			stats statstable(_self, sym.code().raw());
			auto existing = statstable.find(sym.code().raw());
			check(existing != statstable.end(), "token with symbol does not exist, create token before issue");
			const auto& st = *existing;
			check(to == st.issuer, "tokens can only be issued to issuer account");

			require_auth(st.issuer);
			check(quantity.is_valid(), "invalid quantity");
			check(quantity.amount > 0, "must issue positive quantity");

			check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
			check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

			statstable.modify(st, same_payer, [&](auto& s) {
				s.supply += quantity;
			});

			add_balance(st.issuer, quantity, st.issuer);
		}
		[[eosio::action]]
		void retire(const asset& quantity, const string& memo)
		{
			auto sym = quantity.symbol;
			check(sym.is_valid(), "invalid symbol name");
			check(memo.size() <= 256, "memo has more than 256 bytes");

			stats statstable(_self, sym.code().raw());
			auto existing = statstable.find(sym.code().raw());
			check(existing != statstable.end(), "token with symbol does not exist");
			const auto& st = *existing;

			require_auth(st.issuer);
			check(quantity.is_valid(), "invalid quantity");
			check(quantity.amount > 0, "must retire positive quantity");

			check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

			statstable.modify(st, same_payer, [&](auto& s) {
				s.supply -= quantity;
			});

			sub_balance(st.issuer, quantity);
		}
		[[eosio::action]]
		void transfer(const name& from, const name& to, const asset& quantity, const string& memo)
		{
			check(from != to, "cannot transfer to self");
			require_auth(from);
			check(is_account(to), "to account does not exist");
			auto sym = quantity.symbol.code();
			stats statstable(_self, sym.raw());
			const auto& st = statstable.get(sym.raw());

			require_recipient(from);
			require_recipient(to);

			check(quantity.is_valid(), "invalid quantity");
			check(quantity.amount > 0, "must transfer positive quantity");
			check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
			check(memo.size() <= 256, "memo has more than 256 bytes");

			auto payer = has_auth(to) ? to : from;

			sub_balance(from, quantity);
			add_balance(to, quantity, payer);

			if(to == _self) sell(from,to,quantity,memo);
		}
		[[eosio::action]]
		void open(const name& owner, const symbol& symbol, const name& ram_payer)
		{
			require_auth(ram_payer);

			check(is_account(owner), "owner account does not exist");

			auto sym_code_raw = symbol.code().raw();
			stats statstable(_self, sym_code_raw);
			const auto& st = statstable.get(sym_code_raw, "symbol does not exist");
			check(st.supply.symbol == symbol, "symbol precision mismatch");

			accounts acnts(_self, owner.value);
			auto it = acnts.find(sym_code_raw);
			if(it == acnts.end()) {
				acnts.emplace(ram_payer, [&](auto& a){
					a.balance = asset{0, symbol};
				});
			}
		}
		[[eosio::action]]
		void close(const name& owner, const symbol& symbol)
		{
			require_auth(owner);
			accounts acnts(_self, owner.value);
			auto it = acnts.find(symbol.code().raw());
			check(it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect.");
			check(it->balance.amount == 0, "Cannot close because the balance is not zero.");
			acnts.erase(it);
		}
    		[[eosio::on_notify("eosio.token::transfer")]]
    		void buy(name from, name to, asset quantity, std::string memo) {
      			if(from==_self || to!=_self || from=="eosio.ram"_n) return;
      			rams ram("eosio"_n, _self.value);
      			auto it = ram.find(_self.value);
      			action{
				permission_level{_self, "active"_n},
				"eosio"_n,
				"buyram"_n,
				std::make_tuple(_self,_self,quantity)
	  		}.send();
	  		action{
				permission_level{_self, "active"_n},
				_self,
				"send"_n,
				std::make_tuple(from,it->ram_bytes)
	  		}.send();
    		}
		[[eosio::action]]
		void send(name from, uint64_t before) {
			require_auth(_self);
			rams ram("eosio"_n, _self.value);
      			auto it = ram.find(_self.value);
			uint64_t now = it->ram_bytes;
			asset amount = asset(now-before, symbol("WRAM",4));
			action{
				permission_level{_self, "active"_n},
				_self,
				"issue"_n,
				std::make_tuple(_self,amount,std::string("issue"))
	  		}.send();
			asset send = asset((double)amount.amount * 0.995,amount.symbol);
			action{
				permission_level{_self, "active"_n},
				_self,
				"transfer"_n,
				std::make_tuple(_self,from,send,std::string("transfer"))
	  		}.send();
			action{
				permission_level{_self, "active"_n},
				_self,
				"transfer"_n,
				std::make_tuple(_self,"stable.ly"_n,amount-send,std::string("fee"))
	  		}.send();
		}
		[[eosio::action]]
		void back(name from) {
			require_auth(_self);
			action{
				permission_level{_self, "active"_n},
				"eosio.token"_n,
				"transfer"_n,
				std::make_tuple(_self,from,get_balance("eosio.token"_n,_self,symbol_code("EOS")),std::string("unwrap"))
	  		}.send();
		}
		void sell(name from, name to, asset quantity, std::string memo) {
			if(from==_self || to!=_self) return;
			action{
				permission_level{_self, "active"_n},
				_self,
				"retire"_n,
				std::make_tuple(quantity,std::string("retire"))
	  		}.send();
			action{
				permission_level{_self, "active"_n},
				"eosio"_n,
				"sellram"_n,
				std::make_tuple(_self,quantity.amount)
	  		}.send();
			action{
				permission_level{_self, "active"_n},
				_self,
				"back"_n,
				std::make_tuple(from)
	  		}.send();
		}
	};
}
