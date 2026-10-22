#!/usr/bin/env python3
"""Exercise real interactive build.sh prompts in a PTY; never edit live config."""
import os
from pathlib import Path
import pty
import re
import select
import shutil
import subprocess
import tempfile
import time
import unittest

SCRIPT=Path(__file__).resolve().parents[1]/"build.sh"
QUESTION="Enable sound card emulation in the MS-DOS emulator?"


def wizard(cpu, sound=None, help_first=False, model="1"):
    with tempfile.TemporaryDirectory(prefix="willburd-audio-") as folder:
        script=Path(folder)/"build.sh";shutil.copy2(SCRIPT,script)
        answers=["",str(cpu)]
        if cpu>=5:
            if help_first: answers+=["H"]
            answers+=[sound or ""]
            if sound=="1":answers+=[model]
        answers+=["","2","1","1","1","1","1"]
        master,slave=pty.openpty()
        child=subprocess.Popen(["bash",str(script),"--config-only"],
            stdin=slave,stdout=slave,stderr=slave,start_new_session=True)
        os.close(slave)
        text="";offset=0;sent=0;deadline=time.monotonic()+15
        try:
            while time.monotonic()<deadline:
                if select.select([master],[],[],.1)[0]:
                    try: chunk=os.read(master,65536)
                    except OSError: break
                    if not chunk: break
                    text+=chunk.decode(errors="replace")
                while True:
                    prompt=re.search(r"(?:Name|Choice|Number) \[[^\]]*\]: ",text[offset:])
                    if not prompt: break
                    offset+=prompt.end()
                    if sent>=len(answers): raise AssertionError("Unexpected prompt:\n"+text)
                    os.write(master,(answers[sent]+"\n").encode());sent+=1
                if child.poll() is not None: break
            child.wait(timeout=2)
            assert child.returncode==0,text
            assert sent==len(answers),(sent,len(answers),text)
            return text,(Path(folder)/"configurations/config.h").read_text()
        finally:
            if child.poll() is None: child.kill();child.wait()
            os.close(master)


class SoundWizardTest(unittest.TestCase):
    def test_modern_yes_no_and_help(self):
        for cpu,choice in ((5,"1"),(5,"2"),(6,"1"),(7,"1"),(7,"")):
            with self.subTest(cpu=cpu,choice=choice):
                text,config=wizard(cpu,choice,help_first=cpu==6)
                self.assertIn(QUESTION,text)
                enabled=int(choice=="1")
                self.assertIn(f"#define CONFIG_MSDOS_SOUND {enabled}",config)
                if enabled: self.assertIn("#define CONFIG_ENABLE_PCI 1",config)
                self.assertIn("#define CONFIG_MSDOS_SOUND_MODEL 2",config)
                self.assertEqual("Which sound card should" in text,bool(enabled))
                self.assertIn("#define CONFIG_AUDIO_MODE 0",config)

    def test_legacy_skips_question(self):
        for cpu in (1,4):
            text,config=wizard(cpu)
            self.assertNotIn(QUESTION,text)
            self.assertIn("#define CONFIG_MSDOS_SOUND 0",config)

    def test_sound_card_choice(self):
        for cpu in (5,6,7):
            for model in ("1","2",""):
                with self.subTest(cpu=cpu,model=model):
                    text,config=wizard(cpu,"1",model=model)
                    expected=16 if model=="2" else 2
                    self.assertIn(f"#define CONFIG_MSDOS_SOUND_MODEL {expected}",config)
                    self.assertIn(f"(Sound Blaster {expected})",text)


if __name__=="__main__":
    unittest.main()
