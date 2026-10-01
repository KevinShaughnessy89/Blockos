#!/usr/bin/env python3
"""BlockOS Kconfig/menuconfig implementation.

This is intentionally shaped like the Linux kernel's menuconfig workflow:
Kconfig -> .config -> include/generated/autoconf.h + config.mk.
The UI is an ncurses tree with Y/N/Space, Enter, Esc, / search and ? help.
"""
from __future__ import annotations

import curses
import os
import shlex
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[1]
KCONFIG = ROOT / "Kconfig"
CONFIG = ROOT / ".config"
GENERATED = ROOT / "include" / "generated"
AUTOCONF = GENERATED / "autoconf.h"
CONFIG_MK = GENERATED / "config.mk"
SETTINGS = ROOT / "blockos.settings"


# ---------------------------------------------------------------------------
# Kconfig model
# ---------------------------------------------------------------------------

@dataclass
class BaseNode:
    prompt: str
    parent: Optional["BaseNode"] = None
    depends: List[str] = field(default_factory=list)
    children: List["BaseNode"] = field(default_factory=list)


@dataclass
class ConfigNode(BaseNode):
    name: str = ""
    kind: str = "bool"
    default: Optional[str] = None
    help_text: str = ""
    value: str = "n"
    choice: Optional["ChoiceNode"] = None


@dataclass
class ChoiceNode(BaseNode):
    default: Optional[str] = None
    help_text: str = ""


@dataclass
class RootNode(BaseNode):
    pass


# ---------------------------------------------------------------------------
# Expression evaluator (Kconfig-style boolean subset)
# ---------------------------------------------------------------------------

class ExprParser:
    def __init__(self, text: str, values: Dict[str, str]):
        self.tokens = self._tokenize(text)
        self.pos = 0
        self.values = values

    @staticmethod
    def _tokenize(text: str) -> List[str]:
        out: List[str] = []
        i = 0
        while i < len(text):
            c = text[i]
            if c.isspace():
                i += 1
                continue
            if text.startswith("&&", i) or text.startswith("||", i):
                out.append(text[i:i + 2])
                i += 2
                continue
            if c in "!()":
                out.append(c)
                i += 1
                continue
            if c == '"':
                j = i + 1
                while j < len(text) and text[j] != '"':
                    j += 1
                out.append(text[i + 1:j])
                i = min(j + 1, len(text))
                continue
            j = i
            while j < len(text) and (text[j].isalnum() or text[j] in "_-"):
                j += 1
            if j == i:
                i += 1
                continue
            out.append(text[i:j])
            i = j
        return out

    def _value(self, token: str) -> bool:
        if token in ("y", "Y", "true", "TRUE", "1"):
            return True
        if token in ("n", "N", "false", "FALSE", "0"):
            return False
        return self.values.get(token, "n") in ("y", "Y", "1", "true", "TRUE")

    def parse(self) -> bool:
        if not self.tokens:
            return True
        return self._or()

    def _peek(self) -> Optional[str]:
        return self.tokens[self.pos] if self.pos < len(self.tokens) else None

    def _take(self) -> Optional[str]:
        tok = self._peek()
        if tok is not None:
            self.pos += 1
        return tok

    def _or(self) -> bool:
        left = self._and()
        while self._peek() == "||":
            self._take()
            left = left or self._and()
        return left

    def _and(self) -> bool:
        left = self._unary()
        while self._peek() == "&&":
            self._take()
            left = left and self._unary()
        return left

    def _unary(self) -> bool:
        if self._peek() == "!":
            self._take()
            return not self._unary()
        if self._peek() == "(":
            self._take()
            value = self._or()
            if self._peek() == ")":
                self._take()
            return value
        tok = self._take()
        return True if tok is None else self._value(tok)


def expr_true(exprs: Iterable[str], values: Dict[str, str]) -> bool:
    for expr in exprs:
        if expr and not ExprParser(expr, values).parse():
            return False
    return True


# ---------------------------------------------------------------------------
# Kconfig parser
# ---------------------------------------------------------------------------

