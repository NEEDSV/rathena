"""Minimal RO protocol client for the G2 isolated stack (PACKETVER 20250604, no obfuscation).

Based on tools/need_marble/roclient.py. Adds: NPC dialog driver (mes/next/menu/input/close),
dispbottom (0x02C1) collection decoded as CP949, monster tracking from unit packets and the
attack request (0x0437). Server texts are parsed by scanning every offset with length/ID checks.
G2-A: hunt captcha (0x0A58/0x0A59 image, 0x0A5A ack, 0x0A5B ask, 0x0A5C answer, 0x0A5D close) and
skill use on a target (0x0438, heal seen back as 0x09CB).
"""
import select
import socket
import struct
import threading
import time

LOGIN = ('127.0.0.1', 26901)
CHAR = ('127.0.0.1', 26121)
NPC_MIN = 100000000


def _recv_some(sock, timeout):
    r, _, _ = select.select([sock], [], [], timeout)
    if not r:
        return b''
    d = sock.recv(65536)
    if not d:
        raise ConnectionError('closed by server')
    return d


def _recv_until(sock, pid, timeout=15.0):
    buf = b''
    end = time.time() + timeout
    key = struct.pack('<H', pid)
    while time.time() < end:
        buf += _recv_some(sock, 0.3)
        i = buf.find(key)
        if i >= 0 and len(buf) - i >= 30:
            return buf, i
    raise TimeoutError('packet 0x%04X not received' % pid)


def dec(b):
    return b.split(b'\0')[0].decode('cp949', 'replace')


