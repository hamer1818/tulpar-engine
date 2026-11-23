#!/usr/bin/env python3
"""tpr_tip.py — Tulpar shader alt kumesinin (.tprs) GPU TIP DENETIMI (Faz 8.2, 2026-11-23).

tpr_shader.py'nin ayristirdigi AST'yi GLSL'e uretmeden ONCE tipler. Amac: bir tip hatasi
`glslc`'ye kalmadan, `.tprs` satiriyla ve Turkce yakalanir (FAZ8.md 8.2 kapisi:
`vec3 c = texture(...)` artik cevirici katmaninda duser).

Kapsam (yalniz shader baglami; CPU tarafina dokunmaz):
  * skaler: float int uint bool  (+ `1u` literali; T3 `uint` burada TIP — Tulpar derleyicisinde
    hala siradan ad, o is TulparLang deposunda)
  * vec2-4 / ivec / uvec / bvec, mat2-4, opak ornekleyiciler, struct, `T[N]` / `T[]`
  * GLSL 4.50 ortuk donusumleri: int -> uint, int/uint -> float (vektorlerde ayni)
  * aritmetik / bit / karsilastirma / mantik islecleri, skaler-vektor-matris kurallari
  * swizzle (xyzw | rgba | stpq, karistirilamaz, boyutu asamaz), alan, indeks
  * kurucular (vecN/matN/struct: bilesen sayisi), alt kumenin yerlesikleri (imza kurallari)
  * kullanici fonksiyonu arguman/donus tipleri, demet (tdecl/tassign)
  * SALT OKUNUR hedefe yazma: girdi/tekduze/itme/depo/ornek/ozel/const
Bilerek DISARIDA: GLSL'in tam asiri yukleme cozumu (alt kume her yerlesigi tek kurala baglar),
sabit ifade degerlendirmesi, `inout` (alt kumede yok).

Kapinin kendisi: tpr_shader.py --tip-kontrol (23/23 temiz + bozulmus ornekler DUSMELI).
"""

SCALAR_RANK = {"int": 0, "uint": 1, "float": 2}
OPAQUE = {"sampler2D", "sampler2DShadow", "sampler2DArray", "samplerCube"}


class TipHatasi(Exception):
    def __init__(self, msg, line=0):
        super().__init__("satir %d: tip hatasi: %s" % (line, msg) if line else "tip hatasi: " + msg)
        self.line = line
        self.msg = "tip hatasi: " + msg


class Ty:
    """base: float|int|uint|bool|void|<struct>|<opak>|tuple; n: vektor boyu (skalerde 1);
    mat: matris sutun sayisi (0 = matris degil); arr: None | N | -1 (calisma-zamani dizi)."""
    __slots__ = ("base", "n", "mat", "arr", "items")

    def __init__(self, base, n=1, mat=0, arr=None, items=None):
        self.base, self.n, self.mat, self.arr, self.items = base, n, mat, arr, items

    def key(self):
        return (self.base, self.n, self.mat, self.arr,
                tuple(i.key() for i in self.items) if self.items else None)

    def __eq__(self, o):
        return isinstance(o, Ty) and self.key() == o.key()

    def __hash__(self):
        return hash(self.key())

    def elem(self):
        return Ty(self.base, self.n, self.mat)

    def with_arr(self, arr):
        return Ty(self.base, self.n, self.mat, arr)

    @property
    def scalar(self):
        return self.n == 1 and self.mat == 0 and self.arr is None and self.base in ("float", "int", "uint", "bool")

    @property
    def vector(self):
        return self.n > 1 and self.mat == 0 and self.arr is None

    @property
    def matrix(self):
        return self.mat > 0 and self.arr is None

    @property
    def numeric(self):
        return self.base in SCALAR_RANK and self.arr is None

    @property
    def integer(self):
        return self.base in ("int", "uint") and self.arr is None and self.mat == 0

    def __str__(self):
        if self.items:
            return "(" + ", ".join(str(i) for i in self.items) + ")"
        if self.mat:
            s = "mat%d" % self.mat
        elif self.n > 1:
            s = {"float": "vec", "int": "ivec", "uint": "uvec", "bool": "bvec"}[self.base] + str(self.n)
        else:
            s = self.base
        if self.arr is not None:
            s += "[]" if self.arr < 0 else "[%d]" % self.arr
        return s