class KconfigParser:
    def __init__(self, root_file: Path):
        self.root_file = root_file
        self.root = RootNode("BlockOS")
        self.nodes: List[ConfigNode] = []
        self.configs: Dict[str, ConfigNode] = {}
        self.mainmenu = "BlockOS Kernel Configuration"
        self._parse_file(root_file, self.root, [], set())
        self._attach_choice_symbols()
        self.values: Dict[str, str] = {n.name: "n" for n in self.nodes}
        self._apply_defaults()

    @staticmethod
    def _strip_comment(line: str) -> str:
        if "#" not in line:
            return line.rstrip()
        quoted = False
        out = []
        for c in line:
            if c == '"':
                quoted = not quoted
            if c == '#' and not quoted:
                break
            out.append(c)
        return "".join(out).rstrip()

    @staticmethod
    def _quoted_or_rest(text: str) -> str:
        text = text.strip()
        if not text:
            return ""
        if text.startswith('"'):
            end = text.rfind('"')
            if end > 0:
                return text[1:end]
        return text

    @staticmethod
    def _join_dep(a: List[str], b: Optional[str]) -> List[str]:
        result = list(a)
        if b:
            result.append(b.strip())
        return result

    def _parse_file(self, path: Path, container: BaseNode,
                    inherited_if: List[str], active_files: set[Path]) -> None:
        path = path.resolve()
        if path in active_files:
            raise RuntimeError(f"recursive Kconfig source: {path}")
        active_files.add(path)
        lines = path.read_text(encoding="utf-8").splitlines()
        i = 0
        current: Optional[BaseNode] = None
        stack: List[Tuple[str, BaseNode, List[str], Optional[BaseNode]]] = []
        local_container = container
        local_if = list(inherited_if)

        while i < len(lines):
            raw = lines[i]
            line = self._strip_comment(raw).strip()
            i += 1
            if not line:
                continue
            parts = line.split(None, 1)
            key = parts[0]
            arg = parts[1].strip() if len(parts) > 1 else ""

            if key == "mainmenu":
                self.mainmenu = self._quoted_or_rest(arg)
                continue

            if key == "source":
                source_name = self._quoted_or_rest(arg)
                source_path = (path.parent / source_name).resolve()
                self._parse_file(source_path, local_container, local_if, active_files)
                continue

            if key == "menu":
                node = BaseNode(self._quoted_or_rest(arg), parent=local_container,
                                depends=list(local_if))
                local_container.children.append(node)
                stack.append(("menu", local_container, list(local_if), current))
                local_container = node
                local_if = list(node.depends)
                current = None
                continue

            if key == "endmenu":
                if not stack:
                    raise RuntimeError(f"unexpected endmenu in {path}:{i}")
                _, old_container, old_if, old_current = stack.pop()
                local_container = old_container
                local_if = old_if
                current = old_current
                continue

            if key == "if":
                expr = arg
                stack.append(("if", local_container, list(local_if), current))
                local_if = self._join_dep(local_if, expr)
                current = None
                continue

            if key == "endif":
                if not stack or stack[-1][0] != "if":
                    raise RuntimeError(f"unexpected endif in {path}:{i}")
                _, old_container, old_if, old_current = stack.pop()
                local_container = old_container
                local_if = old_if
                current = old_current
                continue

            if key == "choice":
                node = ChoiceNode("", parent=local_container, depends=list(local_if))
                local_container.children.append(node)
                stack.append(("choice", local_container, list(local_if), current))
                local_container = node
                current = node
                continue

            if key == "endchoice":
                while stack and stack[-1][0] != "choice":
                    stack.pop()
                if not stack:
                    raise RuntimeError(f"unexpected endchoice in {path}:{i}")
                _, old_container, old_if, old_current = stack.pop()
                local_container = old_container
                local_if = old_if
                current = old_current
                continue

            if key == "config":
                name = arg.split()[0]
                node = ConfigNode(name=name, prompt=name, parent=local_container,
                                  depends=list(local_if))
                local_container.children.append(node)
                self.nodes.append(node)
                self.configs[name] = node
                current = node
                continue

            if key == "bool":
                if not isinstance(current, ConfigNode):
                    continue
                current.kind = "bool"
                if arg:
                    current.prompt = self._quoted_or_rest(arg)
                continue

            if key in ("string", "int", "hex", "tristate"):
                if isinstance(current, ConfigNode):
                    current.kind = key
                    if arg:
                        current.prompt = self._quoted_or_rest(arg)
                continue

            if key == "prompt":
                if current is not None:
                    current.prompt = self._quoted_or_rest(arg)
                continue

            if key == "default":
                if current is None:
                    continue
                split = arg.split(" if ", 1)
                value = self._quoted_or_rest(split[0].strip())
                cond = split[1].strip() if len(split) == 2 else None
                if isinstance(current, ChoiceNode):
                    current.default = value
                elif isinstance(current, ConfigNode) and current.default is None:
                    current.default = value
                    if cond:
                        current.depends.append(cond)
                continue

            if key == "depends":
                if not arg.startswith("on ") or current is None:
                    continue
                current.depends.append(arg[3:].strip())
                continue

            if key == "help":
                if current is None:
                    continue
                buf: List[str] = []
                while i < len(lines):
                    nxt = lines[i]
                    stripped = self._strip_comment(nxt)
                    if stripped and not nxt.startswith((" ", "\t")):
                        break
                    i += 1
                    if not stripped.strip():
                        buf.append("")
                    else:
                        buf.append(stripped.strip())
                current.help_text = "\n".join(buf).strip()
                continue

        active_files.remove(path)

    def _attach_choice_symbols(self) -> None:
        def walk(node: BaseNode, choice: Optional[ChoiceNode] = None) -> None:
            current_choice = choice
            if isinstance(node, ChoiceNode):
                current_choice = node
            if isinstance(node, ConfigNode) and current_choice is not None:
                node.choice = current_choice
                for dep in current_choice.depends:
                    if dep not in node.depends:
                        node.depends.append(dep)
            for child in node.children:
                walk(child, current_choice)
        walk(self.root)

    def _apply_defaults(self) -> None:
        # Choices first so their defaults propagate before other visibility checks.
        for node in self.nodes:
            if node.choice:
                continue
            if node.default:
                node.value = node.default
                self.values[node.name] = node.default
        for node in self._choices():
            if node.default:
                selected = node.default
                for child in node.children:
                    if isinstance(child, ConfigNode):
                        child.value = "y" if child.name == selected else "n"
                        self.values[child.name] = child.value

    def _choices(self) -> List[ChoiceNode]:
        out: List[ChoiceNode] = []
        def walk(n: BaseNode) -> None:
            if isinstance(n, ChoiceNode):
                out.append(n)
            for c in n.children:
                walk(c)
        walk(self.root)
        return out

    def load_config(self, path: Path = CONFIG) -> None:
        if not path.exists():
            return
        raw_values: Dict[str, str] = {}
        for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            line = raw.strip()
            if line.startswith("CONFIG_") and "=" in line:
                k, v = line.split("=", 1)
                raw_values[k[7:]] = v.strip()
            elif line.startswith("# CONFIG_") and line.endswith("is not set"):
                k = line[len("# CONFIG_"):].split(" is not set", 1)[0]
                raw_values[k] = "n"
        for name in self.configs:
            if name in raw_values:
                self.values[name] = raw_values[name]
                self.configs[name].value = raw_values[name]
        for choice in self._choices():
            selected = next((c.name for c in choice.children
                             if isinstance(c, ConfigNode) and self.values.get(c.name) == "y"), None)
            if selected:
                for c in choice.children:
                    if isinstance(c, ConfigNode):
                        c.value = "y" if c.name == selected else "n"
                        self.values[c.name] = c.value

    def visible(self, node: BaseNode) -> bool:
        return expr_true(node.depends, self.values)

    def set_bool(self, node: ConfigNode, value: str) -> None:
        if node.choice and value == "y":
            for sibling in node.choice.children:
                if isinstance(sibling, ConfigNode):
                    sibling.value = "y" if sibling is node else "n"
                    self.values[sibling.name] = sibling.value
            return
        node.value = value
        self.values[node.name] = value

    def toggle(self, node: ConfigNode) -> None:
        if node.kind == "bool":
            self.set_bool(node, "n" if node.value == "y" else "y")

    def _normalize(self) -> None:
        # Match Kconfig visibility semantics: an option hidden by a false
        # dependency cannot remain enabled in the saved configuration.
        changed = True
        while changed:
            changed = False
            for node in self.nodes:
                if not self.visible(node) and self.values.get(node.name, "n") != "n":
                    self.values[node.name] = "n"
                    node.value = "n"
                    changed = True
        for choice in self._choices():
            selected = None
            for child in choice.children:
                if isinstance(child, ConfigNode) and self.values.get(child.name) == "y" and self.visible(child):
                    selected = child.name
                    break
            if selected:
                for child in choice.children:
                    if isinstance(child, ConfigNode):
                        value = "y" if child.name == selected else "n"
                        if self.values.get(child.name) != value:
                            self.values[child.name] = value
                            child.value = value

    def save(self) -> None:
        self._normalize()
        CONFIG.write_text(self.render_config(), encoding="utf-8")
        GENERATED.mkdir(parents=True, exist_ok=True)
        AUTOCONF.write_text(self.render_autoconf(), encoding="utf-8")
        CONFIG_MK.write_text(self.render_make(), encoding="utf-8")
        SETTINGS.write_text(self.render_settings(), encoding="utf-8")
        legacy = ROOT / "kernel" / "blockos_config.h"
        legacy.write_text(
            "#pragma once\n// Compatibility wrapper. Generated by BlockOS Kconfig.\n#include <generated/autoconf.h>\n",
            encoding="utf-8",
        )

    def render_config(self) -> str:
        lines = ["#", "# BlockOS configuration", "# Generated by scripts/menuconfig.py", "#"]
        for node in self.nodes:
            if node.value == "n":
                lines.append(f"# CONFIG_{node.name} is not set")
            else:
                lines.append(f"CONFIG_{node.name}={node.value}")
        return "\n".join(lines) + "\n"

    def render_autoconf(self) -> str:
        lines = ["/* Auto-generated by BlockOS Kconfig. Do not edit. */",
                 "#ifndef __BLOCKOS_AUTOCONF_H__",
                 "#define __BLOCKOS_AUTOCONF_H__", ""]
        for node in self.nodes:
            if node.value == "y":
                lines.append(f"#define CONFIG_{node.name} 1")
            elif node.kind in ("string", "int", "hex") and node.value not in ("", "n"):
                lines.append(f"#define CONFIG_{node.name} {node.value}")

        # Compatibility aliases for the older BlockOS configuration names.
        aliases = {
            "CONFIG_PREEMPT": "CONFIG_KERNEL_PREEMPT",
            "CONFIG_SMP": "CONFIG_KERNEL_SMP",
            "CONFIG_VIRTIO_NET": "CONFIG_DRIVER_VIRTIO_NET",
            "CONFIG_VIRTIO_BLOCK": "CONFIG_DRIVER_VIRTIO_BLOCK",
            "CONFIG_VFS": "CONFIG_FS_VFS",
            "CONFIG_EXT4": "CONFIG_FS_EXT4",
            "CONFIG_RAMFS": "CONFIG_FS_RAMFS",
            "CONFIG_FB": "CONFIG_DRIVER_GRAPHICS_FB",
        }
        lines.append("")
        for old, new in aliases.items():
            lines.append(f"#if defined({new})")
            lines.append(f"#define {old} 1")
            lines.append("#endif")

        lines += ["", "#endif", ""]
        return "\n".join(lines)

    def render_make(self) -> str:
        lines = ["# Auto-generated by BlockOS Kconfig. Do not edit."]
        for node in self.nodes:
            value = node.value
            if value not in ("y", "n"):
                value = value.replace(" ", "")
            lines.append(f"CONFIG_{node.name} := {value}")
        return "\n".join(lines) + "\n"

    def render_settings(self) -> str:
        return "".join(f"CONFIG_{n.name}={'ON' if n.value == 'y' else 'OFF'}\n" for n in self.nodes)


