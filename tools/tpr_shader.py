#!/usr/bin/env python3
"""tpr_shader.py — Tulpar shader alt kumesi (.tprs) -> GLSL 450 -> SPIR-V.

FAZ 8 FIZIBILITE PROTOTIPI. Tasarim ve olculen kapsam: docs/engine/FAZ8.md.

Ne yapar
--------
`.tprs` dosyalari TULPAR SOZDIZIMINDEDIR — baska bir dil degil. Dosyanin
tamami bugunku `./tulpar` ayristiricisindan parse hatasiz gecer (denetim:
`--tulpar-parse`); eksik olan tip sistemi, gramer degil. Shader'a ozgu
bildirimler (girdi/cikti/ornekleyici/uniform/push) siradan Tulpar degisken
bildirimleridir; baslatici bir ISARET CAGRISIDIR:

    str asama = "frag";                  // asama: "vert" | "frag"
    vec2 v_uv     = girdi(0);            // layout(location=0) in vec2 v_uv;
    float v_e     = girdi_duz(2);        // flat in
    vec4 o_color  = cikti(0);            // layout(location=0) out vec4 o_color;
    sampler2D u_s = ornek(0, 0);         // layout(set=0,binding=0) uniform sampler2D
    Frame u       = tekduze(0, 0);       // uniform blok (struct Frame)
    Push pc       = itme();              // push_constant blok
    Skin s        = depo(0, 4);          // readonly std430 SSBO   (depo_yaz: writeonly)
    float k       = ozel(0, 1.0);        // layout(constant_id=0) const

Skaler donusum `float(x)`/`int(x)` BUGUNKU TULPAR GRAMERINDE AYRISMIYOR
(`float`/`int` anahtar kelime, cagrilabilir ad degil). Alt kume bu yuzden
`f32(x)` / `i32(x)` / `u32(x)` yazar. Bu bir tercih degil, olculmus bir
gramer bosluğunun gecici karsiligi (FAZ8.md, bosluk G1).

Kullanim
--------
    tpr_shader.py <dosya.tprs>                 # GLSL'i stdout'a yaz
    tpr_shader.py <dosya.tprs> -o out.frag     # GLSL'i dosyaya yaz
    tpr_shader.py <dosya.tprs> --spv out.spv   # glslc ile SPIR-V uret
    tpr_shader.py --check [dizin]              # uret + glslc + referans GLSL'in
                                               # ayni glslc derlemesiyle BAYT karsilastir
    tpr_shader.py --tulpar-parse [dizin]       # .tprs'leri ./tulpar ile parse et
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
ASSETS = os.path.join(ENGINE, "tests", "assets", "shader")
REPO_SHADERS = os.path.join(ENGINE, "rhi", "shaders")


class ShaderError(Exception):
    def __init__(self, msg, line=0):
        super().__init__("satir %d: %s" % (line, msg) if line else msg)
        self.line = line


# ---------------------------------------------------------------------------
# Sozcuk cozumleyici
# ---------------------------------------------------------------------------

PUNCT = [
    "<<=", ">>=", "&&", "||", "==", "!=", "<=", ">=", "++", "--",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>",
    "(", ")", "{", "}", "[", "]", ";", ",", ":", ".", "?",
    "+", "-", "*", "/", "%", "=", "<", ">", "!", "&", "|", "^", "~",
]

# Tulpar'in kendi anahtar kelimeleri + alt kumenin kullandiklari.
KEYWORDS = {"func", "return", "if", "else", "while", "for", "struct", "type",
            "true", "false", "discard", "break", "continue"}


class Tok:
    __slots__ = ("kind", "text", "line")

    def __init__(self, kind, text, line):
        self.kind, self.text, self.line = kind, text, line

    def __repr__(self):
        return "%s(%r)" % (self.kind, self.text)


def lex(src):
    toks, i, line, n = [], 0, 1, len(src)
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c in " \t\r":
            i += 1
            continue
        if src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            if j < 0:
                raise ShaderError("kapanmamis blok yorumu", line)
            line += src.count("\n", i, j)
            i = j + 2
            continue
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 1
            if j >= n:
                raise ShaderError("kapanmamis dize", line)
            toks.append(Tok("str", src[i + 1:j], line))
            i = j + 1
            continue
        if c.isdigit() or (c == "." and i + 1 < n and src[i + 1].isdigit()):
            m = re.match(r"\d+\.\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?"
                         r"|\d+[eE][+-]?\d+|\d+[uU]?", src[i:])
            txt = m.group(0)
            kind = "float" if re.search(r"[.eE]", txt) else "int"
            toks.append(Tok(kind, txt, line))
            i += len(txt)
            continue
        if c.isalpha() or c == "_":
            m = re.match(r"[A-Za-z_][A-Za-z_0-9]*", src[i:])
            txt = m.group(0)
            toks.append(Tok("kw" if txt in KEYWORDS else "id", txt, line))
            i += len(txt)
            continue
        for p in PUNCT:
            if src.startswith(p, i):
                toks.append(Tok("op", p, line))
                i += len(p)
                break
        else:
            raise ShaderError("tanimsiz karakter %r" % c, line)
    toks.append(Tok("eof", "", line))
    return toks


# ---------------------------------------------------------------------------
# Tipler ve yerlesik adlar
# ---------------------------------------------------------------------------

SCALARS = {"float", "int", "uint", "bool", "void"}
VECMAT = {"vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3",
          "uvec4", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4"}
OPAQUE = {"sampler2D", "sampler2DShadow", "sampler2DArray", "samplerCube"}
BUILTIN_TYPES = SCALARS | VECMAT | OPAQUE

# Cagirilabilen GLSL yerlesikleri (alt kumenin izin verdikleri).
BUILTIN_FUNCS = {
    "texture", "textureLod", "texelFetch", "textureSize",
    "dot", "cross", "normalize", "length", "distance", "reflect",
    "min", "max", "clamp", "mix", "step", "smoothstep",
    "abs", "sign", "floor", "ceil", "fract", "mod", "round",
    "sqrt", "inversesqrt", "pow", "exp", "exp2", "log", "log2",
    "sin", "cos", "tan", "asin", "acos", "atan",
    "fwidth", "dFdx", "dFdy", "transpose", "inverse", "determinant",
    "greaterThan", "lessThan", "any", "all", "equal",
    "findLSB", "findMSB", "bitCount",
    # compute (T20, 2026-11-05)
    "barrier", "memoryBarrier", "memoryBarrierShared", "groupMemoryBarrier",
    "atomicAdd", "atomicMin", "atomicMax", "atomicAnd", "atomicOr",
    "atomicXor", "atomicExchange", "atomicCompSwap",
    "floatBitsToUint", "floatBitsToInt", "uintBitsToFloat", "intBitsToFloat",
} | VECMAT

# Alt kumenin skaler donusum adlari (gramer bosluğu G1'in karsiligi).
CAST_ALIAS = {"f32": "float", "i32": "int", "u32": "uint", "b32": "bool"}

GL_BUILTIN_VARS = {"gl_Position", "gl_FragCoord", "gl_VertexIndex",
                   "gl_InstanceIndex", "gl_PointSize", "gl_FrontFacing",
                   "gl_WorkGroupID", "gl_LocalInvocationID",
                   "gl_GlobalInvocationID", "gl_LocalInvocationIndex",
                   "gl_NumWorkGroups", "gl_WorkGroupSize"}

MARKERS = {"girdi", "girdi_duz", "cikti", "cikti_duz", "ornek", "tekduze",
           "itme", "depo", "depo_yaz", "depo_oy", "ozel", "is_grubu",
           "paylasimli"}


# ---------------------------------------------------------------------------
# Ayristirici — AST dugumleri duz sozluk
# ---------------------------------------------------------------------------

class Parser:
    def __init__(self, toks):
        self.t = toks
        self.i = 0

    def cur(self):
        return self.t[self.i]

    def at(self, text):
        return self.cur().text == text and self.cur().kind in ("op", "kw")

    def at_kind(self, kind):
        return self.cur().kind == kind

    def next(self, k=1):
        return self.t[min(self.i + k, len(self.t) - 1)]

    def eat(self, text):
        if self.at(text):
            self.i += 1
            return True
        return False

    def want(self, text):
        if not self.eat(text):
            raise ShaderError("%r bekleniyordu, %r bulundu"
                              % (text, self.cur().text), self.cur().line)

    def want_id(self):
        if self.cur().kind != "id":
            raise ShaderError("ad bekleniyordu, %r bulundu" % self.cur().text,
                              self.cur().line)
        tok = self.cur()
        self.i += 1
        return tok.text

    # -- tip --------------------------------------------------------------
    def is_type_start(self):
        c = self.cur()
        if c.kind == "id":
            return True
        return c.kind == "kw" and c.text in ("struct", "type")

    def parse_type(self):
        """Tip adi + istege bagli dizi soneki.

        `T[]`  — bicimlenmemis calisma-zamani dizi (SSBO).
        `T[N]` — SABIT boy. Bu ONEK yazim bugunku Tulpar gramerinde AYRISMIYOR
                 (FAZ8.md, bosluk G3); alt kume onu ilerideki gramer icin
                 kullanir, `--tulpar-parse` denetimi bunu ACIKCA kirmizi
                 gosterir. Olculecek sey tam olarak bu ayrim.
        """
        name = self.want_id()
        arr, size = False, None
        if self.at("["):
            self.i += 1
            if self.at("]"):
                self.i += 1
                arr = True
            else:
                tok = self.cur()
                if tok.kind != "int":
                    raise ShaderError("dizi boyu sabit tamsayi olmali", tok.line)
                self.i += 1
                self.want("]")
                arr, size = True, int(tok.text)
        return {"name": name, "array": arr, "size": size}

    # -- program ----------------------------------------------------------
    def parse_program(self):
        decls = []
        while not self.at_kind("eof"):
            decls.append(self.parse_toplevel())
        return decls

    def parse_toplevel(self):
        line = self.cur().line
        if self.at("struct") or self.at("type"):
            return self.parse_struct()
        if self.at("func"):
            return self.parse_func()
        # <Tip> <ad> = <isaret>(...);   ya da  <Tip> <ad> = <ifade>;
        if self.is_type_start() and (self.next().kind == "id"
                                     or self.next().text == "["):
            ty = self.parse_type()
            name = self.want_id()
            self.want("=")
            init = self.parse_expr()
            self.want(";")
            if init["k"] == "call" and init["fn"]["k"] == "id" \
               and init["fn"]["name"] in MARKERS:
                return {"k": "marker", "type": ty, "name": name,
                        "marker": init["fn"]["name"], "args": init["args"],
                        "line": line}
            return {"k": "global", "type": ty, "name": name, "init": init,
                    "line": line}
        raise ShaderError("ust duzeyde beklenmeyen %r" % self.cur().text, line)

    def parse_struct(self):
        line = self.cur().line
        self.i += 1  # struct | type
        name = self.want_id()
        self.want("{")
        fields = []
        while not self.at("}"):
            ty = self.parse_type()
            fname = self.want_id()
            self.want(";")
            fields.append({"type": ty, "name": fname})
        self.want("}")
        self.eat(";")
        if not fields:
            raise ShaderError("bos struct %r" % name, line)
        return {"k": "struct", "name": name, "fields": fields, "line": line}

    def parse_func(self):
        line = self.cur().line
        self.want("func")
        name = self.want_id()
        self.want("(")
        params = []
        while not self.at(")"):
            ty = self.parse_type()
            pname = self.want_id()
            params.append({"type": ty, "name": pname})
            if not self.eat(","):
                break
        self.want(")")
        ret = {"name": "void", "array": False, "size": None}
        if self.eat(":"):
            if self.eat("("):
                # Tulpar demet donusu (`func f(): (vec3, vec3)`, TulparLang
                # P0.1). GLSL'de demet yok: uretici bunu `out` parametrelerine
                # cevirir (FAZ8.md 8.2, K046 — dile yeni sozdizimi gerekmedi).
                items = [self.parse_type()]
                while self.eat(","):
                    items.append(self.parse_type())
                self.want(")")
                if len(items) < 2:
                    raise ShaderError("demet en az iki tip ister", line)
                ret = {"name": "void", "array": False, "size": None,
                       "tuple": items}
            else:
                ret = self.parse_type()
        body = self.parse_block()
        return {"k": "func", "name": name, "params": params, "ret": ret,
                "body": body, "line": line}

    # -- deyimler ---------------------------------------------------------
    def parse_block(self):
        self.want("{")
        stmts = []
        while not self.at("}"):
            if self.at_kind("eof"):
                raise ShaderError("kapanmamis blok", self.cur().line)
            stmts.append(self.parse_stmt())
        self.want("}")
        return {"k": "block", "stmts": stmts}

    def looks_like_decl(self):
        # <id> <id> ( = | ; | , )   ya da  <id> [ N ] <id> ( = | ; )  (G3 yerel dizi)
        if self.cur().kind != "id":
            return False
        if self.next().kind == "id":
            return self.next(2).text in ("=", ";", ",")
        return (self.next().text == "[" and self.next(2).kind == "int"
                and self.next(3).text == "]" and self.next(4).kind == "id"
                and self.next(5).text in ("=", ";"))

    def looks_like_multi_assign(self):
        # <id> , <id> [, <id>...] =   (var olan degiskenlere demet atamasi)
        j = 0
        while self.next(j).kind == "id" and self.next(j + 1).text == ",":
            j += 2
        return j > 0 and self.next(j).kind == "id" and self.next(j + 1).text == "="

    def parse_stmt(self):
        line = self.cur().line
        if self.at("{"):
            return self.parse_block()
        if self.at("if"):
            self.i += 1
            self.want("(")
            cond = self.parse_expr()
            self.want(")")
            then = self.parse_stmt()
            other = None
            if self.eat("else"):
                other = self.parse_stmt()
            return {"k": "if", "cond": cond, "then": then, "else": other,
                    "line": line}
        if self.at("while"):
            self.i += 1
            self.want("(")
            cond = self.parse_expr()
            self.want(")")
            return {"k": "while", "cond": cond, "body": self.parse_stmt(),
                    "line": line}
        if self.at("for"):
            self.i += 1
            self.want("(")
            init = None if self.at(";") else self.parse_simple_stmt()
            self.want(";")
            cond = None if self.at(";") else self.parse_expr()
            self.want(";")
            step = None if self.at(")") else self.parse_simple_stmt()
            self.want(")")
            return {"k": "for", "init": init, "cond": cond, "step": step,
                    "body": self.parse_stmt(), "line": line}
        if self.at("return"):
            self.i += 1
            val = None if self.at(";") else self.parse_expr()
            vals = None
            if val is not None and self.at(","):
                vals = [val]
                while self.eat(","):
                    vals.append(self.parse_expr())
            self.want(";")
            return {"k": "return", "value": val, "values": vals, "line": line}
        if self.at("discard"):
            self.i += 1
            self.want(";")
            return {"k": "discard", "line": line}
        if self.at("break") or self.at("continue"):
            word = self.cur().text
            self.i += 1
            self.want(";")
            return {"k": word, "line": line}
        st = self.parse_simple_stmt()
        self.want(";")
        return st

    def parse_simple_stmt(self):
        line = self.cur().line
        # `const <Tip> <ad> = <ifade>` — Tulpar'in G5 `const`'u (#323).
        # `const` anahtar kelime listesinde degil (ad olarak da gecebilir);
        # yalniz bildirim seklinin onunde gelince niteleyicidir.
        if self.cur().kind == "id" and self.cur().text == "const" \
                and self.next().kind == "id" \
                and (self.next(2).kind == "id" or self.next(2).text == "["):
            self.i += 1
            ty = self.parse_type()
            name = self.want_id()
            self.want("=")
            return {"k": "decl", "type": ty, "name": name, "const": True,
                    "init": self.parse_expr(), "line": line}
        if self.looks_like_multi_assign():
            names = [self.want_id()]
            while self.eat(","):
                names.append(self.want_id())
            self.want("=")
            return {"k": "tassign", "names": names, "init": self.parse_expr(),
                    "line": line}
        if self.looks_like_decl() and self.next(2).text == ",":
            # `vec3 a, b = f();` / `vec3 a, float b = f();` — demet acma.
            ty = self.parse_type()
            vars_ = [(ty, self.want_id())]
            while self.eat(","):
                if self.cur().kind == "id" and self.next().kind == "id":
                    ty = self.parse_type()
                vars_.append((ty, self.want_id()))
            self.want("=")
            return {"k": "tdecl", "vars": vars_, "init": self.parse_expr(),
                    "line": line}
        if self.looks_like_decl():
            ty = self.parse_type()
            name = self.want_id()
            init = None
            if self.eat("="):
                init = self.parse_expr()
            return {"k": "decl", "type": ty, "name": name, "init": init,
                    "line": line}
        target = self.parse_expr()
        for op in ("=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
                   "<<=", ">>="):
            if self.at(op):
                self.i += 1
                return {"k": "assign", "op": op, "target": target,
                        "value": self.parse_expr(), "line": line}
        return {"k": "expr", "expr": target, "line": line}

    # -- ifadeler ---------------------------------------------------------
    def parse_expr(self):
        return self.parse_ternary()

    def parse_ternary(self):
        cond = self.parse_binary(0)
        if self.eat("?"):
            a = self.parse_expr()
            self.want(":")
            b = self.parse_ternary()
            return {"k": "ternary", "cond": cond, "a": a, "b": b}
        return cond

    # GLSL/C oncelik merdiveni (dusukten yukseğe).
    LEVELS = [["||"], ["&&"], ["|"], ["^"], ["&"], ["==", "!="],
              ["<", ">", "<=", ">="], ["<<", ">>"], ["+", "-"],
              ["*", "/", "%"]]

    def parse_binary(self, lvl):
        if lvl >= len(self.LEVELS):
            return self.parse_unary()
        left = self.parse_binary(lvl + 1)
        while self.cur().kind == "op" and self.cur().text in self.LEVELS[lvl]:
            op = self.cur().text
            self.i += 1
            right = self.parse_binary(lvl + 1)
            left = {"k": "bin", "op": op, "l": left, "r": right}
        return left

    def parse_unary(self):
        if self.cur().kind == "op" and self.cur().text in ("-", "!", "+", "~"):
            op = self.cur().text
            self.i += 1
            return {"k": "un", "op": op, "e": self.parse_unary()}
        return self.parse_postfix()

    def parse_postfix(self):
        e = self.parse_primary()
        while True:
            if self.at("("):
                self.i += 1
                args = []
                while not self.at(")"):
                    args.append(self.parse_expr())
                    if not self.eat(","):
                        break
                self.want(")")
                e = {"k": "call", "fn": e, "args": args}
            elif self.at("["):
                self.i += 1
                idx = self.parse_expr()
                self.want("]")
                e = {"k": "index", "base": e, "index": idx}
            elif self.at("."):
                self.i += 1
                e = {"k": "member", "base": e, "name": self.want_id()}
            elif self.at("++") or self.at("--"):
                op = self.cur().text
                self.i += 1
                e = {"k": "post", "op": op, "e": e}
            else:
                return e

    def parse_primary(self):
        c = self.cur()
        if c.kind in ("int", "float"):
            self.i += 1
            return {"k": "num", "kind": c.kind, "text": c.text}
        if c.kind == "str":
            self.i += 1
            return {"k": "str", "text": c.text}
        if c.kind == "kw" and c.text in ("true", "false"):
            self.i += 1
            return {"k": "bool", "text": c.text}
        if c.kind == "id":
            self.i += 1
            return {"k": "id", "name": c.text}
        if self.at("("):
            self.i += 1
            e = self.parse_expr()
            self.want(")")
            return {"k": "paren", "e": e}
        if self.at("["):
            # Tulpar dizi literali `[a, b, c]` (T9). Oge tipi bildirimden
            # gelir: yalniz `T[N] ad = [...]` baslaticisi olarak gecerli.
            self.i += 1
            items = []
            while not self.at("]"):
                items.append(self.parse_expr())
                if not self.eat(","):
                    break
            self.want("]")
            return {"k": "array", "items": items, "line": c.line}
        raise ShaderError("ifadede beklenmeyen %r" % c.text, c.line)


# ---------------------------------------------------------------------------
# Anlamsal denetim + GLSL uretimi
# ---------------------------------------------------------------------------

class Emitter:
    def __init__(self, decls, path):
        self.decls = decls
        self.path = path
        self.structs = {}          # ad -> alan listesi
        self.block_structs = set()  # blok olarak kullanilan struct'lar
        self.funcs = {}
        self.names = set(GL_BUILTIN_VARS)
        self.stage = None
        self.out = []

    # -- yardimcilar ------------------------------------------------------
    def err(self, msg, line=0):
        raise ShaderError(msg, line)

    def typename(self, ty, line):
        n = ty["name"]
        if n in BUILTIN_TYPES or n in self.structs:
            return n
        self.err("bilinmeyen tip %r" % n, line)

    def const_int(self, node, line):
        if node["k"] == "num" and node["kind"] == "int":
            return int(node["text"].rstrip("uU"))
        self.err("sabit tamsayi bekleniyordu", line)

    # -- gecis 1: toplama -------------------------------------------------
    def collect(self):
        for d in self.decls:
            if d["k"] == "struct":
                if d["name"] in self.structs:
                    self.err("struct %r iki kez tanimli" % d["name"], d["line"])
                self.structs[d["name"]] = d
            elif d["k"] == "func":
                if d["name"] in self.funcs:
                    self.err("func %r iki kez tanimli" % d["name"], d["line"])
                self.funcs[d["name"]] = d
        for d in self.decls:
            if d["k"] == "marker" and d["marker"] in ("tekduze", "itme",
                                                      "depo", "depo_yaz"):
                if d["type"]["name"] not in self.structs:
                    self.err("%s icin struct gerekli: %r bilinmiyor"
                             % (d["marker"], d["type"]["name"]), d["line"])
                self.block_structs.add(d["type"]["name"])
            if d["k"] == "global" and d["name"] == "asama":
                if d["init"]["k"] != "str":
                    self.err("asama bir dize olmali", d["line"])
                self.stage = d["init"]["text"]
        if self.stage is None:
            self.err('asama bildirilmemis: str asama = "vert"|"frag";')
        if self.stage not in ("vert", "frag", "comp"):
            self.err("desteklenmeyen asama %r (vert|frag|comp)" % self.stage)
        groups = [d for d in self.decls
                  if d["k"] == "marker" and d["marker"] == "is_grubu"]
        if self.stage == "comp" and len(groups) != 1:
            self.err("comp asamasi tam bir is_grubu(x[, y, z]) ister")
        if self.stage != "comp":
            for d in self.decls:
                if d["k"] == "marker" and d["marker"] in ("is_grubu", "paylasimli"):
                    self.err("%s() yalniz comp asamasinda" % d["marker"],
                             d["line"])
        if "main" not in self.funcs:
            self.err("main() yok")

    # -- gecis 2: uretim --------------------------------------------------
    def emit(self):
        self.collect()
        w = self.out.append
        w("#version 450")
        w("// URETILMIS GLSL — %s dosyasindan tpr_shader.py ile. Elle duzenleme."
          % os.path.basename(self.path))
        for d in self.decls:
            if d["k"] == "struct":
                if d["name"] not in self.block_structs:
                    self.emit_struct(d)
            elif d["k"] == "marker":
                self.emit_marker(d)
            elif d["k"] == "func":
                self.emit_func(d)
            elif d["k"] == "global":
                if d["name"] != "asama":
                    self.err("ust duzey siradan degisken desteklenmiyor: %r"
                             % d["name"], d["line"])
        return "\n".join(self.out) + "\n"

    @staticmethod
    def suffix(ty):
        if not ty["array"]:
            return ""
        return "[]" if ty["size"] is None else "[%d]" % ty["size"]

    def field_line(self, f, line):
        t = self.typename(f["type"], line)
        return "  %s %s%s;" % (t, f["name"], self.suffix(f["type"]))

    def emit_struct(self, d):
        self.out.append("struct %s {" % d["name"])
        for f in d["fields"]:
            self.out.append(self.field_line(f, d["line"]))
        self.out.append("};")

    def block_body(self, sname, line):
        rows = [self.field_line(f, line) for f in self.structs[sname]["fields"]]
        return "{\n" + "\n".join(rows) + "\n}"

    def emit_marker(self, d):
        m, name, line = d["marker"], d["name"], d["line"]
        ty = d["type"]
        if name in self.names:
            self.err("ad %r iki kez bildirilmis" % name, line)
        self.names.add(name)
        a = d["args"]

        def need(n):
            if len(a) != n:
                self.err("%s %d arguman ister, %d verildi" % (m, n, len(a)), line)

        if m in ("girdi", "girdi_duz", "cikti", "cikti_duz"):
            need(1)
            loc = self.const_int(a[0], line)
            qual = "in" if m.startswith("girdi") else "out"
            flat = "flat " if m.endswith("_duz") else ""
            if ty["name"] in OPAQUE:
                self.err("opak tip varyan olamaz: %r" % ty["name"], line)
            self.out.append("layout(location = %d) %s%s %s %s;"
                            % (loc, flat, qual, self.typename(ty, line), name))
        elif m == "ornek":
            need(2)
            if ty["name"] not in OPAQUE:
                self.err("ornek() opak tip ister (sampler2D...), %r verildi"
                         % ty["name"], line)
            self.out.append("layout(set = %d, binding = %d) uniform %s %s;"
                            % (self.const_int(a[0], line),
                               self.const_int(a[1], line), ty["name"], name))
        elif m == "tekduze":
            need(2)
            self.out.append("layout(set = %d, binding = %d) uniform %s %s %s;"
                            % (self.const_int(a[0], line),
                               self.const_int(a[1], line), ty["name"],
                               self.block_body(ty["name"], line), name))
        elif m == "itme":
            need(0)
            self.out.append("layout(push_constant) uniform %s %s %s;"
                            % (ty["name"], self.block_body(ty["name"], line),
                               name))
        elif m in ("depo", "depo_yaz", "depo_oy"):
            need(2)
            # depo = readonly, depo_yaz = writeonly, depo_oy = oku-yaz (niteleyicisiz)
            access = {"depo": "readonly ", "depo_yaz": "writeonly ",
                      "depo_oy": ""}[m]
            self.out.append("layout(std430, set = %d, binding = %d) %sbuffer "
                            "%s %s %s;"
                            % (self.const_int(a[0], line),
                               self.const_int(a[1], line), access,
                               ty["name"], self.block_body(ty["name"], line),
                               name))
        elif m == "is_grubu":
            # `uvec3 grup = is_grubu(64);` -> `layout(local_size_x = 64) in;`
            # Degisken adi GLSL'e gecmez (asama gibi bir bildirim tutucusu);
            # boyuta shader icinden gl_WorkGroupSize ile erisilir.
            if not 1 <= len(a) <= 3:
                self.err("is_grubu 1-3 arguman ister (x, y, z)", line)
            axes = ["local_size_%s = %d" % (ax, self.const_int(v, line))
                    for ax, v in zip("xyz", a)]
            self.out.append("layout(%s) in;" % ", ".join(axes))
        elif m == "paylasimli":
            # `uint[64] s = paylasimli();` -> `shared uint s[64];`
            need(0)
            self.out.append("shared %s %s%s;" % (self.typename(ty, line), name,
                                                 self.suffix(ty)))
        elif m == "ozel":
            need(2)
            self.out.append("layout(constant_id = %d) const %s %s = %s;"
                            % (self.const_int(a[0], line),
                               self.typename(ty, line), name, self.expr(a[1])))
        else:
            self.err("bilinmeyen isaret %r" % m, line)

    @staticmethod
    def tuple_out_name(i):
        return "_r%d" % i

    def emit_func(self, d):
        plist = ["%s %s" % (self.typename(p["type"], d["line"]), p["name"])
                 for p in d["params"]]
        tup = d["ret"].get("tuple")
        self.named_results = self.find_named_results(d) if tup else None
        if tup:
            for i, t in enumerate(tup):
                if t["array"]:
                    self.err("demet ogesi dizi olamaz", d["line"])
                name = (self.named_results[i] if self.named_results
                        else self.tuple_out_name(i))
                plist.append("out %s %s" % (self.typename(t, d["line"]), name))
        if d["name"] == "main":
            if d["params"] or d["ret"]["name"] != "void" or tup:
                self.err("main() parametresiz ve donussuz olmali", d["line"])
        self.cur_tuple = tup
        self.out.append("%s %s(%s) {"
                        % (self.typename(d["ret"], d["line"]), d["name"],
                           ", ".join(plist)))
        stmts = d["body"]["stmts"]
        if self.named_results and stmts and stmts[-1]["k"] == "return":
            # void govdenin sonundaki `return;` GLSL'de fazladan bir blok
            # kimligi uretir (olculdu: 1 kimlik kayar); dusurulur.
            stmts = stmts[:-1]
        for s in stmts:
            self.stmt(s, 1)
        self.out.append("}")
        self.cur_tuple = None
        self.named_results = None

    def find_named_results(self, d):
        """Her `return` AYNI yerel adlari ayni sirayla donduruyorsa o adlar.

        Go'nun adli sonuclari gibi: govdenin EN UST duzeyinde bildirilen ve
        her cikista oldugu gibi donen yereller dogrudan `out` parametresi
        olur (bildirim atamaya, `return a, b;` duz `return;`a doner). Kaynak
        GLSL'in `out` parametresine yazdigi kalip boylece AYNI GLSL'e iner;
        aksi halde yerel + kopya kalir ve glslc -O kimlik numaralari kayar
        (anlam ayni, bayt degil — 2026-11-05 mesh.frag'de olculdu).
        Kosul tutmazsa None: genel yol (_rN parametreleri + kopya).
        """
        rets = []
        inner = set()  # ic bloklardaki bildirimler (golgeleme -> genel yol)

        def walk(st, top=False):
            if st is None:
                return
            if st["k"] == "return":
                rets.append(st)
            if st["k"] in ("decl", "tdecl") and not top:
                inner.update([st["name"]] if st["k"] == "decl"
                             else [n for _, n in st["vars"]])
            if st["k"] == "for" and st.get("init") and st["init"]["k"] == "decl":
                inner.add(st["init"]["name"])
            for key in ("stmts",):
                for x in st.get(key, []):
                    walk(x)
            for key in ("then", "else", "body"):
                if isinstance(st.get(key), dict):
                    walk(st[key])
        for st in d["body"]["stmts"]:
            walk(st, top=True)
        if not rets:
            return None
        names = None
        for r in rets:
            vals = r["values"] or []
            if not vals or any(v["k"] != "id" for v in vals):
                return None
            ns = [v["name"] for v in vals]
            if names is None:
                names = ns
            elif ns != names:
                return None
        if len(set(names)) != len(names) or inner & set(names):
            return None
        top = {st["name"]: st for st in d["body"]["stmts"]
               if st["k"] == "decl" and not st.get("const")}
        tup = d["ret"]["tuple"]
        for i, n in enumerate(names):
            if n not in top or top[n]["type"] != tup[i] \
                    or n in (p["name"] for p in d["params"]):
                return None
        return names

    def tuple_call(self, init, n, line):
        """Demet donduren cagri: (fonksiyon adi, arguman metni)."""
        if init["k"] != "call" or init["fn"]["k"] != "id" \
                or init["fn"]["name"] not in self.funcs:
            self.err("demet acma yalniz bu dosyadaki fonksiyon cagrisindan",
                     line)
        f = self.funcs[init["fn"]["name"]]
        tup = f["ret"].get("tuple")
        if not tup:
            self.err("%r demet dondurmuyor" % f["name"], line)
        if len(tup) != n:
            self.err("%r %d deger donduruyor, %d ad verildi"
                     % (f["name"], len(tup), n), line)
        return f["name"], [self.expr(a) for a in init["args"]]

    # -- deyim uretimi ----------------------------------------------------
    def stmt(self, s, depth):
        pad = "  " * depth
        w = self.out.append
        k = s["k"]
        if k == "block":
            w(pad + "{")
            for x in s["stmts"]:
                self.stmt(x, depth + 1)
            w(pad + "}")
        elif k == "decl" and depth == 1 and getattr(self, "named_results", None) \
                and s["name"] in self.named_results:
            # adli sonuc: yerel degil, `out` parametresi
            if s["init"] is not None:
                w("%s%s = %s;" % (pad, s["name"], self.expr(s["init"])))
        elif k == "decl":
            t = self.typename(s["type"], s["line"])
            sfx = self.suffix(s["type"])
            if s["init"] is None:
                w("%s%s %s%s;" % (pad, t, s["name"], sfx))
            else:
                init = s["init"]
                if init["k"] == "array":
                    # `vec2[3] p = [..]` -> `vec2 p[3] = vec2[](..)`
                    if not s["type"]["array"] or s["type"]["size"] is None:
                        self.err("dizi literali sabit boy dizi bildirimi ister "
                                 "(T[N] ad = [...])", s["line"])
                    if len(init["items"]) != s["type"]["size"]:
                        self.err("dizi %d oge ister, %d verildi"
                                 % (s["type"]["size"], len(init["items"])),
                                 s["line"])
                    val = "%s[](%s)" % (t, ", ".join(self.expr(x)
                                                    for x in init["items"]))
                else:
                    val = self.expr(init)
                w("%s%s%s %s%s = %s;" % (pad, "const " if s.get("const") else "",
                                         t, s["name"], sfx, val))
        elif k == "assign":
            w("%s%s %s %s;" % (pad, self.expr(s["target"]), s["op"],
                               self.expr(s["value"])))
        elif k == "expr":
            w("%s%s;" % (pad, self.expr(s["expr"])))
        elif k == "if":
            w("%sif (%s) {" % (pad, self.expr(s["cond"])))
            self.body(s["then"], depth + 1)
            if s["else"] is not None:
                w(pad + "} else {")
                self.body(s["else"], depth + 1)
            w(pad + "}")
        elif k == "while":
            w("%swhile (%s) {" % (pad, self.expr(s["cond"])))
            self.body(s["body"], depth + 1)
            w(pad + "}")
        elif k == "for":
            init = self.inline_simple(s["init"]) if s["init"] else ""
            cond = self.expr(s["cond"]) if s["cond"] else ""
            step = self.inline_simple(s["step"]) if s["step"] else ""
            w("%sfor (%s; %s; %s) {" % (pad, init, cond, step))
            self.body(s["body"], depth + 1)
            w(pad + "}")
        elif k == "return":
            tup = getattr(self, "cur_tuple", None)
            if tup:
                vals = s["values"] or ([s["value"]] if s["value"] else [])
                if len(vals) != len(tup):
                    self.err("demet %d deger ister, %d verildi"
                             % (len(tup), len(vals)), s["line"])
                if self.named_results:
                    w(pad + "return;")
                    return
                for i, v in enumerate(vals):
                    w("%s%s = %s;" % (pad, self.tuple_out_name(i), self.expr(v)))
                w(pad + "return;")
            else:
                if s["values"]:
                    self.err("coklu return yalniz demet donduren fonksiyonda",
                             s["line"])
                w("%sreturn%s;" % (pad, "" if s["value"] is None
                                   else " " + self.expr(s["value"])))
        elif k == "tdecl":
            fn, args = self.tuple_call(s["init"], len(s["vars"]), s["line"])
            for ty, name in s["vars"]:
                w("%s%s %s;" % (pad, self.typename(ty, s["line"]), name))
            w("%s%s(%s);" % (pad, fn, ", ".join(args + [n for _, n in s["vars"]])))
        elif k == "tassign":
            fn, args = self.tuple_call(s["init"], len(s["names"]), s["line"])
            w("%s%s(%s);" % (pad, fn, ", ".join(args + s["names"])))
        elif k == "discard":
            if self.stage != "frag":
                self.err("discard yalniz frag asamasinda", s["line"])
            w(pad + "discard;")
        elif k in ("break", "continue"):
            w(pad + k + ";")
        else:
            self.err("desteklenmeyen deyim %r" % k, s.get("line", 0))

    def body(self, s, depth):
        """if/for/while govdesi: blok ise ic deyimleri, degilse tek deyim."""
        if s["k"] == "block":
            for x in s["stmts"]:
                self.stmt(x, depth)
        else:
            self.stmt(s, depth)

    def inline_simple(self, s):
        if s["k"] == "decl":
            t = self.typename(s["type"], s["line"])
            return "%s %s = %s" % (t, s["name"], self.expr(s["init"])) \
                if s["init"] is not None else "%s %s" % (t, s["name"])
        if s["k"] == "assign":
            return "%s %s %s" % (self.expr(s["target"]), s["op"],
                                 self.expr(s["value"]))
        if s["k"] == "expr":
            return self.expr(s["expr"])
        self.err("for basliginda desteklenmeyen deyim %r" % s["k"],
                 s.get("line", 0))

    # -- ifade uretimi ----------------------------------------------------
    def expr(self, e):
        k = e["k"]
        if k == "num":
            return e["text"]
        if k == "bool":
            return e["text"]
        if k == "str":
            self.err("dize ifadesi shader'da kullanilamaz")
        if k == "array":
            self.err("dizi literali yalniz T[N] bildiriminin baslaticisi olabilir",
                     e.get("line", 0))
        if k == "id":
            return e["name"]
        if k == "paren":
            return "(%s)" % self.expr(e["e"])
        if k == "un":
            return "%s%s" % (e["op"], self.expr(e["e"]))
        if k == "post":
            return "%s%s" % (self.expr(e["e"]), e["op"])
        if k == "bin":
            return "%s %s %s" % (self.expr(e["l"]), e["op"], self.expr(e["r"]))
        if k == "ternary":
            return "%s ? %s : %s" % (self.expr(e["cond"]), self.expr(e["a"]),
                                     self.expr(e["b"]))
        if k == "index":
            return "%s[%s]" % (self.expr(e["base"]), self.expr(e["index"]))
        if k == "member":
            return "%s.%s" % (self.expr(e["base"]), e["name"])
        if k == "call":
            fn = e["fn"]
            if fn["k"] != "id":
                self.err("yalniz adla cagri desteklenir")
            name = fn["name"]
            if name in CAST_ALIAS:
                name = CAST_ALIAS[name]
            elif name in MARKERS:
                self.err("isaret cagrisi %r yalniz ust duzeyde kullanilir" % name)
            elif name not in BUILTIN_FUNCS and name not in self.funcs \
                    and name not in self.structs:
                self.err("bilinmeyen fonksiyon %r" % name)
            return "%s(%s)" % (name, ", ".join(self.expr(a) for a in e["args"]))
        self.err("desteklenmeyen ifade %r" % k)


def translate(path):
    with open(path) as f:
        src = f.read()
    decls = Parser(lex(src)).parse_program()
    return Emitter(decls, path).emit()


# ---------------------------------------------------------------------------
# glslc / denetim
# ---------------------------------------------------------------------------

STAGE_EXT = {"vert": ".vert", "frag": ".frag", "comp": ".comp"}


def stage_of(glsl):
    # Uretilen GLSL'in asamasi: dosya adi yerine icerikten degil, cagiran
    # zaten biliyor; burada .tprs'in `asama` satirindan tasinir.
    return None


def compile_spv(glsl, stage, out_path, tmpdir):
    glslc = shutil.which("glslc")
    if not glslc:
        return None, "glslc yok"
    src = os.path.join(tmpdir, "gecici" + STAGE_EXT[stage])
    with open(src, "w") as f:
        f.write(glsl)
    r = subprocess.run([glslc, "-O", "--target-env=vulkan1.1", "-o", "-", src],
                       capture_output=True)
    if r.returncode != 0:
        return None, r.stderr.decode(errors="replace")
    data = r.stdout
    if out_path:
        with open(out_path, "wb") as f:
            f.write(data)
    return data, None


def source_path(base):
    return os.path.join(REPO_SHADERS, base)


def source_digest(base):
    """Ported edilen GLSL kaynaginin ozeti (ilk 12 hex)."""
    p = source_path(base)
    if not os.path.exists(p):
        return None
    import hashlib
    return hashlib.sha256(open(p, "rb").read()).hexdigest()[:12]


PIN_RE = re.compile(r"//\s*kaynak-ozet:\s*([0-9a-f]{12})")


def pinned_digest(src):
    m = PIN_RE.search(src)
    return m.group(1) if m else None


def cmd_pin(directory):
    """Her .tprs'e, cevrildigi GLSL kaynaginin ozetini yaz/guncelle.

    Referans (engine/rhi/shaders/*) BASKA biri tarafindan degistirildiginde
    bayt karsilastirmasi sessizce kirmizi ya da sessizce yesil olmasin diye:
    ozet tutmuyorsa denetim GORUNUR bicimde "kaynak degismis" der.
    """
    n = 0
    for f in sorted(os.listdir(directory)):
        if not f.endswith(".tprs"):
            continue
        base = f[:-5]
        dig = source_digest(base)
        if dig is None:
            print("  %s: depoda %s yok, atlandi" % (f, base))
            continue
        path = os.path.join(directory, f)
        src = open(path).read()
        line = "// kaynak-ozet: %s  (engine/rhi/shaders/%s)\n" % (dig, base)
        if PIN_RE.search(src):
            src = re.sub(r"//\s*kaynak-ozet:[^\n]*\n", line, src, count=1)
        else:
            src = line + src
        open(path, "w").write(src)
        print("  %s -> %s" % (f, dig))
        n += 1
    print("%d dosya ozetlendi" % n)
    return 0


def header_bytes(name):
    """engine/rhi/shaders/<name>_spv.h icindeki SPIR-V'yi bayta cevir."""
    p = os.path.join(REPO_SHADERS, name.replace(".", "_") + "_spv.h")
    if not os.path.exists(p):
        return None
    words = re.findall(r"0x([0-9a-fA-F]{8})", open(p).read())
    return b"".join(int(w, 16).to_bytes(4, "little") for w in words)


def tprs_stage(path):
    src = open(path).read()
    m = re.search(r'asama\s*=\s*"(\w+)"', src)
    return m.group(1) if m else None


def cmd_check(directory):
    import tempfile
    files = sorted(f for f in os.listdir(directory) if f.endswith(".tprs"))
    if not files:
        print("ATLANDI: %s altinda .tprs yok" % directory)
        return 0
    have_glslc = shutil.which("glslc") is not None
    ok = same = diff = failed = 0
    tmp = tempfile.mkdtemp(prefix="tprs")
    for f in files:
        path = os.path.join(directory, f)
        base = f[:-5]  # .tprs at
        try:
            glsl = translate(path)
        except ShaderError as e:
            print("  KIRMIZI %-24s cevrilemedi: %s" % (base, e))
            failed += 1
            continue
        ok += 1
        if not have_glslc:
            print("  ATLANDI %-24s glslc yok (cevrildi)" % base)
            continue
        stage = tprs_stage(path)
        data, err = compile_spv(glsl, stage, None, tmp)
        if data is None:
            print("  KIRMIZI %-24s glslc: %s" % (base, err.strip().splitlines()[0]
                                                 if err.strip() else "?"))
            failed += 1
            continue
        # Karsi taraf: referans GLSL'in AYNI yerel derleyiciyle derlemesi
        # (faz8_shader_audit.py ile ayni tanim, #68). Depodaki *_spv.h'ye
        # karsi olcmek, basliklari baska bir derleyici urettiyse her dosyada
        # uretec kimliginden 12 bayt "FARKLI" der — 2026-11-05'e kadar bu mod
        # tam olarak bunu yapiyordu (14/14 "SARI", hepsi +12 bayt).
        ref = None
        if os.path.exists(source_path(base)):
            with open(source_path(base)) as fh:
                ref, rerr = compile_spv(fh.read(), stage, None, tmp)
            if ref is None:
                print("  KIRMIZI %-24s referans GLSL derlenmedi: %s"
                      % (base, (rerr or "?").strip().splitlines()[0]))
                failed += 1
                continue
        if ref is None:
            print("  YESIL   %-24s %5d bayt SPIR-V (depoda karsiligi yok)"
                  % (base, len(data)))
            continue
        pin, now = pinned_digest(open(path).read()), source_digest(base)
        if pin and now and pin != now:
            print("  ATLANDI %-24s kaynak shader degismis (%s -> %s); bayt "
                  "karsilastirmasi yapilmadi — --pin ile guncelle"
                  % (base, pin, now))
            continue
        if ref == data:
            print("  YESIL   %-24s %5d bayt, referans GLSL ile BAYT AYNI"
                  % (base, len(data)))
            same += 1
        else:
            print("  KIRMIZI %-24s %5d bayt, referans %d bayt — FARKLI"
                  % (base, len(data), len(ref)))
            diff += 1
    shutil.rmtree(tmp, ignore_errors=True)
    print("\n%d cevrildi, %d bayt-ayni, %d farkli, %d basarisiz"
          % (ok, same, diff, failed))
    return 1 if failed or diff else 0


def known_gaps(src):
    """Bu .tprs hangi BILINEN ve HALA ACIK Tulpar bosluğunu kullaniyor?

    Bu liste KAPANDIKCA KISALIR. 2026-09-16'da G3 (sabit boy dizi `T[N]`) ve
    G4 (bit islemleri + tabanli sabitler) dile eklendi, yani o iki madde artik
    bir bosluk DEGIL ve buradan cikarildi. Kapanmis bir boslugu listede
    birakmak, denetimi "dusmesi beklenen" dosyalarla doldurup gercek bir
    gerilemeyi gorunmez kilardi (bkz. Tuzaklar 8am: bayat beklenti = kor kapi).

    Kalan tek acik madde `uint` ailesi: `0u`/`1u` isaretsiz sonekleri. Bu bir
    GRAMER boslugu degil TIP SISTEMI boslugu (FAZ8.md T3) — `0u`'yu sessizce
    `int` saymak yanlis bir zihin modeli kurar (`~0u == -1`).
    """
    g = []
    body = re.sub(r"//[^\n]*", "", src)
    if re.search(r"\b\d+[uU]\b", body):
        g.append("T3 isaretsiz sonek (0u/1u)")
    return g


def find_tulpar():
    """Denetimde kullanilacak `tulpar` ikilisi, ve NEREDEN bulundugu.

    Eskiden yalniz `os.path.dirname(ENGINE)/tulpar` aranirdi — depo
    ayrilmadan onceki yerlesim (motor derleyici deposunun ALT dizinindeydi).
    Ayrilmadan sonra bu yol `/mnt/veri/yazilim/tulpar` oldu, yani hicbir
    zaman var olmayan bir dizin: denetim 2026-09-20'den beri her kosumda
    "ATLANDI" deyip HICBIR SEY olcmuyordu (olculdu 2026-09-27). Sira:
      1. $TULPAR_BIN (acik secim)
      2. kardes derleyici deposu: ../Tulpar/tulpar, ../TulparLang/tulpar,
         ../tulpar/tulpar
      3. PATH'teki `tulpar`
    Bu denetim yalniz AYRISTIRICIYI kullanir; motor eklentisi gerekmez.
    (2026-10-02'ye kadar 2. sirada motor_derleyici.sh'in yapi/tulpar-motor
    derleyicisi vardi; o yol K303 ile kalkti.)
    """
    cands = []
    env = os.environ.get("TULPAR_BIN")
    if env:
        cands.append(("TULPAR_BIN", env))
    parent = os.path.dirname(ENGINE)
    for d in ("Tulpar", "TulparLang", "tulpar"):
        cands.append(("kardes depo", os.path.join(parent, d, "tulpar")))
    w = shutil.which("tulpar")
    if w:
        cands.append(("PATH", w))
    for why, c in cands:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c, why
    return None, "aranan: " + ", ".join(c for _, c in cands)


def cmd_tulpar_parse(directory):
    """.tprs dosyalari BUGUNKU Tulpar ayristiricisindan geciyor mu?

    Alt kumenin iddiasi: `.tprs` ayri bir dil degil, Tulpar sozdizimidir.
    Bu denetim onu OLCER. Bilinen gramer bosluğunu (G3/G4) kullanan dosyanin
    dusmesi BEKLENIR ve o bosluk kapandiginda kendiliginden yesile doner;
    BEKLENMEYEN bir dusus regresyondur ve denetimi kirmizi yapar.
    """
    binpath, why = find_tulpar()
    if not binpath:
        # Gorunur atlama; sessiz yesil degil. `--zorunlu` ile KIRMIZI.
        print("ATLANDI: tulpar ikilisi bulunamadi (%s). TULPAR_BIN=<yol> ver "
              "ya da TulparLang'i kur (PATH'te tulpar)." % why)
        return 1 if os.environ.get("TPRS_ZORUNLU") == "1" else 0
    ver = subprocess.run([binpath, "version"], capture_output=True, text=True)
    print("tulpar: %s (%s) — %s" % (binpath, why,
                                     (ver.stdout or ver.stderr).strip().splitlines()[0]
                                     if (ver.stdout or ver.stderr).strip() else "?"))
    import tempfile
    files = sorted(f for f in os.listdir(directory) if f.endswith(".tprs"))
    tmp = tempfile.mkdtemp(prefix="tprsparse")
    env = dict(os.environ, LC_ALL="C")
    # IKI KOVA AYRI. Eskiden "beklenmeyen" hem IYILESMEYI (bosluk kapanmis,
    # dosya artik geciyor) hem GERILEMEYI (bilinen boslugu olmayan dosya
    # dusuyor) sayiyordu ve ikisi de cikisi 1 yapiyordu — yani iyi haber ile
    # kotu haber ozet satirinda AYIRT EDILEMIYORDU. Gerileme kirmizi yapar;
    # iyilesme yapmaz ama yuksek sesle "tabloyu guncelle" der.
    clean = expected = iyilesme = gerileme = 0
    for f in files:
        src = open(os.path.join(directory, f)).read()
        dst = os.path.join(tmp, f[:-5] + ".tpr")
        with open(dst, "w") as out:
            out.write(src)
        r = subprocess.run([binpath, "typecheck", dst], capture_output=True,
                           env=env)
        text = (r.stdout + r.stderr).decode(errors="replace")
        n = len(re.findall(r"parse error|Lexer Error", text))
        gaps = known_gaps(src)
        if n == 0:
            if gaps:
                print("  IYILESME %-23s parse hatasi yok — %s artik bosluk DEGIL;"
                      " known_gaps() guncellenmeli"
                      % (f[:-5], ", ".join(gaps)))
                iyilesme += 1
            else:
                print("  YESIL   %-24s Tulpar grameri kabul ediyor "
                      "(parse hatasi 0)" % f[:-5])
                clean += 1
        elif gaps:
            print("  BEKLENEN %-23s %d parse hatasi — %s"
                  % (f[:-5], n, ", ".join(gaps)))
            expected += 1
        else:
            print("  KIRMIZI %-24s %d parse hatasi, bilinen bosluk YOK — GERILEME"
                  % (f[:-5], n))
            gerileme += 1
    shutil.rmtree(tmp, ignore_errors=True)
    print("\n%d dosya bugunku Tulpar grameriyle TEMIZ, %d dosya BILINEN acik "
          "bosluk yuzunden dusuyor, %d gerileme, %d iyilesme (toplam %d)"
          % (clean, expected, gerileme, iyilesme, len(files)))
    if iyilesme:
        print("UYARI: %d dosya beklenenden IYI — known_gaps() bayat, guncelle "
              "(kapanmis bir bosluk listede kalirsa gercek gerileme gorunmez olur)."
              % iyilesme)
    return 1 if gerileme else 0


def main():
    ap = argparse.ArgumentParser(description="Tulpar shader alt kumesi -> GLSL")
    ap.add_argument("input", nargs="?", help=".tprs dosyasi")
    ap.add_argument("-o", "--out", help="GLSL cikti dosyasi")
    ap.add_argument("--spv", help="SPIR-V cikti dosyasi (glslc gerekir)")
    ap.add_argument("--check", nargs="?", const=ASSETS, metavar="DIZIN",
                    help="dizindeki tum .tprs'leri cevir, derle, karsilastir")
    ap.add_argument("--tulpar-parse", nargs="?", const=ASSETS, metavar="DIZIN",
                    help=".tprs'leri ./tulpar ayristiricisiyla dogrula")
    ap.add_argument("--pin", nargs="?", const=ASSETS, metavar="DIZIN",
                    help="her .tprs'e cevrildigi GLSL kaynaginin ozetini yaz")
    args = ap.parse_args()

    if args.check is not None:
        return cmd_check(args.check)
    if args.tulpar_parse is not None:
        return cmd_tulpar_parse(args.tulpar_parse)
    if args.pin is not None:
        return cmd_pin(args.pin)
    if not args.input:
        ap.print_help()
        return 2
    try:
        glsl = translate(args.input)
    except ShaderError as e:
        print("%s: %s" % (args.input, e), file=sys.stderr)
        return 1
    if args.out:
        with open(args.out, "w") as f:
            f.write(glsl)
    elif not args.spv:
        sys.stdout.write(glsl)
    if args.spv:
        import tempfile
        tmp = tempfile.mkdtemp(prefix="tprs")
        stage = tprs_stage(args.input)
        data, err = compile_spv(glsl, stage, args.spv, tmp)
        shutil.rmtree(tmp, ignore_errors=True)
        if data is None:
            print("glslc: %s" % err, file=sys.stderr)
            return 1
        print("%s -> %s (%d bayt)" % (args.input, args.spv, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