VOID = Ty("void")
FLOAT, INT, UINT, BOOL = Ty("float"), Ty("int"), Ty("uint"), Ty("bool")


def named(name):
    """Tip adi -> Ty (struct ve opak adlar kendi taban adlariyla)."""
    for pre, base in (("vec", "float"), ("ivec", "int"), ("uvec", "uint"), ("bvec", "bool")):
        if name.startswith(pre) and name[len(pre):] in ("2", "3", "4"):
            return Ty(base, int(name[len(pre):]))
    if name in ("mat2", "mat3", "mat4"):
        k = int(name[3])
        return Ty("float", k, k)
    return Ty(name)


def vec_of(base, n):
    return Ty(base, n) if n > 1 else Ty(base)


def convertible(src, dst):
    """GLSL 4.50 ortuk donusum: ayni sekil, taban int->uint / int,uint->float."""
    if src == dst:
        return True
    if src.items or dst.items or src.arr != dst.arr or src.n != dst.n or src.mat != dst.mat:
        return False
    if src.base in SCALAR_RANK and dst.base in SCALAR_RANK:
        return SCALAR_RANK[src.base] < SCALAR_RANK[dst.base]
    return False


def common_base(a, b):
    if a in SCALAR_RANK and b in SCALAR_RANK:
        return a if SCALAR_RANK[a] >= SCALAR_RANK[b] else b
    return a if a == b else None


SWIZ = ("xyzw", "rgba", "stpq")
GL_VARS = {
    "gl_Position": Ty("float", 4), "gl_FragCoord": Ty("float", 4), "gl_VertexIndex": INT,
    "gl_InstanceIndex": INT, "gl_PointSize": FLOAT, "gl_FrontFacing": BOOL,
    "gl_WorkGroupID": Ty("uint", 3), "gl_LocalInvocationID": Ty("uint", 3),
    "gl_GlobalInvocationID": Ty("uint", 3), "gl_LocalInvocationIndex": UINT,
    "gl_NumWorkGroups": Ty("uint", 3), "gl_WorkGroupSize": Ty("uint", 3),
}
GL_READONLY = {"gl_FragCoord", "gl_VertexIndex", "gl_InstanceIndex", "gl_FrontFacing",
               "gl_WorkGroupID", "gl_LocalInvocationID", "gl_GlobalInvocationID",
               "gl_LocalInvocationIndex", "gl_NumWorkGroups", "gl_WorkGroupSize"}
CAST = {"f32": FLOAT, "i32": INT, "u32": UINT, "b32": BOOL}
READONLY_MARKERS = {"girdi", "girdi_duz", "tekduze", "itme", "depo", "ornek", "ozel", "is_grubu", "erken_test"}


class Sym:
    __slots__ = ("ty", "readonly", "why")

    def __init__(self, ty, readonly=False, why=""):
        self.ty, self.readonly, self.why = ty, readonly, why


