// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED marble V20-REAL: map-server -> char-server CP grant protocol, shared by both servers.
//
// The char-server is the only writer of a marble CP grant. One request = one InnoDB transaction on the char DB that
// stores the idempotency row (need_marble_cp_grant) and the new #CASHPOINTS value of acc_reg_num together.
// While a grant of a login is unanswered, the map-server never saves #CASHPOINTS as a plain absolute value: it
// sends NEED_CP_PKT_CONDSAVE and the char-server adds the amount only when that very request was applied.
// Header only, no server dependency, so tests/need_marble/logic_test.cpp checks it too.

#ifndef COMMON_NEED_MARBLE_CP_GRANT_HPP
#define COMMON_NEED_MARBLE_CP_GRANT_HPP

#include <cstdint>

namespace need_marble_cp {

// map -> char: W cmd, L account_id, L char_id, Q payout_no, Q req_id, L amount
constexpr uint16_t PKT_GRANT = 0x300A;
constexpr int32_t PKT_GRANT_LEN = 30;
// map -> char: W cmd, L account_id, L char_id, Q payout_no, Q req_id, Q base (#CASHPOINTS of the map without the grant)
constexpr uint16_t PKT_CONDSAVE = 0x300B;
constexpr int32_t PKT_CONDSAVE_LEN = 34;
// char -> map: W cmd, L account_id, Q payout_no, Q req_id, L amount, B result, Q balance after (0 unless applied)
constexpr uint16_t PKT_GRANT_ACK = 0x380A;
constexpr int32_t PKT_GRANT_ACK_LEN = 35;

/// Largest single grant. Board rewards are 500..10,000 CP (policy draft); anything above is refused before any change.
constexpr int32_t GRANT_MAX = 100000;
/// Same cap as MAX_CASHPOINT (INT_MAX) of the map-server.
constexpr int64_t BALANCE_MAX = 2147483647LL;
/// Account registry key of the balance (CASHPOINT_VAR of the map-server), index 0.
constexpr const char* CASHPOINT_KEY = "#CASHPOINTS";

// Answer of the char-server. NO_EFFECT answers are certain: nothing of this request was written.
enum e_grant_result : uint8_t {
	GR_APPLIED = 0,      // idempotency row and new balance committed together
	GR_DISABLED = 1,     // NO_EFFECT: switch off or tables not ready on the char-server
	GR_BAD_ARG = 2,      // NO_EFFECT: zero / negative / too large amount, zero key
	GR_CAP = 3,          // NO_EFFECT: the balance would pass BALANCE_MAX, or it is already negative
	GR_ALREADY = 4,      // this key was applied before with the same amount: nothing added now
	GR_CONFLICT = 5,     // this key was applied before with another amount: nothing added now
	GR_DB_ERROR = 6,     // failed inside the transaction or at COMMIT: outcome not proven
};

inline bool grant_args_ok(uint32_t account_id, uint64_t payout_no, uint64_t req_id, int32_t amount) {
	return account_id != 0 && payout_no != 0 && req_id != 0 && amount > 0 && amount <= GRANT_MAX;
}

/// New balance of a grant or GR_CAP. old_balance is the stored #CASHPOINTS (0 when there is no row).
inline uint8_t grant_new_balance(int64_t old_balance, int32_t amount, int64_t* out) {
	if (old_balance < 0 || amount <= 0 || old_balance > BALANCE_MAX - amount)
		return GR_CAP;
	*out = old_balance + amount;
	return GR_APPLIED;
}

/// Value a conditional save stores: the map's value plus the grant only when this very request was applied,
/// never above BALANCE_MAX (the map-server caps #CASHPOINTS the same way).
inline int64_t condsave_value(int64_t base, bool applied_by_this_request, int32_t amount) {
	if (!applied_by_this_request || amount <= 0)
		return base;
	return base > BALANCE_MAX - amount ? BALANCE_MAX : base + amount;
}

}  // namespace need_marble_cp

#endif  // COMMON_NEED_MARBLE_CP_GRANT_HPP
