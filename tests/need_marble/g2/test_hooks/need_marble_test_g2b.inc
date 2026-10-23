
#ifdef NEED_MARBLE_TEST_HOOKS
// ---------------------------------------------------------------------------
// G2-B TEST BUILD ONLY (tests/need_marble/g2/test_hooks/need_marble_test_g2b.inc).
// apply_test_hooks.py --g2b appends this to a SCRATCH COPY of src/map/need_marble.cpp (same translation
// unit, so the anonymous-namespace ledger functions are reachable). The repository source never has it.
// Every entry point is called from a buildin that checks need_marble_test_allowed() first
// (environment NEED_MARBLE_TEST_HOOKS=1 + database ragnarok_mbqa + group 99).
// ---------------------------------------------------------------------------

int64 g_mbqa_clock_offset = 0;   // seconds added to time(nullptr) by today() in the scratch copy (V21 / V24)
int32 g_mbqa_commit_fault = 0;   // one shot: 1 = roll back, 2 = commit; the caller sees "COMMIT failed" either way (V16 / V21)

int64 need_marble_test_clock_offset() {
	return g_mbqa_clock_offset;
}

bool need_marble_test_commit_fault(Sql* handle) {
	if (g_mbqa_commit_fault == 0)
		return false;
	const int32 mode = g_mbqa_commit_fault;
	g_mbqa_commit_fault = 0;
	Sql_QueryStr(handle, mode == 2 ? "COMMIT" : "ROLLBACK");
	ShowWarning("need_marble_test: COMMIT outcome hidden from the ledger (mode %d: %s).\n", mode, mode == 2 ? "committed" : "rolled back");
	return true;
}

/// Sets the test clock offset (seconds) and returns the logical day it gives now.
uint32 need_marble_test_set_clock(int64 offset) {
	g_mbqa_clock_offset = offset;
	ShowWarning("need_marble_test: clock offset %" PRId64 " s, logical day %u.\n", offset, today());
	return today();
}

void need_marble_test_arm_commit_fault(int32 mode) {
	g_mbqa_commit_fault = mode == 1 || mode == 2 ? mode : 0;
}

/// Calls the hunt checkpoint entry point with an explicit (collect_session, batch_no, {day: kills}) batch.
/// spec = "20261101:20,20261102:5". Returns the entry point result; credited = points credited.
int32 need_marble_test_replay(map_session_data* sd, uint64 session, uint32 batch_no, const char* spec, int32& credited) {
	credited = 0;
	if (sd == nullptr || spec == nullptr || g_in_tx)
		return NEED_MARBLE_BAD_ARG;
	std::map<uint32, uint32> batch;
	const char* p = spec;
	while (*p != '\0') {
		char* end = nullptr;
		const unsigned long day = strtoul(p, &end, 10);
		if (end == p || *end != ':')
			return NEED_MARBLE_BAD_ARG;
		p = end + 1;
		const unsigned long kills = strtoul(p, &end, 10);
		if (end == p)
			return NEED_MARBLE_BAD_ARG;
		batch[static_cast<uint32>(day)] = static_cast<uint32>(kills);
		p = *end == ',' ? end + 1 : end;
	}
	if (batch.empty())
		return NEED_MARBLE_BAD_ARG;
	uint32 maxed_day = 0;
	const int32 rc = hunt_checkpoint(sd->status.account_id, sd->status.char_id, session, batch_no, batch, credited, maxed_day);
	ShowWarning("need_marble_test: replay session %" PRIu64 " batch %u (%s) -> %d, credited %d.\n", session, batch_no, spec, rc, credited);
	return rc;
}

/// V16 re-entry: an entry point started while another ledger transaction is open must refuse and write nothing.
/// Returns the inner entry point result (expected NEED_MARBLE_DB_ERROR after "nested transaction refused"),
/// -100 when the outer transaction could not start, -101 when the outer transaction was lost.
int32 need_marble_test_reentry(map_session_data* sd) {
	if (sd == nullptr)
		return NEED_MARBLE_BAD_ARG;
	ledger_tx outer;
	if (!outer.begin())
		return -100;
	std::map<uint32, uint32> batch;
	batch[today()] = 1;
	int32 credited = 0;
	uint32 maxed_day = 0;
	const int32 inner = hunt_checkpoint(sd->status.account_id, sd->status.char_id, 4000000000ull, 1, batch, credited, maxed_day);
	const bool outer_alive = g_in_tx;
	outer.rollback();
	ShowWarning("need_marble_test: re-entry probe -> inner %d, outer transaction %s.\n", inner, outer_alive ? "kept" : "LOST");
	return outer_alive ? inner : -101;
}

/// V16 internal function outside a transaction: must refuse (MARBLE_REQUIRE_TX) and write nothing.
int32 need_marble_test_out_of_tx(map_session_data* sd) {
	if (sd == nullptr || g_in_tx)
		return NEED_MARBLE_BAD_ARG;
	int32 credited = 0;
	const int32 rc = credit_in_tx(sd->status.account_id, sd->status.char_id, season(), ACT_HUNT, today(), 999, 0, today(), 25, false, credited);
	ShowWarning("need_marble_test: credit_in_tx outside a transaction -> %d.\n", rc);
	return rc;
}

/// Ledger module flags and the caller's hunt buffer, as character variables for the harness.
void need_marble_test_flags(map_session_data* sd) {
	if (sd == nullptr)
		return;
	pc_setreg(sd, add_str("@MBQA_IN_TX"), g_in_tx ? 1 : 0);
	pc_setreg(sd, add_str("@MBQA_FAULT"), g_fault ? 1 : 0);
	pc_setreg(sd, add_str("@MBQA_DAY"), today());
	pc_setreg(sd, add_str("@MBQA_POL_DAY"), g_pol_day);
	auto it = g_hunt.find(sd->status.account_id);
	int64 pending = 0;
	if (it != g_hunt.end())
		for (const auto& e : it->second.pending)
			pending += e.second;
	pc_setreg(sd, add_str("@MBQA_SESSION"), it != g_hunt.end() ? static_cast<int64>(it->second.collect_session) : 0);
	pc_setreg(sd, add_str("@MBQA_BATCH"), it != g_hunt.end() ? it->second.batch_no : 0);
	pc_setreg(sd, add_str("@MBQA_FROZEN"), it != g_hunt.end() && !it->second.frozen.empty() ? it->second.frozen_no : 0);
	pc_setreg(sd, add_str("@MBQA_PENDING"), pending);
	pc_setreg(sd, add_str("@MBQA_PENDING_DAYS"), it != g_hunt.end() ? static_cast<int64>(it->second.pending.size()) : 0);
}
#endif  // NEED_MARBLE_TEST_HOOKS
