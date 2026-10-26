#ifdef NEED_MARBLE_TEST_HOOKS
#include <cstdlib>

// ---------------------------------------------------------------------------
// V20-MOCK TEST BUILD ONLY (tests/need_marble/g2/test_hooks/need_marble_test_v20.inc).
// apply_test_hooks.py --v20 appends this to a SCRATCH COPY of src/map/need_marble.cpp. The repository source never
// has it, so the operating build registers no CP writer and need_marble_cp_apply stays DISABLED there.
// The mock writer changes only mbqa_cp_mock_balance / mbqa_cp_mock_op of ragnarok_mbqa, never #CASHPOINTS.
// ---------------------------------------------------------------------------

// one shot: 1 refuse before any change, 2 change then lose the answer, 3 change then the map-server dies,
// 4 lose the answer before any change (a timeout that never reached the balance)
int32 g_mbqa_cp_mode = 0;

/// Mock balance writer. Like #CASHPOINTS it has no idempotency key: every call that gets this far adds the amount,
/// so a second call for the same payout would show up as a second mbqa_cp_mock_op row.
int32 need_marble_test_cp_writer(uint32 aid, uint32 cid, uint64 payout_no, int32 amount) {
	const int32 mode = g_mbqa_cp_mode;
	g_mbqa_cp_mode = 0;
	if (!sq("SELECT DATABASE()"))
		return CPW_UNKNOWN;
	char* db = nullptr;
	const bool isolated = next_row() && Sql_GetData(mmysql_handle, 0, &db, nullptr) == SQL_SUCCESS && db != nullptr &&
		strcmp(db, "ragnarok_mbqa") == 0;
	free_result();
	if (isolated && mode == 4) {
		ShowWarning("need_marble_test: mock CP writer answer lost, balance not touched (payout %" PRIu64 ").\n", payout_no);
		return CPW_UNKNOWN;
	}
	if (!isolated || mode == 1) {
		ShowWarning("need_marble_test: mock CP writer refused before any change (payout %" PRIu64 ", %s).\n", payout_no,
			isolated ? "mode 1" : "not ragnarok_mbqa");
		return CPW_NO_EFFECT;
	}
	if (!sq("INSERT INTO `mbqa_cp_mock_op` (`account_id`,`payout_no`,`char_id`,`amount`,`mode`,`created_at`) "
		"VALUES ('%u','%" PRIu64 "','%u','%d','%d',NOW())", aid, payout_no, cid, amount, mode) ||
		!sq("INSERT INTO `mbqa_cp_mock_balance` (`account_id`,`balance`) VALUES ('%u','%d') "
			"ON DUPLICATE KEY UPDATE `balance`=`balance`+'%d'", aid, amount, amount))
		return CPW_UNKNOWN;
	if (mode == 3) {
		ShowWarning("need_marble_test: mock CP writer changed the balance, map-server stops now (payout %" PRIu64 ").\n", payout_no);
		std::_Exit(3);
	}
	if (mode == 2) {
		ShowWarning("need_marble_test: mock CP writer changed the balance, answer lost (payout %" PRIu64 ").\n", payout_no);
		return CPW_UNKNOWN;
	}
	return CPW_APPLIED;
}

void need_marble_test_cp_arm(int32 mode) {
	g_mbqa_cp_mode = mode >= 1 && mode <= 4 ? mode : 0;
}

// Registered before need_marble_init() only when the map-server runs with NEED_MARBLE_TEST_HOOKS=1 (bc_servers.ps1),
// and not with NEED_MARBLE_TEST_CP=char: then a NEED_MARBLE_CP_CHAR_WRITER build registers its char writer (V20-REAL).
struct mbqa_cp_register {
	mbqa_cp_register() {
		const char* e = getenv("NEED_MARBLE_TEST_HOOKS");
		const char* w = getenv("NEED_MARBLE_TEST_CP");
		if (e != nullptr && strcmp(e, "1") == 0 && (w == nullptr || strcmp(w, "char") != 0))
			need_marble_cp_set_writer(&need_marble_test_cp_writer);
	}
} g_mbqa_cp_register;
#endif  // NEED_MARBLE_TEST_HOOKS