class Checker:
    def __init__(self, decls):
        self.decls = decls
        self.structs = {}
        self.funcs = {}
        self.globals = {n: Sym(t, n in GL_READONLY, "yerlesik girdi") for n, t in GL_VARS.items()}
        self.scopes = []
        self.ret = None
        self.line = 0
        self.n_expr = 0   # tiplenen ifade sayisi (kapinin "bir sey olctu" kaniti)
        self.n_stmt = 0

    # -- yardimcilar -------------------------------------------------------
    def err(self, msg):
        raise TipHatasi(msg, self.line)

    def ty(self, t):
        """AST tip sozlugu -> Ty."""
        base = named(t["name"])
        if base.base not in SCALAR_RANK and base.base not in ("bool", "void") \
                and base.base not in OPAQUE and base.base not in self.structs:
            self.err("bilinmeyen tip %r" % t["name"])
        if t.get("array"):
            return base.with_arr(-1 if t.get("size") is None else t["size"])
        return base

    def lookup(self, name):
        for sc in reversed(self.scopes):
            if name in sc:
                return sc[name]
        if name in self.globals:
            return self.globals[name]
        self.err("tanimsiz ad %r" % name)

    def declare(self, name, sym):
        sc = self.scopes[-1]
        if name in sc:
            self.err("ad %r bu kapsamda iki kez bildirilmis" % name)
        sc[name] = sym

    def need_assign(self, src, dst, what):
        if not convertible(src, dst):
            self.err("%s: %s beklenirken %s verildi" % (what, dst, src))

    # -- program -------------------------------------------------------------
    def check(self):
        for d in self.decls:
            if d["k"] == "struct":
                self.structs[d["name"]] = d
        for d in self.decls:
            if d["k"] == "struct":
                self.line = d["line"]
                for f in d["fields"]:
                    self.ty(f["type"])
            elif d["k"] == "func":
                self.funcs[d["name"]] = d
        for d in self.decls:
            self.line = d.get("line", 0)
            if d["k"] == "marker":
                t = self.ty(d["type"])
                ro = d["marker"] in READONLY_MARKERS
                self.globals[d["name"]] = Sym(t, ro, "%s()" % d["marker"])
                if d["marker"] == "ozel" and len(d["args"]) == 2:
                    self.scopes = [{}]
                    self.need_assign(self.expr(d["args"][1]), t, "ozel() varsayilani")
                    self.scopes = []
        for d in self.decls:
            if d["k"] == "func":
                self.func(d)

    def func(self, d):
        self.line = d["line"]
        tup = d["ret"].get("tuple")
        self.ret = Ty("tuple", items=[self.ty(x) for x in tup]) if tup else self.ty(d["ret"])
        self.scopes = [{}]
        for p in d["params"]:
            self.declare(p["name"], Sym(self.ty(p["type"])))
        for s in d["body"]["stmts"]:
            self.stmt(s)
        self.scopes = []
        self.ret = None

    def sig(self, name):
        d = self.funcs[name]
        tup = d["ret"].get("tuple")
        ret = Ty("tuple", items=[self.ty(x) for x in tup]) if tup else self.ty(d["ret"])
        return [self.ty(p["type"]) for p in d["params"]], ret

    # -- deyimler --------------------------------------------------------------
    def block(self, s):
        self.scopes.append({})
        for x in s["stmts"]:
            self.stmt(x)
        self.scopes.pop()

    def body(self, s):
        if s["k"] == "block":
            self.block(s)
        else:
            self.scopes.append({})
            self.stmt(s)
            self.scopes.pop()

    def cond(self, e, what):
        t = self.expr(e)
        if t != BOOL:
            self.err("%s kosulu bool olmali, %s verildi" % (what, t))

    def stmt(self, s):
        self.n_stmt += 1
        if "line" in s:
            self.line = s["line"]
        k = s["k"]
        if k == "block":
            self.block(s)
        elif k == "decl":
            t = self.ty(s["type"])
            init = s["init"]
            if init is not None:
                if init["k"] == "array":
                    if t.arr is None or t.arr < 0:
                        self.err("dizi literali sabit boy dizi bildirimi ister")
                    if len(init["items"]) != t.arr:
                        self.err("dizi %d oge ister, %d verildi" % (t.arr, len(init["items"])))
                    for i, x in enumerate(init["items"]):
                        self.need_assign(self.expr(x), t.elem(), "dizi ogesi %d" % i)
                else:
                    self.need_assign(self.expr(init), t, "%r bildirimi" % s["name"])
            self.declare(s["name"], Sym(t, bool(s.get("const")), "const"))
        elif k == "tdecl":
            items = self.tuple_of(s["init"], len(s["vars"]))
            for (ty, name), it in zip(s["vars"], items):
                t = self.ty(ty)
                self.need_assign(it, t, "demet ogesi %r" % name)
                self.declare(name, Sym(t))
        elif k == "tassign":
            items = self.tuple_of(s["init"], len(s["names"]))
            for name, it in zip(s["names"], items):
                sym = self.lookup(name)
                self.writable(sym, name)
                self.need_assign(it, sym.ty, "demet atamasi %r" % name)
        elif k == "assign":
            tt = self.lvalue(s["target"])
            vt = self.expr(s["value"])
            if s["op"] == "=":
                self.need_assign(vt, tt, "atama")
            else:
                op = s["op"][:-1]
                rt = self.binary(op, tt, vt)
                self.need_assign(rt, tt, "'%s' atamasi" % s["op"])
        elif k == "expr":
            self.expr(s["expr"])
        elif k == "if":
            self.cond(s["cond"], "if")
            self.body(s["then"])
            if s["else"] is not None:
                self.body(s["else"])
        elif k == "while":
            self.cond(s["cond"], "while")
            self.body(s["body"])
        elif k == "for":
            self.scopes.append({})
            if s["init"]:
                self.stmt(s["init"])
            if s["cond"]:
                self.cond(s["cond"], "for")
            if s["step"]:
                self.stmt(s["step"])
            self.body(s["body"])
            self.scopes.pop()
        elif k == "return":
            vals = s["values"] or ([s["value"]] if s["value"] is not None else [])
            if self.ret.items:
                if len(vals) != len(self.ret.items):
                    self.err("demet %d deger ister, %d verildi" % (len(self.ret.items), len(vals)))
                for i, (v, t) in enumerate(zip(vals, self.ret.items)):
                    self.need_assign(self.expr(v), t, "donus ogesi %d" % i)
            elif self.ret == VOID:
                if vals:
                    self.err("void fonksiyon deger donduremez")
            else:
                if len(vals) != 1:
                    self.err("fonksiyon %s dondurmeli" % self.ret)
                self.need_assign(self.expr(vals[0]), self.ret, "donus")
        elif k in ("discard", "break", "continue"):
            pass
        else:
            self.err("denetlenemeyen deyim %r" % k)

    def tuple_of(self, init, n):
        if init["k"] != "call" or init["fn"]["k"] != "id" or init["fn"]["name"] not in self.funcs:
            self.err("demet acma yalniz bu dosyadaki fonksiyon cagrisindan")
        t = self.expr(init)
        if not t.items:
            self.err("%r demet dondurmuyor" % init["fn"]["name"])
        if len(t.items) != n:
            self.err("%r %d deger donduruyor, %d ad verildi" % (init["fn"]["name"], len(t.items), n))
        return t.items

    # -- sol deger -----------------------------------------------------------
    def root(self, e):
        while e["k"] in ("member", "index", "paren"):
            e = e["base"] if e["k"] != "paren" else e["e"]
        return e

    def writable(self, sym, name):
        if sym.readonly:
            self.err("%r salt okunur (%s), yazilamaz" % (name, sym.why))

    def lvalue(self, e):
        r = self.root(e)
        if r["k"] != "id":
            self.err("atama hedefi bir degisken olmali")
        self.writable(self.lookup(r["name"]), r["name"])
        if e["k"] == "member":
            bt = self.expr(e["base"])
            if bt.n > 1 and bt.mat == 0 and bt.arr is None:
                sw = e["name"]
                if len(set(sw)) != len(sw):
                    self.err("tekrarli swizzle %r atama hedefi olamaz" % sw)
        return self.expr(e)

    # -- ifadeler --------------------------------------------------------------
    def expr(self, e):
        self.n_expr += 1
        k = e["k"]
        if k == "num":
            if e["kind"] == "float":
                return FLOAT
            return UINT if e["text"][-1] in "uU" else INT
        if k == "bool":
            return BOOL
        if k == "str":
            self.err("dize shader'da kullanilamaz")
        if k == "array":
            self.err("dizi literali yalniz T[N] bildiriminin baslaticisi olabilir")
        if k == "id":
            return self.lookup(e["name"]).ty
        if k == "paren":
            return self.expr(e["e"])
        if k == "un":
            t = self.expr(e["e"])
            if e["op"] == "!":
                if t != BOOL:
                    self.err("'!' bool ister, %s verildi" % t)
                return t
            if e["op"] == "~":
                if not t.integer:
                    self.err("'~' tamsayi ister, %s verildi" % t)
                return t
            if not t.numeric:
                self.err("'%s' sayisal tip ister, %s verildi" % (e["op"], t))
            return t
        if k == "post":
            t = self.lvalue(e["e"])
            if not t.numeric or t.mat:
                self.err("'%s' sayisal skaler/vektor ister, %s verildi" % (e["op"], t))
            return t
        if k == "bin":
            return self.binary(e["op"], self.expr(e["l"]), self.expr(e["r"]))
        if k == "ternary":
            c = self.expr(e["cond"])
            if c != BOOL:
                self.err("'?:' kosulu bool olmali, %s verildi" % c)
            a, b = self.expr(e["a"]), self.expr(e["b"])
            if convertible(b, a):
                return a
            if convertible(a, b):
                return b
            self.err("'?:' kollari uyusmuyor: %s / %s" % (a, b))
        if k == "index":
            bt, it = self.expr(e["base"]), self.expr(e["index"])
            if not (it.scalar and it.integer):
                self.err("indeks int/uint olmali, %s verildi" % it)
            if bt.arr is not None:
                return bt.elem()
            if bt.mat:
                return Ty("float", bt.n)
            if bt.n > 1:
                return Ty(bt.base)
            self.err("%s indekslenemez" % bt)
        if k == "member":
            bt = self.expr(e["base"])
            name = e["name"]
            if bt.arr is None and bt.mat == 0 and bt.base in self.structs and bt.n == 1:
                for f in self.structs[bt.base]["fields"]:
                    if f["name"] == name:
                        return self.ty(f["type"])
                self.err("%s yapisinda %r alani yok" % (bt.base, name))
            if bt.arr is None and bt.mat == 0 and bt.base in ("float", "int", "uint", "bool"):
                return self.swizzle(bt, name)
            self.err("%s uzerinde .%s yok" % (bt, name))
        if k == "call":
            return self.call(e)
        self.err("denetlenemeyen ifade %r" % k)

    def swizzle(self, bt, sw):
        if not 1 <= len(sw) <= 4:
            self.err("swizzle %r 1-4 bilesen olmali" % sw)
        for alfabe in SWIZ:
            if all(c in alfabe for c in sw):
                if any(alfabe.index(c) >= bt.n for c in sw):
                    self.err("%s icin swizzle %r boyutu asiyor" % (bt, sw))
                return vec_of(bt.base, len(sw))
        self.err("gecersiz swizzle %r (xyzw | rgba | stpq, karistirilamaz)" % sw)

    def binary(self, op, a, b):
        if a.arr is not None or b.arr is not None or a.items or b.items:
            self.err("'%s' dizi/demet uzerinde tanimsiz (%s, %s)" % (op, a, b))
        if op in ("&&", "||"):
            if a != BOOL or b != BOOL:
                self.err("'%s' bool ister, %s / %s verildi" % (op, a, b))
            return BOOL
        if op in ("==", "!="):
            if not (convertible(a, b) or convertible(b, a)):
                self.err("'%s' karsilastirmasi uyusmuyor: %s / %s" % (op, a, b))
            return BOOL
        if op in ("<", ">", "<=", ">="):
            if not (a.scalar and b.scalar and a.numeric and b.numeric) or common_base(a.base, b.base) is None:
                self.err("'%s' sayisal skaler ister, %s / %s verildi" % (op, a, b))
            return BOOL
        if op in ("<<", ">>"):
            if not (a.integer and b.integer):
                self.err("'%s' tamsayi ister, %s / %s verildi" % (op, a, b))
            if b.n > 1 and b.n != a.n:
                self.err("'%s' vektor boylari uyusmuyor: %s / %s" % (op, a, b))
            return a
        if op in ("&", "|", "^", "%"):
            if not (a.integer and b.integer):
                self.err("'%s' tamsayi ister, %s / %s verildi" % (op, a, b))
            return self.shape(op, a, b, common_base(a.base, b.base))
        # + - * /
        if not (a.numeric and b.numeric):
            self.err("'%s' sayisal tip ister, %s / %s verildi" % (op, a, b))
        base = common_base(a.base, b.base)
        if a.mat or b.mat:
            if base != "float":
                self.err("matris islemi float ister (%s / %s)" % (a, b))
            if op == "*":
                if a.mat and b.mat:
                    if a.mat != b.n:
                        self.err("matris carpimi boyutu uyusmuyor: %s * %s" % (a, b))
                    return Ty("float", a.n, b.mat)
                if a.mat and b.vector:
                    if a.mat != b.n:
                        self.err("%s * %s boyutu uyusmuyor" % (a, b))
                    return Ty("float", a.n)
                if a.vector and b.mat:
                    if a.n != b.n:
                        self.err("%s * %s boyutu uyusmuyor" % (a, b))
                    return Ty("float", b.mat)
            if a.mat and b.scalar:
                return a
            if b.mat and a.scalar:
                return b
            if a == b:
                return a
            self.err("'%s' matris islemi uyusmuyor: %s / %s" % (op, a, b))
        return self.shape(op, a, b, base)

    def shape(self, op, a, b, base):
        if base is None:
            self.err("'%s' taban tipleri uyusmuyor: %s / %s" % (op, a, b))
        if a.n == b.n:
            return vec_of(base, a.n)
        if a.n == 1:
            return vec_of(base, b.n)
        if b.n == 1:
            return vec_of(base, a.n)
        self.err("'%s' vektor boylari uyusmuyor: %s / %s" % (op, a, b))

    # -- cagrilar ------------------------------------------------------------
    def call(self, e):
        fn = e["fn"]
        if fn["k"] != "id":
            self.err("yalniz adla cagri desteklenir")
        name = fn["name"]
        args = [self.expr(a) for a in e["args"]]
        if name in CAST:
            # GLSL skaler kurucusu: tek arguman; vektor/matris verilirse ILK bileseni alir (gecerli GLSL —
            # bunu reddetmek denetimi glslc'den KATI yapiyordu, --tip-kontrol'un capraz dogrulamasi yakaladi).
            if len(args) != 1 or not (args[0].scalar or args[0].vector or args[0].mat):
                self.err("%s() tek skaler/vektor arguman ister, %d verildi" % (name, len(args)))
            return CAST[name]
        if name in self.funcs:
            params, ret = self.sig(name)
            if len(args) != len(params):
                self.err("%s() %d arguman ister, %d verildi" % (name, len(params), len(args)))
            for i, (a, p) in enumerate(zip(args, params)):
                self.need_assign(a, p, "%s() arguman %d" % (name, i + 1))
            return ret
        if name in self.structs:
            fields = [self.ty(f["type"]) for f in self.structs[name]["fields"]]
            if len(args) != len(fields):
                self.err("%s kurucusu %d alan ister, %d verildi" % (name, len(fields), len(args)))
            for i, (a, f) in enumerate(zip(args, fields)):
                self.need_assign(a, f, "%s alani %d" % (name, i + 1))
            return Ty(name)
        t = named(name)
        if t.base in SCALAR_RANK or t.base == "bool":
            return self.construct(t, args)
        rule = BUILTINS.get(name)
        if rule is None:
            self.err("bilinmeyen fonksiyon %r" % name)
        return rule(self, name, args)

    def construct(self, t, args):
        if not args:
            self.err("%s kurucusu bos olamaz" % t)
        for a in args:
            if a.arr is not None or a.items or not (a.base in SCALAR_RANK or a.base == "bool"):
                self.err("%s kurucusuna %s verilemez" % (t, a))
        if t.mat:
            if len(args) == 1 and (args[0].scalar or args[0].mat):
                return t
            total = sum(a.n * (a.mat or 1) for a in args)
            if total != t.mat * t.n:
                self.err("%s kurucusu %d bilesen ister, %d verildi" % (t, t.mat * t.n, total))
            return t
        if len(args) == 1 and args[0].scalar:
            return t
        total = sum(a.n * (a.mat or 1) for a in args)
        if len(args) == 1 and total >= t.n:
            return t  # vec3(v4) -> ilk uc bilesen (GLSL'de gecerli)
        if total != t.n:
            self.err("%s kurucusu %d bilesen ister, %d verildi" % (t, t.n, total))
        return t


