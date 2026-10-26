#ifdef NEED_MARBLE_TEST_HOOKS
#include <chrono>
#include <thread>

extern int32 char_fd;   // chrif.cpp

// ---------------------------------------------------------------------------
// V20-REAL TEST BUILD ONLY (tests/need_marble/g2/test_hooks/need_marble_test_v20real.inc).
// apply_test_hooks.py --v20real appends this to a SCRATCH COPY of src/map/need_marble.cpp and anchors four calls
// into it (answer hold, stop after the request, fence off, no automatic payout). The repository source never has it.
// Nothing here changes a balance: grants go through the product char writer to the isolated char-server.
// ---------------------------------------------------------------------------

struct mbqa_held_answer {
	uint32 aid;
	uint64 payout_no, req_id;
	int32 amount, result;
	int64 balance;
};
std::vector<mbqa_held_answer> g_mbqa_held;
int32 g_mbqa_real_mode = 0;    // 1 hold char-server answers, 4 stop after the request left (flushed), 5 stop before it left
bool g_mbqa_fence_off = false;  // negative control: #CASHPOINTS saves stay plain absolute values during a grant
bool g_mbqa_no_kick = false;    // no automatic payout: rows wait for @mbqacpapply
std::map<uint64, int32> g_mbqa_last;   // payout_no -> last char-server result seen (held or not)
struct mbqa_held_request {
	uint32 aid, cid;
	uint64 payout_no, req_id;
	int32 amount;
};
std::vector<mbqa_held_request> g_mbqa_held_req;
bool g_mbqa_hold_req = false;   // grant requests wait in the map-server (a request that reaches the char-server late)

bool need_marble_test_cp_hold_request(uint32 aid, uint32 cid, uint64 payout_no, uint64 req_id, int32 amount) {
	if (!g_mbqa_hold_req)
		return false;
	g_mbqa_held_req.push_back({ aid, cid, payout_no, req_id, amount });
	ShowWarning("need_marble_test: grant request held in the map-server (payout %" PRIu64 ").\n", payout_no);
	return true;
}

bool need_marble_test_cp_hold(uint32 aid, uint64 payout_no, uint64 req_id, int32 amount, int32 result, int64 balance) {
	g_mbqa_last[payout_no] = result;
	if (g_mbqa_real_mode != 1)
		return false;
	g_mbqa_held.push_back({ aid, payout_no, req_id, amount, result, balance });
	ShowWarning("need_marble_test: char-server answer held (payout %" PRIu64 ", result %d).\n", payout_no, result);
	return true;
}

void need_marble_test_cp_after_send() {
	if (g_mbqa_real_mode == 4) {
		flush_fifo(char_fd);
		ShowWarning("need_marble_test: grant request flushed to the char-server, map-server stops now.\n");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		std::_Exit(4);
	}
	if (g_mbqa_real_mode == 5) {
		ShowWarning("need_marble_test: map-server stops before the grant request left.\n");
		std::_Exit(5);
	}
}

bool need_marble_test_cp_fence_off() {
	return g_mbqa_fence_off;
}

bool need_marble_test_cp_no_kick() {
	return g_mbqa_no_kick;
}

/// 0 reset, 1 hold answers, 2 deliver held answers, 3 drop held answers (lost), 4 / 5 stop the map-server after /
/// before the next request leaves, 6 / 7 fence off / on, 8 / 9 automatic payout off / on, 10 hold grant requests in
/// the map-server, 11 send the held requests now. Returns the mode or the number of answers / requests handled.
int32 need_marble_test_cp_real(int32 mode) {
	switch (mode) {
	case 0:
		g_mbqa_real_mode = 0;
		g_mbqa_fence_off = false;
		g_mbqa_no_kick = false;
		g_mbqa_hold_req = false;
		return 0;
	case 10:
		g_mbqa_hold_req = true;
		return 10;
	case 11: {
		g_mbqa_hold_req = false;
		std::vector<mbqa_held_request> held;
		held.swap(g_mbqa_held_req);
		for (const auto& h : held)
			intif_need_cp_grant(h.aid, h.cid, h.payout_no, h.req_id, h.amount);
		return static_cast<int32>(held.size());
	}
	case 1:
	case 4:
	case 5:
		g_mbqa_real_mode = mode;
		return mode;
	case 2: {
		g_mbqa_real_mode = 0;
		std::vector<mbqa_held_answer> held;
		held.swap(g_mbqa_held);
		for (const auto& h : held)
			need_marble_cp_char_result(h.aid, h.payout_no, h.req_id, h.amount, h.result, h.balance);
		return static_cast<int32>(held.size());
	}
	case 3: {
		g_mbqa_real_mode = 0;
		const int32 n = static_cast<int32>(g_mbqa_held.size());
		g_mbqa_held.clear();
		return n;
	}
	case 6:
	case 7:
		g_mbqa_fence_off = mode == 6;
		return mode;
	case 8:
	case 9:
		g_mbqa_no_kick = mode == 8;
		return mode;
	}
	return -1;
}

/// Ordinary cash change of an online character through the product paths: pc_getcash (delta > 0) / pc_paycash
/// (delta < 0), then with save = 1 the registry save a script end does. Returns sd->cashPoints.
int64 need_marble_test_cp_cash(map_session_data* tsd, int32 delta, int32 save) {
	if (delta > 0)
		pc_getcash(tsd, delta, 0, LOG_TYPE_SCRIPT);
	else if (delta < 0)
		pc_paycash(tsd, -delta, 0, LOG_TYPE_SCRIPT);
	if (save && tsd->vars_dirty)
		intif_saveregistry(tsd);
	return tsd->cashPoints;
}

/// Sends one more grant request for a key the ledger already settled (char-server idempotency probe). The answer is
/// handled like any other: the ledger row is no longer UNCERTAIN so nothing is recorded, and no login gets the amount.
int32 need_marble_test_cp_raw(map_session_data* tsd, uint64 payout_no, int32 amount) {
	boot_id();
	const uint64 req_id = (static_cast<uint64>(g_boot_id) << 32) | ++g_cp_req_seq;
	g_mbqa_last.erase(payout_no);
	if (intif_need_cp_grant(tsd->status.account_id, tsd->status.char_id, payout_no, req_id, amount) != 0)
		return -1;
	g_cp_sent[{ tsd->status.account_id, payout_no }] = { req_id, amount, gettick() };
	return 0;
}

/// Last char-server result seen for this payout_no, -1 = none yet.
int32 need_marble_test_cp_last(uint64 payout_no) {
	auto it = g_mbqa_last.find(payout_no);
	return it == g_mbqa_last.end() ? -1 : it->second;
}

/// payout_no of the unanswered grant of this account (0 = none), and whether it is this login's (fence active).
int64 need_marble_test_cp_inflight(map_session_data* tsd, int32* fence) {
	uint64 p = 0, r = 0;
	*fence = need_marble_cp_fence(tsd, &p, &r) ? 1 : 0;
	auto it = g_cp_inflight.find(tsd->status.account_id);
	return it == g_cp_inflight.end() ? 0 : static_cast<int64>(it->second.payout_no);
}
#endif  // NEED_MARBLE_TEST_HOOKS
