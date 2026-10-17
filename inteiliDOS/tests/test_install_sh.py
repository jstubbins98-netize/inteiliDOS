#!/usr/bin/env python3
"""Hermetic installer routing tests: never install real packages or toolchains."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

SCRIPT=Path(__file__).resolve().parents[1]/"install.sh"
BASH=shutil.which("bash")
TOOLS="gcc g++ ld as ar objcopy objdump ranlib strip".split()


class InstallerTests(unittest.TestCase):
    def run_installer(self,host="darwin",brew=True,clt=True,compiler=False,
                      binutils=False,fail_brew=False,yay=True,full=False):
        with tempfile.TemporaryDirectory(prefix="installer-test-") as directory:
            home=Path(directory)
            brewbin=home/"brew/bin"
            brewbin.mkdir(parents=True)
            stub=home/"tool-stub"
            stub.write_text("""#!/usr/bin/env bash
if [[ "${1:-}" == --version ]]; then echo "Mock cross tool 99.0";exit;fi
while [[ $# -gt 0 ]];do
    if [[ "$1" == -o ]];then touch "$2";break;fi
    shift
done
""")
            stub.chmod(0o755)
            if brew:(home/"brew-ready").touch()
            if clt:(home/"clt-ready").touch()
            for tool in TOOLS:
                if (tool in ("gcc","g++") and compiler) or (tool not in ("gcc","g++") and binutils):
                    shutil.copy(stub,brewbin/("i686-elf-"+tool))
            source=SCRIPT.read_text()
            # On non-macOS hosts use our Bash, and never discover a real host brew.
            source=source.replace('/bin/bash "$installer"','"'+BASH+'" "$installer"')
            source=source.replace("/opt/homebrew/bin/brew",str(home/"absent-arm-brew"))
            source=source.replace("/usr/local/bin/brew",str(home/"absent-intel-brew"))
            if not full:
                source=source[:source.index("JOBS=$(")]+"\ninstall_system_deps\n"
            prefix=home/"opt/cross"
            if full and host=="linux-gnu":
                # Disposable source/build fixtures let us verify the entire Linux flow.
                (prefix/"bin").mkdir(parents=True)
                for tool in TOOLS:shutil.copy(stub,prefix/"bin"/("i686-elf-"+tool))
                src=home/"src/inteilidOS-toolchain"
                for package,version in (("binutils","2.41"),("gcc","13.2.0")):
                    folder=src/(package+"-"+version)
                    folder.mkdir(parents=True)
                    (src/(package+"-"+version+".tar.xz")).touch()
                    configure=folder/"configure"
                    configure.write_text('#!/usr/bin/env bash\necho "configure '+package+' $*" >> "$TEST_LOG"\n')
                    configure.chmod(0o755)
                (src/"gcc-13.2.0/gmp").mkdir()
            harness=r'''
OSTYPE="$HOST"
command() {
    if [[ "${1:-}" == -v ]];then
        case "${2:-}" in
            brew) [[ -f "$HOME/brew-ready" ]] || return 1;;
            apt-get|dnf|zypper) return 1;;
            pacman) [[ "$HOST" == linux-gnu ]] && { echo pacman;return 0; };return 1;;
            yay) [[ "$HAS_YAY" == 1 ]] && { echo yay;return 0; };return 1;;
        esac
    fi
    builtin command "$@"
}
xcode-select() {
    echo "xcode-select $*" >> "$TEST_LOG"
    if [[ "$1" == --install ]];then touch "$HOME/clt-ready";return;fi
    [[ -f "$HOME/clt-ready" ]]
}
xcrun() { [[ -f "$HOME/clt-ready" ]]; }
curl() {
    echo "curl $*" >> "$TEST_LOG"
    while [[ $# -gt 0 ]];do
        if [[ "$1" == -o ]];then
            printf '%s\n' 'touch "$HOME/brew-ready"' > "$2";return
        fi
        shift
    done
    return 1
}
brew() {
    echo "brew $*" >> "$TEST_LOG"
    case "$1" in
        shellenv) printf 'export PATH="%s/brew/bin:$PATH"\n' "$HOME";;
        --prefix) echo "$HOME/brew";;
        install)
            [[ "$FAIL_BREW" == 0 ]] || return 7
            if [[ "${2:-}" == i686-elf-gcc || "${2:-}" == i686-elf-binutils ]];then
                for t in gcc g++ ld as ar objcopy objdump ranlib strip;do
                    cp "$STUB" "$HOME/brew/bin/i686-elf-$t"
                done
            fi;;
    esac
}
sudo() { echo "sudo $*" >> "$TEST_LOG"; }
yay() { echo "yay $*" >> "$TEST_LOG"; }
make() { echo "make $*" >> "$TEST_LOG"; }
'''
            env={"HOME":str(home),"PATH":str(brewbin)+":"+os.environ.get("PATH",os.defpath),
                 "SHELL":"/bin/bash","TEST_LOG":str(home/"calls"),"STUB":str(stub),
                 "HOST":host,"FAIL_BREW":str(int(fail_brew)),"HAS_YAY":str(int(yay))}
            result=subprocess.run([BASH,"-c",harness+"\n"+source],env=env,input="y\n",
                                  text=True,capture_output=True,timeout=15)
            log=(home/"calls").read_text() if (home/"calls").exists() else ""
            snippet=(prefix/"env.sh").read_text() if (prefix/"env.sh").exists() else ""
            return result,log,snippet

    def test_mac_installs_missing_compiler_without_source_build(self):
        result,log,snippet=self.run_installer(full=True)
        self.assertEqual(result.returncode,0,result.stderr+result.stdout)
        self.assertIn("brew install i686-elf-gcc",log)
        self.assertNotIn("configure ",log)
        self.assertNotIn("Downloading sources",result.stdout)
        self.assertIn("/brew/bin:",snippet)
        self.assertIn("Compiler smoke-test passed",result.stdout)

    def test_mac_reuses_existing_compiler_and_tools(self):
        result,log,_=self.run_installer(compiler=True,binutils=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertNotIn("brew install i686-elf-gcc",log)
        self.assertNotIn("brew install i686-elf-binutils",log)
        self.assertNotIn("xcode-select --install",log)

    def test_mac_missing_binutils_installed(self):
        result,log,_=self.run_installer(compiler=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn("brew install i686-elf-binutils",log)

    def test_mac_bootstraps_brew_and_command_line_tools(self):
        result,log,_=self.run_installer(brew=False,clt=False,full=True)
        self.assertEqual(result.returncode,0,result.stderr+result.stdout)
        self.assertIn("xcode-select --install",log)
        self.assertIn("https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh",log)
        self.assertIn("brew shellenv",log)
        self.assertIn("brew install i686-elf-gcc",log)

    def test_brew_failure_stops_installer(self):
        result,_,_=self.run_installer(fail_brew=True)
        self.assertNotEqual(result.returncode,0)
        self.assertNotIn("System dependencies ready",result.stdout)

    def test_arch_yay_then_source_build(self):
        result,log,_=self.run_installer(host="linux-gnu",full=True)
        self.assertEqual(result.returncode,0,result.stderr+result.stdout)
        self.assertIn("yay -S i686-elf-gcc",log)
        self.assertIn("configure binutils --target=i686-elf",log)
        self.assertIn("configure gcc --target=i686-elf",log)
        self.assertIn("all-target-libgcc",log)
        self.assertLess(log.index("yay -S"),log.index("configure binutils"))
        self.assertNotIn("brew install",log)

    def test_arch_missing_yay_has_actionable_error(self):
        result,_,_=self.run_installer(host="linux-gnu",yay=False)
        self.assertNotEqual(result.returncode,0)
        self.assertIn("Install yay, then rerun",result.stderr)


if __name__=="__main__":unittest.main()