# ---------------------------------------------------------------------------
# Yerlesik imzalar: her ad tek bir kurala baglanir (alt kume tam asiri yukleme cozumu yapmaz)
# ---------------------------------------------------------------------------

def _argc(c, name, args, *ns):
    if len(args) not in ns:
        c.err("%s() %s arguman ister, %d verildi" % (name, "/".join(map(str, ns)), len(args)))


def _genf(c, name, a):
    if not (a.base == "float" or a.base in ("int", "uint")) or a.mat or a.arr is not None:
        c.err("%s() float skaler/vektor ister, %s verildi" % (name, a))
    return vec_of("float", a.n)


def r_genf(c, name, args):
    _argc(c, name, args, 1)
    return _genf(c, name, args[0])


def r_gen_any(c, name, args):  # abs, sign: float/int skaler/vektor
    _argc(c, name, args, 1)
    a = args[0]
    if not a.numeric or a.mat:
        c.err("%s() sayisal skaler/vektor ister, %s verildi" % (name, a))
    return a


def _same_or_scalar(c, name, x, y, idx):
    if convertible(y, x) or (y.scalar and y.numeric and convertible(y, Ty(x.base))):
        return
    c.err("%s() arguman %d: %s ya da %s skaleri beklenirken %s verildi" % (name, idx, x, x.base, y))