# ---------------------------------------------------------------------------
# Menu UI
# ---------------------------------------------------------------------------

class MenuUI:
    def __init__(self, model: KconfigParser):
        self.m = model
        self.path: List[BaseNode] = [model.root]
        self.cursor = 0
        self.scroll = 0
        self.status = "Arrow keys navigate, <Enter> selects, <Esc> backs out, <?> help."
        self.search = ""

    @property
    def current(self) -> BaseNode:
        return self.path[-1]

    def children(self) -> List[BaseNode]:
        items = [n for n in self.current.children if self.m.visible(n)]
        if self.search:
            q = self.search.lower()
            items = [n for n in items if q in self._search_blob(n)]
        return items

    def _search_blob(self, node: BaseNode) -> str:
        if isinstance(node, ConfigNode):
            return f"{node.name} {node.prompt} {node.help_text}".lower()
        return getattr(node, "prompt", "").lower()

    def run(self, stdscr: "curses._CursesWindow") -> bool:
        self._setup_colors()
        stdscr.keypad(True)
        curses.curs_set(0)
        while True:
            items = self.children()
            if items:
                self.cursor = max(0, min(self.cursor, len(items) - 1))
            else:
                self.cursor = 0
            self._draw(stdscr, items)
            key = stdscr.getch()
            if key in (curses.KEY_UP, ord('k')):
                if items:
                    self.cursor = (self.cursor - 1) % len(items)
            elif key in (curses.KEY_DOWN, ord('j')):
                if items:
                    self.cursor = (self.cursor + 1) % len(items)
            elif key in (curses.KEY_PPAGE,):
                self.cursor = max(0, self.cursor - 8)
            elif key in (curses.KEY_NPAGE,):
                self.cursor = min(max(0, len(items) - 1), self.cursor + 8)
            elif key == curses.KEY_HOME:
                self.cursor = 0
            elif key == curses.KEY_END and items:
                self.cursor = len(items) - 1
            elif key in (ord(' '), ord('y'), ord('n')) and items:
                node = items[self.cursor]
                if isinstance(node, ConfigNode):
                    if key == ord('n'):
                        self.m.set_bool(node, 'n')
                    elif key == ord('y'):
                        self.m.set_bool(node, 'y')
                    else:
                        self.m.toggle(node)
                    self.status = f"{node.name} = {node.value}"
            elif key in (curses.KEY_ENTER, 10, 13):
                if items:
                    node = items[self.cursor]
                    if node.children:
                        self.path.append(node)
                        self.cursor = 0
                        self.scroll = 0
                    elif isinstance(node, ConfigNode):
                        self.m.toggle(node)
            elif key == 27:
                if self.search:
                    self.search = ""
                    self.status = "Search cleared."
                elif len(self.path) > 1:
                    self.path.pop()
                    self.cursor = 0
                else:
                    return False
            elif key in (ord('s'), ord('S')):
                self.m.save()
                self.status = f"Saved .config and generated headers."
            elif key in (ord('q'), ord('Q'), curses.KEY_F10):
                if self._prompt_yesno(stdscr, "Save configuration before exit?", default=True):
                    self.m.save()
                return True
            elif key in (ord('/'),):
                self._search_prompt(stdscr)
            elif key in (ord('?'), curses.KEY_F1):
                self._help(stdscr, items[self.cursor] if items else None)
            elif key in (ord('h'),):
                self._help(stdscr, items[self.cursor] if items else None)
        return True

    def _setup_colors(self) -> None:
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_BLUE)
        curses.init_pair(2, curses.COLOR_BLACK, curses.COLOR_WHITE)
        curses.init_pair(3, curses.COLOR_YELLOW, curses.COLOR_BLUE)
        curses.init_pair(4, curses.COLOR_CYAN, curses.COLOR_BLUE)
        curses.init_pair(5, curses.COLOR_RED, curses.COLOR_BLUE)

    def _draw(self, s, items: List[BaseNode]) -> None:
        s.erase()
        h, w = s.getmaxyx()
        if h < 18 or w < 72:
            s.bkgd(' ', curses.color_pair(1))
            s.addstr(2, 2, "Terminal too small; resize to at least 72x18.", curses.color_pair(5) | curses.A_BOLD)
            s.refresh()
            return
        s.bkgd(' ', curses.color_pair(1))
        title = f" BlockOS Kernel Configuration — {self.m.mainmenu} "
        s.addstr(0, 0, title[:w - 1], curses.color_pair(3) | curses.A_BOLD)
        crumb = " > ".join([n.prompt or "Choice" for n in self.path])
        s.addstr(2, 2, crumb[:w - 4], curses.color_pair(4) | curses.A_BOLD)
        box_top, box_bottom = 4, h - 5
        s.addstr(box_top, 1, "<" + "-" * (w - 4) + ">", curses.color_pair(4))
        visible_rows = max(1, box_bottom - box_top - 1)
        if self.cursor < self.scroll:
            self.scroll = self.cursor
        if self.cursor >= self.scroll + visible_rows:
            self.scroll = self.cursor - visible_rows + 1
        for row, idx in enumerate(range(self.scroll, min(len(items), self.scroll + visible_rows))):
            node = items[idx]
            y = box_top + 1 + row
            selected = idx == self.cursor
            attr = curses.color_pair(2) if selected else curses.color_pair(1)
            s.addstr(y, 2, " " * (w - 5), attr)
            if isinstance(node, ConfigNode):
                marker = "[ * ]" if node.value == 'y' else "[   ]"
                if node.choice:
                    marker = "(*)" if node.value == 'y' else "( )"
                text = f"{marker} {node.prompt}"
            else:
                text = f"> {node.prompt or 'Choice'}"
            s.addstr(y, 3, text[:w - 7], attr)
        s.addstr(h - 4, 2, f"{self.status}"[:w - 4], curses.color_pair(4))
        footer = "<Enter>Select  <Space>Toggle  <Y>/<N>Set  /Search  ?Help  <Esc>Back  S Save  Q Quit"
        s.addstr(h - 2, 1, footer[:w - 2], curses.color_pair(1) | curses.A_BOLD)
        s.refresh()

    def _prompt_yesno(self, s, text: str, default: bool) -> bool:
        h, w = s.getmaxyx()
        answer = 'Y' if default else 'N'
        while True:
            s.addstr(h - 3, 2, f"{text} [Y/n] " if default else f"{text} [y/N] ", curses.color_pair(2))
            s.refresh()
            key = s.getch()
            if key in (ord('y'), ord('Y')):
                return True
            if key in (ord('n'), ord('N')):
                return False
            if key == 10:
                return default
            if key == 27:
                return False
            _ = answer

    def _search_prompt(self, s) -> None:
        curses.echo()
        curses.curs_set(1)
        h, w = s.getmaxyx()
        s.addstr(h - 3, 2, "/" + " " * max(1, w - 5), curses.color_pair(2))
        s.move(h - 3, 3)
        try:
            q = s.getstr(h - 3, 3, w - 5).decode('utf-8', 'ignore')
        finally:
            curses.noecho()
            curses.curs_set(0)
        self.search = q.strip()
        self.cursor = 0
        self.scroll = 0
        self.status = f"Search: {self.search}" if self.search else "Search cleared."

    def _help(self, s, node: Optional[BaseNode]) -> None:
        s.erase()
        h, w = s.getmaxyx()
        s.bkgd(' ', curses.color_pair(1))
        s.addstr(1, 2, "BlockOS menuconfig help", curses.color_pair(3) | curses.A_BOLD)
        lines = [
            "This interface follows the Linux menuconfig interaction model.",
            "Arrow keys move through menu entries.",
            "Enter opens a submenu; Space toggles a boolean option.",
            "Y enables an option, N disables it.",
            "/ searches the current menu, Esc returns to the parent menu.",
            "S writes .config and generated include/generated files.",
            "Q exits and offers to save.",
        ]
        if isinstance(node, ConfigNode):
            lines += ["", f"Symbol: CONFIG_{node.name}", f"Current: {node.value}"]
            if node.help_text:
                lines += ["", node.help_text]
        y = 3
        for line in lines:
            for chunk in line.splitlines() or [""]:
                if y >= h - 2:
                    break
                s.addstr(y, 3, chunk[:w - 6], curses.color_pair(1))
                y += 1
        s.addstr(h - 2, 3, "Press any key to return.", curses.color_pair(4))
        s.refresh()
        s.getch()


def parse_and_maybe_save(mode: str) -> int:
    model = KconfigParser(KCONFIG)
    if mode != "defconfig":
        model.load_config(CONFIG)
    model.save()
    return 0


def main() -> int:
    args = sys.argv[1:]
    if "--help" in args or "-h" in args:
        print("Usage: make menuconfig | make defconfig | make oldconfig | python3 scripts/menuconfig.py [--defconfig|--sync]")
        return 0
    if "--defconfig" in args:
        return parse_and_maybe_save("defconfig")
    if "--sync" in args or "--oldconfig" in args:
        return parse_and_maybe_save("sync")
    model = KconfigParser(KCONFIG)
    model.load_config(CONFIG)
    model._normalize()
    try:
        return 0 if curses.wrapper(MenuUI(model).run) else 0
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
