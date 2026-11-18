"""NEED Chat Translate v1 - text handling of the relay (no I/O, no network).

- decode / encode the client code page (CP949) and cut at a character boundary,
- prefilter: lines that need no API call (laughter, symbols, numbers, single tokens, phrases),
- placeholders: game links, colour codes, URLs, refine values and ids are replaced before the
  model sees the line and restored byte-exactly afterwards; a translation that loses, adds or
  duplicates one is thrown away (the player still has the original line),
- output validation: one display line only, no control characters, no fake "[TL]" prefixes.
"""
import hashlib
import re
import unicodedata

CP = "cp949"

HANGUL_SYLLABLE = re.compile(r"[가-힣]")
HANGUL_ANY = re.compile(r"[ᄀ-ᇿ㄰-㆏가-힣]")
LATIN = re.compile(r"[A-Za-z]")
LAUGHTER = re.compile(
    r"^(?:[ㅋㅎㅠㅜㄷ]+|[\^~.!?;:\-_*+=()<>\[\]/\\ ,'\"`]+|(?:ha)+h?|(?:he)+h?|lo+l|lmao|xd+)+$",
    re.IGNORECASE,
)
ONLY_NUM_SYM = re.compile(r"^[\d\s\W_]+$", re.UNICODE)
WS = re.compile(r"\s+")
PHRASE_STRIP = re.compile(r"[\s!?.~^,]+")

# game markup the client renders: <ITEML>..</ITEML>, <ITEM>..</ITEM>, <NAVI>..</NAVI>, <INFO>..</INFO> ...
TAG_BLOCK = re.compile(r"<([A-Z]{2,12})>.*?</\1>", re.S)
COLOR = re.compile(r"\^[0-9A-Fa-f]{6}")
URL = re.compile(r"(?:https?://|www\.)\S+", re.IGNORECASE)
REFINE = re.compile(r"(?<![\w+])\+\d{1,2}(?!\d)")
LONG_NUMBER = re.compile(r"(?<!\d)\d{3,}(?!\d)")
AT_COMMAND = re.compile(r"(?<!\S)@[A-Za-z][A-Za-z0-9_]*")
PLACEHOLDER = re.compile(r"⟦(\d{1,2})⟧")	# ⟦n⟧
PH_SPACE_PUNCT = re.compile(r"(⟦\d{1,2}⟧)[ \t]+(,)")	# comma only: "url ." / "url ?" stay apart

FAKE_PREFIX = re.compile(r"^\s*(?:\[\s*(?:TL|번역)\s*\]\s*)+", re.IGNORECASE)

# short chats answered without the API (lower case, spaces / punctuation removed). Taken from the
# NEEDTranslator game API table, which already ran against real NEED chat.
PHRASES_KO_TO_EN = {
    "ㅎㅇ": "hi", "하이": "hi", "안녕": "hi", "안녕하세요": "hello",
    "ㅂㅇ": "bye", "ㅂㅂ": "bye", "바이": "bye",
    "ㄱㄱ": "let's go", "고고": "let's go",
    "ㅇㅋ": "ok", "오케이": "ok", "ㅇㅇ": "yeah", "응": "yeah", "ㄴㄴ": "no",
    "ㄳ": "thanks", "ㄱㅅ": "thanks", "감사": "thanks", "감사합니다": "thank you",
    "고마워": "thanks", "고마워요": "thank you",
    "ㅈㅅ": "sorry", "죄송합니다": "sorry", "미안": "sorry", "미안해요": "sorry",
    "ㅊㅋ": "congrats", "축하해요": "congratulations", "축하합니다": "congratulations",
    "수고하셨습니다": "good work", "수고하세요": "take care", "ㅅㄱ": "good work",
    "잠시만요": "one moment", "ㅈㄱ": "wait", "파티": "party",
}
PHRASES_EN_TO_KO = {
    "hi": "안녕하세요", "hello": "안녕하세요", "hey": "안녕하세요", "bye": "안녕히 가세요",
    "thx": "고마워요", "thanks": "고마워요", "thank you": "감사합니다", "ty": "고마워요",
    "np": "천만에요", "ok": "알겠어요", "okay": "알겠어요", "k": "알겠어요",
    "yes": "네", "no": "아니요", "sorry": "죄송해요", "gg": "수고하셨습니다", "gj": "잘했어요",
    "brb": "잠깐만요", "afk": "자리 비움", "wait": "잠깐만요", "pt": "파티", "party": "파티",
    "lfg": "파티 구해요", "lfp": "파티 구해요", "congrats": "축하해요", "grats": "축하해요",
    "gl": "행운을 빌어요",
}