def r_minmax(c, name, args):
    _argc(c, name, args, 2)
    x, y = args
    if not x.numeric or x.mat:
        c.err("%s() sayisal skaler/vektor ister, %s verildi" % (name, x))
    if convertible(x, y) and x != y:
        return y
    _same_or_scalar(c, name, x, y, 2)
    return x


def r_clamp(c, name, args):
    _argc(c, name, args, 3)
    x = args[0]
    if not x.numeric or x.mat:
        c.err("clamp() sayisal skaler/vektor ister, %s verildi" % x)
    if x.base != "float" and any(a.base == "float" for a in args[1:]):
        x = vec_of("float", x.n)
    for i, a in enumerate(args[1:], 2):
        _same_or_scalar(c, name, x, a, i)
    return x


def r_mix(c, name, args):
    _argc(c, name, args, 3)
    x = _genf(c, name, args[0])
    c.need_assign(args[1], x, "mix() arguman 2")
    a = args[2]
    if a.base == "bool" and a.n == x.n:
        return x
    _same_or_scalar(c, name, x, a, 3)
    return x


def r_pow(c, name, args):
    _argc(c, name, args, 2)
    x = _genf(c, name, args[0])
    if name == "mod":
        _same_or_scalar(c, name, x, args[1], 2)
    else:
        c.need_assign(args[1], x, "%s() arguman 2" % name)
    return x


