# -*- coding: utf-8 -*-
"""Replay the 2025 client's EnchantList load in a real Lua 5.1 (lupa.lua51).

The client exe's C_* functions are stubbed; every resource key goes through the real
ItemDB_To_ItemID of the ItemDBNameTbl under test (MessageBox on a missing key), and
C_GetSlotCount answers with the item's card slots so AddTargetItem's slot check runs too.
"""
import nelib

try:
    import lupa.lua51 as lua51
except ImportError:  # pragma: no cover
    lua51 = None

# Values used by the client's EnchantList_f checks (exe constants, see Phase 1.0 report)
CLIENT_CONSTANTS = {'MAX_SLOT_NUM': 4, 'MAX_REFINE_LEVEL': 20, 'MAX_GRADE_LEVEL': 4, 'MAX_MATERIAL_NUM': 10}

STUBS = b'''
local function R(n) return ItemDB_To_ItemID(n) ~= 0 end
local function M(t) local ok = true; if t then for k, v in pairs(t) do ok = R(k) and ok end end; return ok end
C_SetSlotOrder = function() return true, "" end
C_AddTargetItem = function(i, n) return R(n), "target" end
C_SetCondition = function() return true, "" end
C_ApproveRandomOption = function() return true, "" end
C_SetReset = function(i, b, r, z, mt) return M(mt), "reset" end
C_SetCaution = function() return true, "" end
C_SetRequire = function(i, s, z, mt) return M(mt), "require" end
C_SetSuccessRate = function() return true, "" end
C_SetGradeBonus = function() return true, "" end
C_SetEnchant = function(i, s, g, rt) return M(rt), "enchant" end
C_AddPerfectEnchant = function(i, s, n, z, mt) return R(n) and M(mt), "perfect" end
C_AddUpgradeEnchant = function(i, s, n, r, z, mt) return R(n) and R(r) and M(mt), "upgrade" end
C_SetRandomUpgradeRequire = function(i, s, n, z, mt) return R(n) and M(mt) end
C_AddRandomUpgradeEnchant = function(i, s, n, r, rate) return R(n) and R(r) end
C_AddPerfectUpgradeEnchant = function(i, s, n, r, z, mt) return R(n) and R(r) and M(mt) end
'''

# Canonical, order-independent serialisation of the Table built by EnchantList (functions skipped)
SERIALIZE = b'''
function __need_ser(v, seen)
  local t = type(v)
  if t == "table" then
    if seen[v] then return "<cycle>" end
    seen[v] = true
    local keys = {}
    for k, x in pairs(v) do
      if type(x) ~= "function" then table.insert(keys, k) end
    end
    table.sort(keys, function(a, b)
      local ta, tb = type(a), type(b)
      if ta ~= tb then return ta < tb end
      return a < b
    end)
    local parts = {}
    for _, k in ipairs(keys) do
      table.insert(parts, __need_ser(k, seen) .. "=" .. __need_ser(v[k], seen))
    end
    seen[v] = nil
    return "{" .. table.concat(parts, ",") .. "}"
  elseif t == "string" then
    return string.format("%q", v)
  else
    return tostring(v)
  end
end
'''


class ClientRun(object):
    def __init__(self):
        self.messages = []
        self.check_ok = None
        self.load_ok = None
        self.load_msg = None
        self.tables = 0
        self.serialized = None

    @property
    def ok(self):
        return self.check_ok and self.load_ok and not self.messages


def run(itemdbnametbl_src, enchantlist_f_blob, enchantlist_blob, slot_count, serialize=False):
    """slot_count: callable(item_id) -> card slots."""
    if lua51 is None:
        raise RuntimeError('lupa (lua51) is required')
    rt = lua51.LuaRuntime(unpack_returned_tuples=True, encoding=None)
    g = rt.globals()
    result = ClientRun()
    g.MessageBox = lambda s: result.messages.append(s.decode('cp949', 'replace') if isinstance(s, bytes) else s)
    for k, v in CLIENT_CONSTANTS.items():
        g[k.encode('ascii')] = v
    g.IS_CLIENT = True
    rt.execute(_host(itemdbnametbl_src))
    rt.execute(STUBS)
    tbl = rt.eval('ItemDBNameTbl')
    g.C_GetSlotCount = lambda name: slot_count(int(tbl[name] or 0))
    rt.execute(_host(enchantlist_f_blob))
    rt.execute(_host(enchantlist_blob))
    result.check_ok, _ = rt.eval('CheckFile()')
    result.load_ok, result.load_msg = rt.eval('LoadAllData()')
    result.tables = rt.eval('(function() local c = 0 for k in pairs(Table) do c = c + 1 end return c end)()')
    if serialize:
        rt.execute(SERIALIZE)
        result.serialized = rt.eval('__need_ser(Table, {})')
    return result


def _host(blob):
    return nelib.lua_to_host(blob) if blob[:4] == b'\x1bLua' else blob


def equivalent(itemdbnametbl_src, enchantlist_f_blob, original_blob, generated_src, slot_count):
    """True when the decompiled source builds exactly the same Table as the original bytecode."""
    a = run(itemdbnametbl_src, enchantlist_f_blob, original_blob, slot_count, serialize=True)
    b = run(itemdbnametbl_src, enchantlist_f_blob, generated_src, slot_count, serialize=True)
    return a.serialized == b.serialized, a, b
