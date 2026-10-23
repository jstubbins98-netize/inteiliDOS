#!/usr/bin/env python3
"""Host regression: bounded ISO -> streamed read-only DOS FAT view."""
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class CdromTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix="dos-iso-unit-")
        cls.folder=Path(cls.tmp.name)
        library=cls.folder/"bridge.so"
        subprocess.run(["cc","-shared","-fPIC","-Wall","-Wextra","-Werror",
                        str(ROOT/"tests/msdos_cdrom_host.c"),
                        str(ROOT/"kernel/msdos/cdrom.c"),"-o",str(library)],check=True)
        cls.lib=ctypes.CDLL(str(library))
        cls.lib.fixture_open.argtypes=[ctypes.c_char_p]
        cls.lib.msdos_cdrom_error.restype=ctypes.c_char_p
        cls.lib.msdos_cdrom_sectors.restype=ctypes.c_uint32
        disc=cls.folder/"disc";disc.mkdir()
        (disc/"GAME").mkdir()
        cls.payload=bytes(range(256))*520+bytes(range(97))
        (disc/"GAME"/"ASSET.DAT").write_bytes(cls.payload)
        (disc/"GAME"/"EMPTY.DAT").write_bytes(b"")
        (disc/"GAME"/"DEEP").mkdir()
        (disc/"GAME"/"DEEP"/"LEVEL.TXT").write_bytes(b"NESTED")
        for n in range(100):
            (disc/f"F{n:03}.TXT").write_text(str(n))
        for n in ("LONG_FILENAME_ONE.DAT","LONG_FILENAME_TWO.DAT"):
            (disc/n).write_text(n)
        cls.iso=cls.folder/"game.iso"
        subprocess.run(["xorriso","-as","mkisofs","-quiet","-iso-level","2",
                        "-V","GAMEFILES","-o",str(cls.iso),str(disc)],check=True,
                       stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        cls.original=cls.iso.read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.lib.msdos_cdrom_close();cls.tmp.cleanup()

    def setUp(self):
        self.assertEqual(self.lib.fixture_open(str(self.iso).encode()),0)
        self.assertEqual(self.lib.msdos_cdrom_open(),0,self.lib.msdos_cdrom_error())
        self.partition=struct.unpack_from("<I",self.read(0),454)[0]
        bpb=self.read(self.partition)
        self.spc=bpb[13]
        self.fats=struct.unpack_from("<H",bpb,22)[0]
        self.roots=struct.unpack_from("<H",bpb,17)[0]*32//512
        self.root=self.partition+1+2*self.fats
        self.data=self.root+self.roots

    def tearDown(self):
        self.lib.msdos_cdrom_close()

    def read(self,lba):
        out=(ctypes.c_ubyte*512)()
        self.assertEqual(self.lib.msdos_cdrom_read(lba,out),0)
        return bytes(out)

    def entries(self,cluster=0):
        count=self.spc if cluster else self.roots
        start=self.data+(cluster-2)*self.spc if cluster else self.root
        result={}
        for lba in range(start,start+count):
            b=self.read(lba)
            for offset in range(0,512,32):
                e=b[offset:offset+32]
                if not e[0]: return result
                result[e[:11]]=(e[11],struct.unpack_from("<H",e,26)[0],
                               struct.unpack_from("<I",e,28)[0])
        return result

    def test_game_tree_large_file_and_empty_file(self):
        root=self.entries()
        self.assertEqual(len(root),104)  # label + GAME + 100 files + 2 aliases
        attrs,cluster,size=root[b"GAME       "]
        self.assertEqual(attrs,0x11)
        game=self.entries(cluster)
        self.assertEqual(game[b"EMPTY   DAT"][1:],(0,0))
        deep=self.entries(game[b"DEEP       "][1])
        self.assertEqual(deep[b"..         "][1],cluster)
        attrs,first,size=game[b"ASSET   DAT"]
        self.assertEqual(attrs,0x21)
        raw=b"".join(self.read(self.data+(first-2)*self.spc+n)
                     for n in range((size+511)//512))
        self.assertEqual(raw[:size],self.payload)
        self.assertFalse(any(raw[size:]))
        aliases=[name for name in root if b"~" in name]
        self.assertEqual(len(aliases),2)
        self.assertNotEqual(*aliases)

    def test_fat_chains_and_no_allocatable_space(self):
        fat=b"".join(self.read(self.partition+1+i) for i in range(self.fats))
        second=b"".join(self.read(self.partition+1+self.fats+i) for i in range(self.fats))
        self.assertEqual(fat,second)
        words=struct.unpack("<"+"H"*(len(fat)//2),fat)
        self.assertEqual(words[0],0xfff8)
        self.assertNotIn(0,words)
        game=self.entries(self.entries()[b"GAME       "][1])
        first=game[b"ASSET   DAT"][1]
        self.assertEqual(words[first],first+1)
        self.assertEqual(words[first+4],0xffff)
        out=(ctypes.c_ubyte*512)()
        self.assertEqual(self.lib.msdos_cdrom_read(self.lib.msdos_cdrom_sectors(),out),-1)

    def test_physical_read_error_is_not_zero_filled_success(self):
        game=self.entries(self.entries()[b"GAME       "][1])
        first=game[b"ASSET   DAT"][1]
        iso=self.original
        # Locate the first byte sequence of ASSET.DAT; it begins at a sector.
        source=iso.index(self.payload[:4096])//2048
        self.lib.fixture_fail(source)
        out=(ctypes.c_ubyte*512)()
        self.assertEqual(self.lib.msdos_cdrom_read(self.data+(first-2)*self.spc,out),-1)

    def test_bad_and_truncated_iso_rejected_and_reentry_clean(self):
        for name,data in (("signature",self.original[:32769]+b"BAD00"+self.original[32774:]),
                          ("truncated",self.original[:17*2048])):
            self.lib.msdos_cdrom_close()
            bad=self.folder/(name+".iso");bad.write_bytes(data)
            self.lib.fixture_open(str(bad).encode())
            self.assertEqual(self.lib.msdos_cdrom_open(),-1)
            self.assertEqual(self.lib.msdos_cdrom_sectors(),0)
        self.lib.fixture_open(str(self.iso).encode())
        self.assertEqual(self.lib.msdos_cdrom_open(),0)
        self.assertEqual(self.iso.read_bytes(),self.original)


if __name__=="__main__":
    unittest.main()