def r_atan(c, name, args):
    _argc(c, name, args, 1, 2)
    x = _genf(c, name, args[0])
    if len(args) == 2:
        c.need_assign(args[1], x, "atan() arguman 2")
    return x


def r_step(c, name, args):
    _argc(c, name, args, 2 if name == "step" else 3)
    x = _genf(c, name, args[-1])
    for i, a in enumerate(args[:-1], 1):
        _same_or_scalar(c, name, x, a, i)
    return x


def r_dot(c, name, args):
    _argc(c, name, args, 2)
    x = _genf(c, name, args[0])
    c.need_assign(args[1], x, "%s() arguman 2" % name)
    return FLOAT


def r_length(c, name, args):
    _argc(c, name, args, 1)
    _genf(c, name, args[0])
    return FLOAT


def r_cross(c, name, args):
    _argc(c, name, args, 2)
    for i, a in enumerate(args, 1):
        c.need_assign(a, Ty("float", 3), "cross() arguman %d" % i)
    return Ty("float", 3)


def r_reflect(c, name, args):
    _argc(c, name, args, 2)
    x = _genf(c, name, args[0])
    c.need_assign(args[1], x, "reflect() arguman 2")
    return x


SAMPLER_COORD = {"sampler2D": 2, "sampler2DShadow": 3, "sampler2DArray": 3, "samplerCube": 3}


