"""Native word processor model, real PDF parsing, and full key-driven UI tests.

No physical disks: storage is in-memory, VGA is a private mmap fixture.
Run: python3 inteiliDOS/tests/test_writer.py
"""
import ctypes as C
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CAP = 16384
F1, F2, F3, F4, F5, F6, F7, F8, F9, F10 = 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99
LEFT, RIGHT, UP, DOWN = 0x82, 0x83, 0x80, 0x81


class Doc(C.Structure):
    _fields_ = [("text", C.c_char * CAP), ("length", C.c_uint32),
                ("cursor", C.c_uint32), ("dirty", C.c_int),
                ("style", C.c_uint8 * CAP), ("typing_style", C.c_uint8)]


class WriterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="inteiliwriter-")
        cls.path = Path(cls.tmp.name)
        lib = cls.path / "writer.so"
        subprocess.run([
            "cc", "-shared", "-fPIC", "-Os", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            str(ROOT / "shell/writer.c"), str(ROOT / "shell/writer_document.c"),
            str(ROOT / "shell/storage_dialog.c"), str(ROOT / "tests/writer_host.c"),
            "-o", str(lib)], check=True)
        cls.lib = C.CDLL(str(lib))
        cls.lib.writer_document_init.argtypes = [C.POINTER(Doc)]
        cls.lib.writer_document_load.argtypes = [C.POINTER(Doc), C.c_char_p, C.c_uint32]
        cls.lib.writer_document_load_markdown.argtypes = cls.lib.writer_document_load.argtypes
        cls.lib.writer_insert.argtypes = [C.POINTER(Doc), C.c_char]
        cls.lib.writer_backspace.argtypes = cls.lib.writer_delete_line.argtypes = [C.POINTER(Doc)]
        cls.lib.writer_position.argtypes = [C.POINTER(Doc), C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]
        cls.lib.writer_vertical.argtypes = [C.POINTER(Doc), C.c_int]
        cls.lib.writer_words.argtypes = [C.POINTER(Doc)]
        cls.lib.writer_export_pdf.argtypes = [C.POINTER(Doc), C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32)]
        cls.lib.writer_export_text.argtypes = [
            C.POINTER(Doc), C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32)]
        cls.lib.writer_host_keys.argtypes = [C.POINTER(C.c_int), C.c_int]
        cls.lib.writer_host_store.argtypes = [C.c_char_p, C.c_char_p, C.c_uint32]
        cls.lib.writer_host_fetch.argtypes = [C.c_char_p, C.c_void_p]
        cls.lib.writer_host_screen.restype = C.c_char_p

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.doc = Doc()
        self.lib.writer_document_init(C.byref(self.doc))

    def load(self, text):
        raw = text.encode("ascii") if isinstance(text, str) else text
        return self.lib.writer_document_load(C.byref(self.doc), raw, len(raw))

    def text(self):
        return C.string_at(C.addressof(self.doc), self.doc.length).decode("ascii")

    def export(self, pdf=False, markdown=False, capacity=65536):
        out = C.create_string_buffer(capacity)
        size = C.c_uint32(99)
        if pdf:
            rc = self.lib.writer_export_pdf(C.byref(self.doc), out, capacity, C.byref(size))
        else:
            rc = self.lib.writer_export_text(
                C.byref(self.doc), markdown, out, capacity, C.byref(size))
        return rc, out.raw[:size.value]

    def test_load_ascii_crlf_tabs_and_invalid_input_is_transactional(self):
        self.assertEqual(self.load(b"# Title\r\nOne\ttwo\rThree"), 0)
        self.assertEqual(self.text(), "# Title\nOne    two\nThree")
        for bad in (b"bad\0data", b"\xff", b"\t" * (CAP // 4 + 1), b"x" * (CAP + 1)):
            self.assertEqual(self.load(bad), -1)
            self.assertEqual(self.text(), "# Title\nOne    two\nThree")
        self.assertEqual(self.load(b"\r\n" * CAP), 0)
        self.assertEqual(self.doc.length, CAP)
        self.assertEqual(self.export()[1], b"\r\n" * CAP)

    def test_cursor_insert_backspace_and_delete_logical_line(self):
        self.load("Hello world\nSecond line\nThird")
        self.doc.cursor = 6
        for char in b"brave ":
            self.assertEqual(self.lib.writer_insert(C.byref(self.doc), bytes([char])), 0)
        self.assertTrue(self.text().startswith("Hello brave world"))
        self.lib.writer_backspace(C.byref(self.doc))
        self.assertTrue(self.text().startswith("Hello braveworld"))
        self.doc.cursor = self.text().index("Second") + 2
        self.lib.writer_delete_line(C.byref(self.doc))
        self.assertEqual(self.text(), "Hello braveworld\nThird")
        self.doc.cursor = self.doc.length
        self.lib.writer_delete_line(C.byref(self.doc))
        self.assertEqual(self.text(), "Hello braveworld")

    def test_randomized_editing_matches_reference(self):
        random.seed(124)
        text, cursor = "", 0
        for _ in range(3000):
            operation = random.randrange(4)
            if operation == 0:
                char = random.choice("abc ()\\\n")
                self.lib.writer_insert(C.byref(self.doc), char.encode())
                text = text[:cursor] + char + text[cursor:]
                cursor += 1
            elif operation == 1:
                self.lib.writer_backspace(C.byref(self.doc))
                if cursor:
                    text = text[:cursor - 1] + text[cursor:]
                    cursor -= 1
            elif operation == 2:
                cursor = random.randrange(len(text) + 1)
                self.doc.cursor = cursor
            else:
                self.lib.writer_delete_line(C.byref(self.doc))
                start = text.rfind("\n", 0, cursor) + 1
                end = text.find("\n", cursor)
                if end < 0:
                    end = len(text)
                    if start:
                        start -= 1
                else:
                    end += 1
                text = text[:start] + text[end:]
                cursor = start
            self.assertEqual(self.text(), text)
            self.assertEqual(self.doc.cursor, cursor)

    def test_capacity_and_soft_wrap_do_not_drop_text(self):
        self.load("x" * CAP)
        self.doc.cursor = 40
        self.assertEqual(self.lib.writer_insert(C.byref(self.doc), b"z"), -1)
        self.assertEqual(self.doc.length, CAP)
        row, col = C.c_uint32(), C.c_uint32()
        self.doc.cursor = 78
        self.lib.writer_position(C.byref(self.doc), C.byref(row), C.byref(col))
        self.assertEqual((row.value, col.value), (1, 2))
        self.lib.writer_vertical(C.byref(self.doc), -1)
        self.assertEqual(self.doc.cursor, 2)
        self.assertEqual(self.export(markdown=True)[1], b"x" * CAP)
        self.load("x" * 76 + "\n" + "y" * 100)
        self.doc.cursor = 76
        self.lib.writer_vertical(C.byref(self.doc), 1)
        self.lib.writer_position(C.byref(self.doc), C.byref(row), C.byref(col))
        self.assertEqual((row.value, col.value), (1, 75))

    def test_text_and_markdown_export_and_output_limit(self):
        self.load("# Heading\n\nHello *world* (test)\\file")
        self.assertEqual(self.lib.writer_words(C.byref(self.doc)), 5)
        self.assertEqual(self.export(markdown=True)[1],
                         self.text().replace("\\", "\\\\").replace("*", "\\*").encode())
        self.assertEqual(self.export()[1], self.text().replace("\n", "\r\n").encode())
        self.assertEqual(self.export(capacity=5), (-1, b""))

    def check_pdf(self, raw):
        self.assertTrue(raw.startswith(b"%PDF-1.4"))
        xref = int(re.search(rb"startxref\n(\d+)", raw)[1])
        self.assertEqual(raw[xref:xref + 4], b"xref")
        table = raw[xref:].splitlines()
        count = int(table[1].split()[1])
        for object_id in range(1, count):
            offset = int(table[2 + object_id].split()[0])
            self.assertTrue(raw[offset:].startswith(f"{object_id} 0 obj\n".encode()))
        for match in re.finditer(rb"/Length (\d+) >>\nstream\n", raw):
            size = int(match[1])
            self.assertEqual(raw[match.end() + size:match.end() + size + 9], b"endstream")
        if shutil.which("pdfinfo") and shutil.which("pdftotext"):
            path = self.path / "document.pdf"
            path.write_bytes(raw)
            info = subprocess.check_output(["pdfinfo", str(path)], text=True)
            text = subprocess.check_output(["pdftotext", "-layout", str(path), "-"], text=True)
            return info, text
        return "", ""

    def test_pdf_pages_escaping_xref_stream_lengths_and_real_parser(self):
        self.load("\n".join(f"Line {i} (test) \\ folder" for i in range(125)))
        rc, raw = self.export(pdf=True)
        self.assertEqual(rc, 0)
        self.assertIn(b"\\(test\\) \\\\ folder", raw)
        info, text = self.check_pdf(raw)
        if info:
            self.assertRegex(info, r"Pages:\s+3")
            self.assertIn("Line 124 (test) \\ folder", text)
        self.assertEqual(self.export(pdf=True, capacity=100), (-1, b""))
        self.load("\n" * CAP)
        self.assertEqual(self.export(pdf=True), (-1, b""))

    def queue(self, *items):
        keys = []
        for item in items:
            keys.extend(ord(c) if c != "\n" else 13 for c in item) if isinstance(item, str) else keys.append(item)
        array = (C.c_int * len(keys))(*keys)
        self.lib.writer_host_keys(array, len(keys))
        self.lib.writer_run()
        self.assertEqual(self.lib.writer_host_exhausted(), 0, "UI requested unexpected keys")

    def fetch(self, path):
        out = C.create_string_buffer(65536)
        length = self.lib.writer_host_fetch(path.encode(), out)
        self.assertGreaterEqual(length, 0, f"Missing output: {path}")
        return out.raw[:length]

    def reset_ui(self):
        self.assertEqual(self.lib.writer_host_reset(), 0)

    def test_ui_usb_all_formats_left_right_save_load_delete_and_overwrite(self):
        self.reset_ui()
        self.queue("Hello world", *([LEFT] * 5), "brave ",
                   F2, "DOC.TXT\n30",  # Choose USB, then USB0.
                   F3, "DOC.TXT\n30",  # Actual reload of the saved content.
                   RIGHT, F4, "Final (text)\\demo\nLine two",
                   F5, "DOC.TXT\n0y", F6, "DOC.md\n0", F7, "DOC.pdf\n0",
                   F2, "\ny", F10)
        self.assertEqual(self.fetch("USB0:\\DOC.TXT"), b"Final (text)\\demo\r\nLine two")
        self.assertEqual(self.fetch("USB0:\\DOC.md"), b"Final (text)\\\\demo\nLine two")
        self.check_pdf(self.fetch("USB0:\\DOC.pdf"))
        self.assertIn(b"inteiliWriter", self.lib.writer_host_screen())

    def test_ui_failed_or_invalid_load_preserves_document(self):
        self.reset_ui()
        self.lib.writer_host_store(b"USB1:\\BAD.TXT", b"\x00binary", 7)
        self.queue("Keep this", F3, "yUSB1:\\BAD.TXT\n",
                   F5, "USB0:\\KEEP.TXT\n", F10, "y")
        self.assertEqual(self.fetch("USB0:\\KEEP.TXT"), b"Keep this")

    def test_ui_save_failure_keeps_unsaved_document(self):
        self.reset_ui()
        self.lib.writer_host_fail(1)
        self.queue("Still here", F2, "USB0:\\FAIL.TXT\n", F10, "n", F10, "y")
        self.assertIn(b"* unsaved", self.lib.writer_host_screen())
        self.assertIn(b"Still here", self.lib.writer_host_screen())

    def test_ui_native_save_cancel_and_ordinary_words(self):
        self.reset_ui()
        self.queue("save load quit", F5, 27, F2, "NATIVE.MD\n1", F10)
        self.assertEqual(self.fetch("C:\\NATIVE.MD"), b"save load quit")

    def test_formatting_roundtrip_all_transitions_and_pdf_fonts(self):
        for old in range(4):
            for new in range(4):
                self.load("AB")
                self.doc.style[0], self.doc.style[1] = old, new
                rc, encoded = self.export(markdown=True)
                self.assertEqual(rc, 0)
                self.assertEqual(self.lib.writer_document_load_markdown(
                    C.byref(self.doc), encoded, len(encoded)), 0)
                self.assertEqual(self.text(), "AB")
                self.assertEqual(list(self.doc.style[:2]), [old, new], encoded)
        self.load("normal bold italic both")
        for i in range(7, 11): self.doc.style[i] = 1
        for i in range(12, 18): self.doc.style[i] = 2
        for i in range(19, 23): self.doc.style[i] = 3
        self.assertEqual(self.export(markdown=True)[1], b"normal **bold** *italic* ***both***")
        self.assertEqual(self.export()[1], b"normal bold italic both")
        rc, raw = self.export(pdf=True)
        self.assertEqual(rc, 0)
        self.check_pdf(raw)
        for font in (b"Courier", b"Courier-Bold", b"Courier-Oblique", b"Courier-BoldOblique"):
            self.assertIn(b"/BaseFont /" + font, raw)
        for number, text in ((2, b"bold"), (3, b"italic"), (4, b"both")):
            self.assertIn(f"/F{number} 10 Tf\n(".encode() + text + b") Tj", raw)

    def test_random_formatted_markdown_and_edit_style_alignment(self):
        random.seed(41)
        for _ in range(50):
            self.load("".join(random.choice("abc *\\\n") for _ in range(120)))
            expected = self.text()
            styles = [random.randrange(4) for _ in expected]
            self.doc.style[:len(styles)] = styles
            rc, encoded = self.export(markdown=True)
            self.assertEqual(rc, 0)
            self.assertEqual(self.lib.writer_document_load_markdown(
                C.byref(self.doc), encoded, len(encoded)), 0)
            self.assertEqual(self.text(), expected)
            self.assertEqual([self.doc.style[i] for i, c in enumerate(expected) if c not in " \n"],
                             [styles[i] for i, c in enumerate(expected) if c not in " \n"], encoded)
        self.load("one\ntwo\nthree")
        for i in range(self.doc.length): self.doc.style[i] = i % 4
        self.doc.cursor = 5
        self.doc.typing_style = 3
        self.lib.writer_insert(C.byref(self.doc), b"X")
        self.assertEqual(self.doc.style[5], 3)
        self.assertEqual(self.doc.style[6], 1)
        self.lib.writer_backspace(C.byref(self.doc))
        self.assertEqual(self.doc.style[5], 1)
        self.lib.writer_delete_line(C.byref(self.doc))
        self.assertEqual(self.text(), "one\nthree")
        self.assertEqual(list(self.doc.style[4:9]), [0, 1, 2, 3, 0])

    def test_markdown_decoded_limit_invalid_load_and_plain_load_reset_styles(self):
        data = b"**" + b"x" * CAP + b"**"
        self.assertEqual(self.lib.writer_document_load_markdown(
            C.byref(self.doc), data, len(data)), 0)
        self.assertEqual(self.doc.length, CAP)
        self.assertEqual(self.doc.style[CAP - 1], 1)
        self.assertEqual(self.lib.writer_document_load_markdown(
            C.byref(self.doc), b"**bad\x00data**", 12), -1)
        self.assertEqual(self.doc.length, CAP)
        self.assertEqual(self.doc.style[CAP - 1], 1)
        self.load("plain")
        self.assertEqual(list(self.doc.style[:5]), [0] * 5)
        self.assertEqual(self.doc.typing_style, 0)

    def test_ui_format_toggles_markdown_reload_and_plain_save_warning(self):
        self.reset_ui()
        self.queue("Normal ", F8, "Bold", F8, " ", F9, "Italic", F8, "Both", F9, F8,
                   F6, "USB0:\\STYLE.MD\n", F7, "USB0:\\STYLE.PDF\n",
                   F3, "yUSB0:\\STYLE.MD\n",  # Restore actual character formatting.
                   F6, "USB0:\\COPY.MD\n",
                   F2, "USB0:\\PLAIN.TXT\nn", F10)
        self.assertEqual(self.fetch("USB0:\\STYLE.MD"), b"Normal **Bold** *Italic****Both***")
        self.assertEqual(self.fetch("USB0:\\COPY.MD"), self.fetch("USB0:\\STYLE.MD"))
        self.assertIn(b"Courier-BoldOblique", self.fetch("USB0:\\STYLE.PDF"))
        # The cancelled plain-text save must not write a file.
        out = C.create_string_buffer(65536)
        self.assertLess(self.lib.writer_host_fetch(b"USB0:\\PLAIN.TXT", out), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
