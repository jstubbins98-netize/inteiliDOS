#!/usr/bin/env python3
"""Verify PLAY pitch, timing, articulation and melody-only extraction."""
from pathlib import Path
import unittest
from qbasic_music import convert, extract


class MusicTests(unittest.TestCase):
    def test_normal(self):
        self.assertEqual(convert('PLAY "T100O3L8A"'), ([(440,262),(0,38)],300))

    def test_legato_dots_octaves_and_accidentals(self):
        self.assertEqual(convert('PLAY "T100MLO3L4C..>C+<B-"'),
                         ([(262,1350),(554,600),(466,600)],2550))

    def test_rest_and_staccato(self):
        self.assertEqual(convert('PLAY "T100MSO3L4AP8"'),
                         ([(440,450),(0,450)],900))

    def test_state_persists_and_queue_modes_do_not_change_notes(self):
        self.assertEqual(convert('PLAY "T100MLO3L8A"\nPLAY "MB>E"\nPLAY "MF<E"'),
                         ([(440,300),(659,300),(330,300)],900))

    def test_only_music_is_imported(self):
        source = ('PRINT "No importing text"\nCOLOR 14, 6\nSLEEP 999\n'
                  '\'PLAY "T255O6B"\nPLAY "T100MLO3A"\n'
                  'CALL typeout("Do not import this")\nPLAY "P4"')
        self.assertEqual(extract(source),["T100MLO3A","P4"])
        self.assertEqual(convert(source), ([(440,600),(0,600)],1200))

    def test_reject_unknown_music(self):
        with self.assertRaises(ValueError): convert('PLAY "O3Z"')

    def test_supplied_source(self):
        root=Path(__file__).resolve().parents[1]
        source=(root/"assets/melody.bas").read_text(encoding="cp437")
        notes,total=convert(source)
        self.assertGreater(len(extract(source)),200)
        self.assertEqual(notes[:6],[(110,262),(0,38),(165,262),(0,38),(110,262),(0,38)])
        self.assertEqual(sum(ms for _,ms in notes),total)
        self.assertTrue(all(ms>0 and (hz==0 or 37<=hz<=32767) for hz,ms in notes))
        # None of the BASIC UI text or its alternate lyrics enter the output.
        data=(root/"data.h").read_text()
        self.assertNotIn("Emergency Boot",data)
        self.assertNotIn("[REDACTED]",data)
        self.assertNotIn("Thanks for watching",data)
        print(f"Extracted {len(notes)} segments, {total} ms from active PLAY statements")


if __name__=="__main__": unittest.main()