def _sampler(c, name, s):
    if s.base not in OPAQUE or s.arr is not None:
        c.err("%s() ilk arguman ornekleyici ister, %s verildi" % (name, s))
    return SAMPLER_COORD[s.base]


def r_texture(c, name, args):
    _argc(c, name, args, 2, 3)
    n = _sampler(c, name, args[0])
    c.need_assign(args[1], Ty("float", n), "%s() koordinati" % name)
    if len(args) == 3:
        c.need_assign(args[2], FLOAT, "%s() sapma" % name)
    return FLOAT if args[0].base == "sampler2DShadow" else Ty("float", 4)


def r_texlod(c, name, args):
    _argc(c, name, args, 3)
    n = _sampler(c, name, args[0])
    c.need_assign(args[1], Ty("float", n), "textureLod() koordinati")
    c.need_assign(args[2], FLOAT, "textureLod() lod")
    return FLOAT if args[0].base == "sampler2DShadow" else Ty("float", 4)


def r_texel(c, name, args):
    _argc(c, name, args, 3)
    n = _sampler(c, name, args[0])
    c.need_assign(args[1], Ty("int", n), "texelFetch() koordinati")
    c.need_assign(args[2], INT, "texelFetch() lod")
    return Ty("float", 4)


def r_texsize(c, name, args):
    _argc(c, name, args, 2)
    n = _sampler(c, name, args[0])
    c.need_assign(args[1], INT, "textureSize() lod")
    return Ty("int", 2 if args[0].base in ("sampler2D", "sampler2DShadow", "samplerCube") else n)