class Client:
    def __init__(self, user, password, slot=0, log=print, tag=None):
        self.user, self.password, self.slot, self.log = user, password, slot, log
        self.tag = tag or user
        self.sock = None
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.alive = False
        self.seen = set()
        self.events = []      # (offset, kind, data) in stream order
        self.texts = []       # dispbottom texts (str)
        self.mobs = {}        # gid -> last seen time
        self.dead = set()
        self.name = ''

    # ------------------------------------------------------------------ connect
    def connect(self, name):
        self.name = name
        s = socket.create_connection(LOGIN, timeout=10)
        s.settimeout(None)
        s.sendall(struct.pack('<HI24s24sB', 0x0064, 0, self.user.encode(), self.password.encode(), 0))
        buf, i = _recv_until(s, 0x0AC4)
        id1, aid, id2 = struct.unpack_from('<III', buf, i + 4)
        sex = buf[i + 46]
        s.close()
        c = socket.create_connection(CHAR, timeout=10)
        c.settimeout(None)
        c.sendall(struct.pack('<HIIIHB', 0x0065, aid, id1, id2, 0, sex))
        time.sleep(1.0)
        _recv_some(c, 1.0)
        c.sendall(struct.pack('<HB', 0x0066, self.slot))
        buf, i = _recv_until(c, 0x0AC5)
        cid = struct.unpack_from('<I', buf, i + 2)[0]
        mapname = buf[i + 6:i + 22].split(b'\0')[0].decode('latin1')
        port = struct.unpack_from('<H', buf, i + 26)[0]
        c.close()
        m = socket.create_connection(('127.0.0.1', port), timeout=10)
        m.settimeout(None)
        tick = int(time.time() * 1000) & 0xffffffff
        m.sendall(struct.pack('<HIIII4sB', 0x0436, aid, cid, id1, tick, b'\0' * 4, sex))
        self.sock = m
        self.aid, self.cid, self.mapname = aid, cid, mapname
        time.sleep(1.5)
        self.sock.sendall(struct.pack('<H', 0x007D))
        self.alive = True
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._keepalive, daemon=True).start()
        time.sleep(1.0)
        self.log(f'[{self.tag}] entered {mapname} aid={aid} cid={cid}')
        return self

    def _reader(self):
        while self.alive:
            try:
                d = _recv_some(self.sock, 0.2)
            except Exception as e:
                self.log(f'[{self.tag}] reader stopped: {e}')
                self.alive = False
                return
            if d:
                with self.lock:
                    start = max(0, len(self.buf) - 8)
                    self.buf += d
                    self._scan(start)

    def _keepalive(self):
        while self.alive:
            try:
                self.sock.sendall(struct.pack('<HI', 0x0360, int(time.time() * 1000) & 0xffffffff))
                self.sock.sendall(struct.pack('<H', 0x007D))
            except Exception:
                self.alive = False
                return
            time.sleep(0.7)

    # ------------------------------------------------------------------ parsing
    def _scan(self, start):
        b = self.buf
        n = len(b)
        i = start
        while i + 6 <= n:
            pid = b[i] | (b[i + 1] << 8)
            if pid in (0x02C1, 0x00B4, 0x00B7):
                ln = b[i + 2] | (b[i + 3] << 8)
                if pid == 0x02C1:
                    ok = 13 <= ln <= 1000 and i + ln <= n and i + 12 <= n and struct.unpack_from('<I', b, i + 4)[0] == self.aid
                    if ok and i not in self.seen:
                        self.seen.add(i)
                        t = dec(bytes(b[i + 12:i + ln]))
                        self.texts.append(t)
                        self.events.append((i, 'text', t))
                    elif not ok and i + ln > n and 13 <= ln <= 1000:
                        return   # incomplete, retried with the next chunk
                else:
                    ok = 9 <= ln <= 3000 and i + 8 <= n and struct.unpack_from('<I', b, i + 4)[0] >= NPC_MIN
                    if ok and i + ln > n:
                        return
                    if ok and i not in self.seen:
                        self.seen.add(i)
                        npc = struct.unpack_from('<I', b, i + 4)[0]
                        t = dec(bytes(b[i + 8:i + ln]))
                        self.events.append((i, 'mes' if pid == 0x00B4 else 'menu', (npc, t)))
            elif pid == 0x008E:
                # ZC_NOTIFY_PLAYERCHAT: clif_displaymessage (GM / atcommand answers such as @mbcp). A whole, printable,
                # NUL-terminated message only, so stray bytes of other packets are not taken for one.
                ln = b[i + 2] | (b[i + 3] << 8)
                if 5 <= ln <= 600 and i + ln > n:
                    return
                body = bytes(b[i + 4:i + ln]) if 5 <= ln <= 600 else b''
                if body and body[-1] == 0 and 0 not in body[:-1] and all(c >= 0x20 for c in body[:-1]) and i not in self.seen:
                    self.seen.add(i)
                    t = dec(body)
                    self.texts.append(t)
                    self.events.append((i, 'text', t))
            elif pid in (0x00B5, 0x00B6, 0x0142):
                npc = struct.unpack_from('<I', b, i + 2)[0]
                if npc >= NPC_MIN and npc < 0x7FFFFFFF and i not in self.seen and self._near_dialog(npc):
                    self.seen.add(i)
                    self.events.append((i, {0x00B5: 'next', 0x00B6: 'close', 0x0142: 'input'}[pid], npc))
            elif pid in (0x09FF, 0x09FE, 0x09FD):
                if i + 13 <= n:
                    ln = b[i + 2] | (b[i + 3] << 8)
                    if 60 <= ln <= 200 and b[i + 4] == 5:
                        # PACKETVER >= 20131223 carries the unit id in the AID field (offset 5); GID is 0 for mobs
                        gid = struct.unpack_from('<I', b, i + 5)[0] or struct.unpack_from('<I', b, i + 9)[0]
                        if gid >= NPC_MIN:
                            self.mobs[gid] = time.time()
                            self.dead.discard(gid)
            elif pid == 0x0091:
                # ZC_NPCACK_MAPMOVE: mapname[16] x y (sent on every warp / recall, also within one map)
                if i + 22 <= n and i not in self.seen:
                    name = bytes(b[i + 2:i + 18]).split(b'\0')[0]
                    if name.endswith(b'.gat') and 4 < len(name) <= 16:
                        self.seen.add(i)
                        self.pos = (name.decode('latin1'),) + struct.unpack_from('<HH', b, i + 18)
            elif pid == 0x0080:
                if i + 7 <= n:
                    gid = struct.unpack_from('<I', b, i + 2)[0]
                    if gid in self.mobs and b[i + 6] in (0, 1):
                        self.mobs.pop(gid, None)
                        if b[i + 6] == 1:
                            self.dead.add(gid)
            elif pid == 0x0A58 and i not in self.seen:
                # ZC_APPLY_MACRO_DETECTOR (8) is followed at once by the 0x0A59 image chunks
                if i + 12 > n:
                    return
                size = struct.unpack_from('<H', b, i + 2)[0]
                if 0 < size <= 20000 and b[i + 8] | (b[i + 9] << 8) == 0x0A59:
                    j, data = i + 8, b''
                    while len(data) < size:
                        if j + 8 > n:
                            return   # incomplete, retried with the next chunk
                        if b[j] | (b[j + 1] << 8) != 0x0A59:
                            break
                        ln = struct.unpack_from('<H', b, j + 2)[0]
                        if ln < 8 or j + ln > n:
                            return
                        data += bytes(b[j + 8:j + ln])
                        j += ln
                    if len(data) == size:
                        self.seen.add(i)
                        self.captcha_on = True
                        self.events.append((i, 'captcha_image', data))
            elif pid == 0x0A5B and self.captcha_on and i not in self.seen:
                # ZC_REQ_ANSWER_MACRO_DETECTOR: retry(1) timeout(4)
                if i + 7 > n:
                    return
                retry, timeout = b[i + 2], struct.unpack_from('<i', b, i + 3)[0]
                if 1 <= retry <= 10 and 1000 <= timeout <= 3600000:
                    self.seen.add(i)
                    self.events.append((i, 'captcha_ask', retry))
            elif pid == 0x0A5D and self.captcha_on and i not in self.seen:
                # ZC_CLOSE_MACRO_DETECTOR: status(4), 2 = MCD_GOOD
                if i + 6 > n:
                    return
                st = struct.unpack_from('<i', b, i + 2)[0]
                if 0 <= st <= 8:
                    self.seen.add(i)
                    self.events.append((i, 'captcha_close', st))
            elif pid == 0x09CB and i not in self.seen:
                # ZC_USE_SKILL2: skill(2) amount(4) target(4) src(4) result(1)
                if i + 17 <= n and struct.unpack_from('<I', b, i + 12)[0] == self.aid and \
                        struct.unpack_from('<H', b, i + 2)[0] in (28,):
                    self.seen.add(i)
                    self.events.append((i, 'skill', struct.unpack_from('<HiI', b, i + 2)))
            i += 1

    def _near_dialog(self, npc):
        """Fixed size dialog packets are only trusted for an NPC id already seen in mes/menu,
        or for the first close right after a command."""
        return any(e[1] in ('mes', 'menu') and e[2][0] == npc for e in self.events[-50:]) or self.expect_close

    expect_close = False
    drop_after = False
    pos = None
    captcha_on = False

    # ------------------------------------------------------------------ actions
    def raw(self, data):
        self.sock.sendall(data)

    def say(self, text):
        payload = f'{self.name} : {text}'.encode('cp949') + b'\0'
        self.sock.sendall(struct.pack('<HH', 0x00F3, 4 + len(payload)) + payload)

    def walk(self, x, y):
        p = bytes([(x >> 2) & 0xff, ((x << 6) | ((y >> 4) & 0x3f)) & 0xff, (y << 4) & 0xff])
        self.sock.sendall(struct.pack('<H', 0x035F) + p)

    def attack(self, gid):
        self.sock.sendall(struct.pack('<HIB', 0x0437, gid, 7))

    def use_skill(self, skill_id, level, target):
        """CZ_USE_SKILL2 (0x0438): level, skill id, target id. Refreshes idletime (idletime_option 0x2)."""
        self.sock.sendall(struct.pack('<HHHI', 0x0438, level, skill_id, target))

    def captcha_ack(self):
        """CZ_COMPLETE_APPLY_MACRO_DETECTOR_CAPTCHA (0x0A5A): image received, show the answer window."""
        self.sock.sendall(struct.pack('<H', 0x0A5A))

    def captcha_answer(self, text):
        """CZ_ACK_ANSWER_MACRO_DETECTOR (0x0A5C): answer[16]."""
        self.sock.sendall(struct.pack('<H16s', 0x0A5C, text.encode('latin1')))

    def wait_event(self, mark, kind, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            for e in self.events[mark:]:
                if e[1] == kind:
                    return e[2]
            time.sleep(0.1)
        return None

    def logout(self):
        try:
            self.sock.sendall(struct.pack('<HH', 0x018A, 0))
            time.sleep(1.0)
        finally:
            self.drop()

    def drop(self):
        self.alive = False
        try:
            self.sock.close()
        except Exception:
            pass

    # ------------------------------------------------------------------ texts / dialog
    def mark(self):
        return len(self.events)

    def texts_since(self, mark):
        return [e[2] for e in self.events[mark:] if e[1] == 'text']

    def wait_text(self, mark, needle, timeout=15.0):
        end = time.time() + timeout
        while time.time() < end:
            for t in self.texts_since(mark):
                if needle in t:
                    return t
            time.sleep(0.1)
        return None

    def command(self, text, settle=1.5):
        """@command without a dialog: answers the trailing close (0x00B6) so the next NPC talk works."""
        m = self.mark()
        self.expect_close = True
        self.say(text)
        end = time.time() + settle
        while time.time() < end:
            for e in self.events[m:]:
                if e[1] == 'close':
                    self.raw(struct.pack('<HI', 0x0146, e[2]))
                    self.expect_close = False
                    time.sleep(0.3)
                    return self.texts_since(m)
            time.sleep(0.1)
        self.expect_close = False
        return self.texts_since(m)

    def dialog(self, trigger, choose, timeout=20.0):
        """Runs a whole NPC dialog, started by a chat line (an @command) or, for an int trigger, by clicking that NPC id.
        choose(kind, items_or_none, mes_lines) returns
        - for 'menu': the label (substring) to pick, or None for ESC (0xFF)
        - for 'input': an int
        Returns (mes lines, texts, finished)."""
        m = self.mark()
        self.expect_close = True
        if isinstance(trigger, int):
            self.raw(struct.pack('<HIB', 0x0090, trigger, 1))   # CZ_CONTACTNPC: click the NPC with this game id
        else:
            self.say(trigger)
        pos = m
        mes = []
        end = time.time() + timeout
        while time.time() < end and self.alive:
            if pos >= len(self.events):
                time.sleep(0.05)
                continue
            _, kind, data = self.events[pos]
            pos += 1
            if kind == 'mes':
                mes.append(data[1])
            elif kind == 'next':
                time.sleep(0.05)
                self.raw(struct.pack('<HI', 0x00B9, data))
            elif kind == 'menu':
                npc, text = data
                items = [x for x in text.split(':') if x != '']
                label = choose('menu', items, mes)
                if label is None:
                    self.raw(struct.pack('<HIB', 0x00B8, npc, 0xFF))
                else:
                    idx = [k for k, x in enumerate(items) if label in x]
                    if not idx:
                        raise RuntimeError(f'menu item {label!r} not in {items}')
                    self.raw(struct.pack('<HIB', 0x00B8, npc, idx[0] + 1))
                if self.drop_after:
                    # simulated response loss: the choice reached the server, the reply is never read
                    time.sleep(0.02)
                    self.drop()
                    self.expect_close = False
                    return mes, self.texts_since(m), False
            elif kind == 'input':
                v = choose('input', None, mes)
                self.raw(struct.pack('<HIi', 0x0143, data, int(v)))
            elif kind == 'close':
                self.raw(struct.pack('<HI', 0x0146, data))
                self.expect_close = False
                time.sleep(0.3)
                return mes, self.texts_since(m), True
        self.expect_close = False
        return mes, self.texts_since(m), False
