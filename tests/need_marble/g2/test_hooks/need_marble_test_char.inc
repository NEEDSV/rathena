#ifdef NEED_MARBLE_TEST_HOOKS
// ---------------------------------------------------------------------------
// V20-REAL TEST BUILD ONLY (tests/need_marble/g2/test_hooks/need_marble_test_char.inc).
// apply_test_hooks.py --v20real appends this to a SCRATCH COPY of src/char/inter.cpp and anchors the calls into the
// CP grant path. One shot faults armed in mbqa_cp_fault of ragnarok_mbqa, read only with NEED_MARBLE_TEST_HOOKS=1:
// 1 the char-server dies after both writes, before COMMIT; 2 it dies after COMMIT, before answering;
// 3 it commits and keeps running but the answer is never sent.
// ---------------------------------------------------------------------------
static int32 g_mbqa_char_mode = 0;

static void need_cp_test_take_mode() {
	g_mbqa_char_mode = 0;
	const char* e = getenv("NEED_MARBLE_TEST_HOOKS");
	if (e == nullptr || strcmp(e, "1") != 0)
		return;
	if (need_cp_count("SELECT COUNT(*) FROM `information_schema`.`TABLES` WHERE DATABASE()='ragnarok_mbqa' "
		"AND `TABLE_SCHEMA`='ragnarok_mbqa' AND `TABLE_NAME`='mbqa_cp_fault'") != 1)
		return;
	const int64 mode = need_cp_count("SELECT COALESCE(MAX(`mode`),0) FROM `mbqa_cp_fault` WHERE `id`='1'");
	if (mode > 0 && SQL_SUCCESS == Sql_QueryStr(sql_handle, "UPDATE `mbqa_cp_fault` SET `mode`='0' WHERE `id`='1'"))
		g_mbqa_char_mode = static_cast<int32>(mode);
}

static void need_cp_test_point(int32 point) {
	if (g_mbqa_char_mode != point)
		return;
	ShowWarning("need_marble_test: char-server stops %s.\n", point == 1 ? "before COMMIT" : "after COMMIT, before the answer");
	std::_Exit(10 + point);
}

static bool need_cp_test_drop_answer() {
	if (g_mbqa_char_mode != 3)
		return false;
	ShowWarning("need_marble_test: grant committed, answer not sent.\n");
	return true;
}
#endif  // NEED_MARBLE_TEST_HOOKS
