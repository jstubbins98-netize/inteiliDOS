"""Extract only literal PLAY strings; never execute any other BASIC statement."""
from fractions import Fraction
import re


def extract(source):
    # Anchored matching ignores commented-out PLAY lines and every UI/SLEEP/
    # typewriter instruction. State persists between consecutive PLAY strings.
    return re.findall(r'^\s*PLAY\s+"([^"]*)"', source, re.M | re.I)


def convert(source):
    octave, length, tempo = 4, 4, 120
    articulation = Fraction(7, 8)  # QBasic's default MN
    clock = Fraction(0)
    segments = []
    def append(hz, duration):
        nonlocal clock
        start = int(clock)
        clock += duration
        ms = int(clock)-start
        if ms:
            if segments and segments[-1][0] == hz:
                segments[-1] = (hz, segments[-1][1]+ms)
            else:
                segments.append((hz, ms))
    for commands in extract(source):
        commands = re.sub(r"\s+", "", commands.upper())
        pos = 0
        while pos < len(commands):
            token = commands[pos]; pos += 1
            if token == "M":
                if pos >= len(commands): raise ValueError("Incomplete M command")
                mode = commands[pos]; pos += 1
                if mode == "N": articulation = Fraction(7, 8)
                elif mode == "L": articulation = Fraction(1)
                elif mode == "S": articulation = Fraction(3, 4)
                elif mode not in "BF": raise ValueError("Unknown music mode: "+mode)
                continue  # MB/MF change queueing, not pitches/durations
            if token in "<>":
                octave += 1 if token == ">" else -1
                if not 0 <= octave <= 6: raise ValueError("Octave outside QBasic range")
                continue
            accidental = 0
            if token in "ABCDEFG" and pos < len(commands) and commands[pos] in "#+-":
                accidental = -1 if commands[pos] == "-" else 1
                pos += 1
            start = pos
            while pos < len(commands) and commands[pos].isdigit(): pos += 1
            number = int(commands[start:pos]) if pos > start else None
            if token in "OLT":
                if number is None: raise ValueError("Missing music parameter")
                if token == "O":
                    if not 0 <= number <= 6: raise ValueError("Invalid octave")
                    octave = number
                elif token == "L":
                    if not 1 <= number <= 64: raise ValueError("Invalid note length")
                    length = number
                else:
                    if not 32 <= number <= 255: raise ValueError("Invalid tempo")
                    tempo = number
                continue
            if token not in "ABCDEFGPN": raise ValueError("Unsupported PLAY token: "+token)
            denominator = length if number is None or token == "N" else number
            if not 1 <= denominator <= 64: raise ValueError("Invalid duration")
            duration = Fraction(240000, tempo*denominator)
            while pos < len(commands) and commands[pos] == ".":
                duration *= Fraction(3, 2); pos += 1
            if token == "P" or (token == "N" and number == 0):
                append(0, duration)
                continue
            if token == "N":
                if number is None or not 1 <= number <= 84: raise ValueError("Invalid N note")
                midi = number+23
            else:
                # QB octave 3 starts at middle C (C4 / MIDI 60), not C3.
                midi = 24+octave*12+{"C":0,"D":2,"E":4,"F":5,"G":7,"A":9,"B":11}[token]+accidental
            hz = round(440*2**((midi-69)/12))
            append(hz, duration*articulation)
            append(0, duration*(1-articulation))
    if not segments: raise ValueError("No PLAY melody in BASIC source")
    return segments, int(clock)