def r_mat1(c, name, args):
    _argc(c, name, args, 1)
    m = args[0]
    if not m.mat:
        c.err("%s() matris ister, %s verildi" % (name, m))
    return FLOAT if name == "determinant" else m


def r_vcmp(c, name, args):
    _argc(c, name, args, 2)
    a, b = args
    if not a.vector or not (convertible(a, b) or convertible(b, a)):
        c.err("%s() ayni boylu iki vektor ister, %s / %s verildi" % (name, a, b))
    return Ty("bool", a.n)


def r_anyall(c, name, args):
    _argc(c, name, args, 1)
    if args[0].base != "bool" or not args[0].vector:
        c.err("%s() bvec ister, %s verildi" % (name, args[0]))
    return BOOL


def r_bits(c, name, args):
    _argc(c, name, args, 1)
    if not args[0].integer:
        c.err("%s() int/uint ister, %s verildi" % (name, args[0]))
    return vec_of("int", args[0].n)


def r_barrier(c, name, args):
    _argc(c, name, args, 0)
    return VOID


def r_atomic(c, name, args):
    _argc(c, name, args, 3 if name == "atomicCompSwap" else 2)
    t = args[0]
    if not (t.scalar and t.integer):
        c.err("%s() int/uint bellek ister, %s verildi" % (name, t))
    for i, a in enumerate(args[1:], 2):
        c.need_assign(a, t, "%s() arguman %d" % (name, i))
    return t


def r_bitcast(c, name, args):
    _argc(c, name, args, 1)
    a = args[0]
    src = {"floatBitsToUint": "float", "floatBitsToInt": "float",
           "uintBitsToFloat": "uint", "intBitsToFloat": "int"}[name]
    dst = {"floatBitsToUint": "uint", "floatBitsToInt": "int",
           "uintBitsToFloat": "float", "intBitsToFloat": "float"}[name]
    if a.base != src or a.mat or a.arr is not None:
        c.err("%s() %s skaler/vektor ister, %s verildi" % (name, src, a))
    return vec_of(dst, a.n)


BUILTINS = {}
for _n in ("normalize", "floor", "ceil", "fract", "round", "sqrt", "inversesqrt", "exp", "exp2",
           "log", "log2", "sin", "cos", "tan", "asin", "acos", "fwidth", "dFdx", "dFdy"):
    BUILTINS[_n] = r_genf
BUILTINS.update({
    "abs": r_gen_any, "sign": r_gen_any, "min": r_minmax, "max": r_minmax, "clamp": r_clamp,
    "mix": r_mix, "pow": r_pow, "mod": r_pow, "atan": r_atan, "step": r_step, "smoothstep": r_step,
    "dot": r_dot, "distance": r_dot, "length": r_length, "cross": r_cross, "reflect": r_reflect,
    "texture": r_texture, "textureLod": r_texlod, "texelFetch": r_texel, "textureSize": r_texsize,
    "transpose": r_mat1, "inverse": r_mat1, "determinant": r_mat1,
    "greaterThan": r_vcmp, "lessThan": r_vcmp, "equal": r_vcmp, "any": r_anyall, "all": r_anyall,
    "findLSB": r_bits, "findMSB": r_bits, "bitCount": r_bits,
    "barrier": r_barrier, "memoryBarrier": r_barrier, "memoryBarrierShared": r_barrier,
    "groupMemoryBarrier": r_barrier,
    "floatBitsToUint": r_bitcast, "floatBitsToInt": r_bitcast, "uintBitsToFloat": r_bitcast,
    "intBitsToFloat": r_bitcast,
})
for _n in ("atomicAdd", "atomicMin", "atomicMax", "atomicAnd", "atomicOr", "atomicXor",
           "atomicExchange", "atomicCompSwap"):
    BUILTINS[_n] = r_atomic


def check(decls):
    """AST'yi tiple; ilk hatada TipHatasi. Donus: (tiplenen ifade, deyim) sayilari."""
    c = Checker(decls)
    c.check()
    return c.n_expr, c.n_stmt