# whole-line game terms that stay as they are (no API call, no translation line)
KEEP_TERMS = {
    "bg", "kvm", "woe", "mvp", "mvps", "ogh", "et", "gh", "bio", "thor", "ice dungeon",
    "@alootid", "@autoloot", "@autoloottype", "jack frost", "madogear", "big bell", "payon",
    "prontera", "geffen", "morroc", "izlude", "alberta", "juno", "rachel", "lighthalzen",
}


def decode(raw: bytes) -> str:
    return raw.decode(CP, errors="replace")


def normalize(text: str) -> str:
    return WS.sub(" ", unicodedata.normalize("NFC", text or "")).strip()


def text_hash(text: str, src: str, tgt: str) -> str:
    return hashlib.sha256(f"{src}>{tgt}\n{normalize(text).lower()}".encode("utf-8")).hexdigest()


def encode_cut(text: str, max_bytes: int) -> bytes:
    """CP949 bytes, characters CP949 cannot hold (emoji ...) dropped, cut on a character boundary."""
    out = bytearray()
    for ch in text:
        try:
            b = ch.encode(CP)
        except UnicodeEncodeError:
            continue
        if len(out) + len(b) > max_bytes:
            break
        out += b
    return bytes(out)


def detect(text: str):
    """'ko' when Hangul (syllables or jamo such as ㄱㄱ) is present, 'en' for latin letters only, else None."""
    if HANGUL_ANY.search(text):
        return "ko"
    if LATIN.search(text):
        return "en"
    return None


def prefilter(text: str, src: str, tgt: str):
    """Returns (kind, value). kind None = the API is needed.
    kinds: 'skip' (nothing to translate), 'phrase' (value = fixed translation)."""
    t = normalize(text)
    if not t:
        return "skip", None
    if LAUGHTER.match(t) or ONLY_NUM_SYM.match(t):
        return "skip", None
    key = PHRASE_STRIP.sub(" ", t.lower()).strip()
    table = PHRASES_KO_TO_EN if src == "ko" else PHRASES_EN_TO_KO
    if key in table and detect(t) == src:
        return "phrase", table[key]
    real = detect(t)
    if real is None:
        return "skip", None
    if real != src:		# the map-server and the relay disagree: do not guess
        return "skip", None
    if real == tgt:
        return "skip", None
    if key in KEEP_TERMS:
        return "skip", None
    stripped = mask(t)[0]
    stripped = PLACEHOLDER.sub(" ", stripped)
    letters = len(HANGUL_SYLLABLE.findall(stripped)) + len(LATIN.findall(stripped))
    if letters < 2:
        return "skip", None
    return None, None


def mask(text: str):
    """Replace protected spans with ⟦n⟧. Returns (masked, [originals])."""
    spans = []
    for rx in (TAG_BLOCK, URL, COLOR, AT_COMMAND, REFINE, LONG_NUMBER):
        for m in rx.finditer(text):
            s, e = m.span()
            if any(not (e <= a or s >= b) for a, b, _ in spans):
                continue
            spans.append((s, e, m.group(0)))
    spans.sort()
    if len(spans) > 30:
        spans = spans[:30]
    out, originals, pos = [], [], 0
    for s, e, g in spans:
        out.append(text[pos:s])
        out.append("⟦%d⟧" % len(originals))
        originals.append(g)
        pos = e
    out.append(text[pos:])
    return "".join(out), originals


def unmask(text: str, originals):
    """Restore placeholders. None when the set of placeholders is not exactly the original one."""
    found = [int(m.group(1)) for m in PLACEHOLDER.finditer(text)]
    if sorted(found) != list(range(len(originals))):
        return None
    # the model sometimes writes "⟦0⟧ , ..." - tidy only the gap it made right after a placeholder,
    # never inside a restored span (Gate C 2026-11-19: "item 501 , I reported ...")
    text = PH_SPACE_PUNCT.sub(r"\1\2", text)
    return PLACEHOLDER.sub(lambda m: originals[int(m.group(1))], text)


def clean_output(text: str):
    """One display line or None. Strips quotes / fake prefixes / control characters."""
    if text is None:
        return None
    t = text.strip()
    if not t or t.lower() in ("<skip>", "skip"):
        return None
    if "<skip>" in t.lower():
        return None
    t = t.replace("\r", " ").replace("\n", " ")
    t = "".join(ch for ch in t if unicodedata.category(ch)[0] != "C")
    for _ in range(2):	# quotes around a fake prefix, or a fake prefix before quotes
        if len(t) >= 2 and t[0] == t[-1] and t[0] in "\"'`":
            t = t[1:-1].strip()
        t = FAKE_PREFIX.sub("", t)
    t = WS.sub(" ", t).strip()
    return t or None


def plausible(src_text: str, out_text: str) -> bool:
    """Reject runaway outputs (the model explaining instead of translating)."""
    n = len(src_text)
    return 0 < len(out_text) <= max(4 * n + 40, 60)
