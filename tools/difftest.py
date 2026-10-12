#!/usr/bin/env python3
"""Differential test of one decompiled function against the recompiled build.

For every snapshot and argument vector, ps2run calls the function twice on the same machine state:
once recompiled, once with the decompiled hook enabled. The two result files are compared.

usage: difftest.py --ps2run PS2RUN --elf ELF --function ADDR --hook NAME --snapshots DIR --out DIR
                   [--args "--f12 f:1.5 --a0 8" | --args snapshot] [--args-file cases.txt]
                   [--returns v0,v1,f0,f1] [--compare-fpr f0,..] [--compare-vu] [--strict]
                   [-j N] [--disc ISO] [--env NAME=VALUE]... [--timeout SECONDS]

Exit code: 0 no case failed (skips allowed), 1 a case failed, 2 harness or setup error.
The result files are game-derived: keep --out outside the repository.
"""
import argparse
import concurrent.futures
import json
import os
import shlex
import struct
import subprocess
import sys

CALLEE_SAVED_GPR = (16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31)
CALLEE_SAVED_FPR = tuple(range(20, 32))
GPR_NAMES = ('zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
             's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra')
ABORTED = ('hung', 'forbidden_syscall', 'thread_switch', 'halted', 'timeout')
MEM_KINDS = {1: 'ram', 2: 'spr', 3: 'stack'}
DEFAULT_ENV = {'PS2_GPU_FALLBACK': '1', 'PS2_GS': 'off'}


class SetupError(Exception):
    """The harness could not produce a result: not a verdict on the function."""


def parse_register(name):
    """'v0' -> ('r', 2); 'f12' -> ('f', 12); 'r5' -> ('r', 5)."""
    name = name.strip().lower()
    if name in GPR_NAMES:
        return ('r', GPR_NAMES.index(name))
    if len(name) > 1 and name[0] in 'rf' and name[1:].isdigit() and int(name[1:]) < 32:
        return (name[0], int(name[1:]))
    raise SetupError(f'unknown register {name}')


def register_label(kind, index):
    return GPR_NAMES[index] if kind == 'r' else f'f{index}'


def gpr64(value):
    """The low 64 bits of a 128-bit register as written by the harness (32 hex digits)."""
    return value[-16:]


def read_mem(path):
    """The .mem sidecar as {(kind, address): bytes}."""
    out = {}
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError:
        return out
    at = 0
    while at + 9 <= len(data):
        kind, address, length = struct.unpack_from('<BII', data, at)
        out[(MEM_KINDS.get(kind, '?'), address)] = data[at + 9:at + 9 + length]
        at += 9 + length
    return out


def hex_bytes(data):
    return ' '.join(f'{b:02x}' for b in data)


def compare_ranges(kind, a, b, a_mem, b_mem):
    """None when the two range lists agree, else the description of the first difference."""
    for i in range(max(len(a), len(b))):
        if i >= len(a):
            return f'{kind} {b[i]["at"]}+{b[i]["len"]} written by decomp only'
        if i >= len(b):
            return f'{kind} {a[i]["at"]}+{a[i]["len"]} written by recomp only'
        x, y = a[i], b[i]
        if (x['at'], x['len']) != (y['at'], y['len']):
            return f'{kind} ranges differ: recomp {x["at"]}+{x["len"]}, decomp {y["at"]}+{y["len"]}'
        if x['fnv'] != y['fnv']:
            address = int(x['at'], 16)
            xb, yb = a_mem.get((kind, address)), b_mem.get((kind, address))
            if xb is None or yb is None:
                return f'{kind} {x["at"]}+{x["len"]} differs (no bytes in the .mem files)'
            offset = next((n for n in range(min(len(xb), len(yb))) if xb[n] != yb[n]), 0)
            return (f'{kind} {x["at"]}+{x["len"]} differs at +{offset}: '
                    f'{hex_bytes(xb[offset:offset + 16])} != {hex_bytes(yb[offset:offset + 16])}')
    return None


def compare(recomp, decomp, returns, recomp_mem=None, decomp_mem=None, strict=False, compare_vu=False):
    """('PASS' | 'FAIL' | 'SKIP', text) for one case, from the two result documents."""
    if recomp['status'] != 'ok':
        detail = f': {recomp["detail"]}' if recomp.get('detail') else ''
        return 'SKIP', f'recomp {recomp["status"]}{detail}'
    if decomp['status'] != 'ok':
        detail = f': {decomp["detail"]}' if decomp.get('detail') else ''
        return 'FAIL', f'status decomp {decomp["status"]}{detail}'
    if not decomp.get('hook_ran'):
        return 'FAIL', 'the hook did not run on the decomp side'
    if recomp.get('hook_ran'):
        return 'FAIL', 'a hook ran on the recomp side'
    ra, da = recomp['after'], decomp['after']
    for kind, index in returns:
        if kind == 'r':
            x, y = gpr64(ra['r'][index]), gpr64(da['r'][index])
        else:
            x, y = ra['f'][index], da['f'][index]
        if x != y:
            return 'FAIL', f'{register_label(kind, index)} {x} != {y}'
    for side, doc in (('recomp', recomp), ('decomp', decomp)):
        before, after = doc['before'], doc['after']
        for index in CALLEE_SAVED_GPR:
            if before['r'][index] != after['r'][index]:
                return 'FAIL', f'abi {side} changed {GPR_NAMES[index]}: {gpr64(before["r"][index])} -> {gpr64(after["r"][index])}'
        for index in CALLEE_SAVED_FPR:
            if before['f'][index] != after['f'][index]:
                return 'FAIL', f'abi {side} changed f{index}: {before["f"][index]} -> {after["f"][index]}'
    for kind in ('ram', 'spr'):
        difference = compare_ranges(kind, recomp[kind], decomp[kind], recomp_mem or {}, decomp_mem or {})
        if difference:
            return 'FAIL', difference
    if recomp['io'] != decomp['io']:
        n = next((i for i in range(min(len(recomp['io']), len(decomp['io']))) if recomp['io'][i] != decomp['io'][i]),
                 min(len(recomp['io']), len(decomp['io'])))
        return 'FAIL', f'io differs at write {n}: {len(recomp["io"])} recomp, {len(decomp["io"])} decomp'
    if recomp['syscalls'] != decomp['syscalls']:
        return 'FAIL', f'syscalls {recomp["syscalls"]} != {decomp["syscalls"]}'
    if strict:
        for name in ('fcr31', 'facc'):
            if ra[name] != da[name]:
                return 'FAIL', f'{name} {ra[name]} != {da[name]}'
    if compare_vu:
        for name in ('vf', 'acc', 'vi', 'q', 'p', 'i', 'vr', 'mac', 'status', 'clip'):
            if ra[name] != da[name]:
                return 'FAIL', f'vu0 {name} differs'
    return 'PASS', f'cycles {recomp["cycles"]}'


def entry_address(name):
    """The function address in an entry-snapshot name (eADDR_fFRAME_NNNN.entry.state), or None."""
    if not name.endswith('.entry.state') or not name.startswith('e'):
        return None
    try:
        return int(name[1:9], 16)
    except ValueError:
        return None


def list_snapshots(directory, function, from_entry):
    """Sorted snapshot paths. from_entry: only the entry snapshots taken at this function."""
    try:
        names = sorted(os.listdir(directory))
    except OSError as e:
        raise SetupError(f'cannot read the snapshot folder: {e}')
    if from_entry:
        names = [n for n in names if entry_address(n) == function]
    else:
        names = [n for n in names if n.endswith('.state') and not n.endswith('.entry.state')]
    if not names:
        raise SetupError(f'no snapshots in {directory}')
    return [os.path.join(directory, n) for n in names]


def read_cases(path):
    """One argument vector per line; blank lines and # comments are skipped."""
    try:
        with open(path, encoding='utf-8') as f:
            lines = [line.strip() for line in f]
    except OSError as e:
        raise SetupError(f'cannot read {path}: {e}')
    cases = [shlex.split(line) for line in lines if line and not line.startswith('#')]
    if not cases:
        raise SetupError(f'no argument lines in {path}')
    return cases


def case_name(snapshot, index, count):
    stem = os.path.basename(snapshot)
    for suffix in ('.entry.state', '.state'):
        if stem.endswith(suffix):
            stem = stem[:-len(suffix)]
            break
    return stem if count == 1 else f'{stem}_c{index:03d}'


def run_ps2run(command, env, timeout):
    """Runs one ps2run call. Returns (exit code or None on timeout, stderr text)."""
    try:
        done = subprocess.run(command, env=env, timeout=timeout, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    except subprocess.TimeoutExpired:
        return None, ''
    except OSError as e:
        raise SetupError(f'cannot run {command[0]}: {e}')
    return done.returncode, done.stderr.decode('utf-8', 'replace')


def call_side(options, snapshot, arguments, out_json, hook, runner):
    """One side of a case: the result document, or a stand-in when the call timed out."""
    command = [options.ps2run, options.elf]
    if options.disc:
        command += ['--disc', options.disc]
    command += ['--snapshot', snapshot, '--call', f'0x{options.function:08X}'] + list(arguments)
    if hook:
        command += ['--hook', hook]
    command += ['--out', out_json]
    for stale in (out_json, out_json + '.mem'):
        if os.path.exists(stale):
            os.remove(stale)
    code, stderr = runner(command, options.environment, options.timeout)
    if code is None:
        return {'status': 'timeout', 'detail': f'no result after {options.timeout} s'}
    if code not in (0, 3):
        last = [line for line in stderr.splitlines() if line.strip()][-1:] or ['no output']
        raise SetupError(f'ps2run exit {code} on {os.path.basename(snapshot)}: {last[0]}')
    try:
        with open(out_json, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise SetupError(f'no usable result file {out_json}: {e}')


def run_case(options, snapshot, arguments, name, runner):
    """Both sides of one case and the verdict: a dict for the summary."""
    recomp_json = os.path.join(options.out, 'recomp', name + '.json')
    decomp_json = os.path.join(options.out, 'decomp', name + '.json')
    record = {'case': name, 'snapshot': snapshot, 'args': list(arguments), 'recomp': recomp_json, 'decomp': decomp_json}
    recomp = call_side(options, snapshot, arguments, recomp_json, None, runner)
    if recomp['status'] != 'ok':
        verdict, text = compare(recomp, recomp, options.returns)
    else:
        decomp = call_side(options, snapshot, arguments, decomp_json, options.hook, runner)
        verdict, text = compare(recomp, decomp, options.returns, read_mem(recomp_json + '.mem'), read_mem(decomp_json + '.mem'),
                                options.strict, options.compare_vu)
    record.update(verdict=verdict, text=text)
    return record


def parse_arguments(argv):
    p = argparse.ArgumentParser(prog='difftest.py', description='Recompiled against decompiled, one function, many snapshots.')
    p.add_argument('--ps2run', required=True)
    p.add_argument('--elf', required=True)
    p.add_argument('--function', required=True, help='entry address, hex')
    p.add_argument('--hook', required=True, help='hook name or 0x address, as ps2run --hook takes it')
    p.add_argument('--snapshots', required=True, help='folder of .state files')
    p.add_argument('--out', required=True, help='folder for the result files (outside the repository)')
    p.add_argument('--args', default=None, help='one argument vector for ps2run, or the word snapshot for entry snapshots')
    p.add_argument('--args-file', default=None, help='one argument vector per line')
    p.add_argument('--returns', default='v0,v1,f0,f1', help='registers compared as results')
    p.add_argument('--compare-fpr', default='', help='more float registers to compare')
    p.add_argument('--compare-vu', action='store_true')
    p.add_argument('--strict', action='store_true', help='also compare fcr31 and facc')
    p.add_argument('-j', type=int, default=1, dest='jobs')
    p.add_argument('--disc', default=None)
    p.add_argument('--env', action='append', default=[], metavar='NAME=VALUE')
    p.add_argument('--timeout', type=int, default=120)
    # An argument vector starts with dashes, which argparse would take for an option of its own.
    argv = list(argv)
    for i in range(len(argv) - 1):
        if argv[i] == '--args':
            argv[i:i + 2] = ['--args=' + argv[i + 1]]
            break
    try:
        options = p.parse_args(argv)
    except SystemExit:
        raise SetupError('bad arguments')
    try:
        options.function = int(options.function, 16)
    except ValueError:
        raise SetupError(f'bad function address {options.function}')
    names = [n for n in (options.returns + ',' + options.compare_fpr).split(',') if n.strip()]
    options.returns = [parse_register(n) for n in names]
    options.environment = dict(os.environ)
    options.environment.update(DEFAULT_ENV)
    for pair in options.env:
        if '=' not in pair:
            raise SetupError(f'--env wants NAME=VALUE, got {pair}')
        key, value = pair.split('=', 1)
        options.environment[key] = value
    if options.args == 'snapshot' and options.args_file:
        raise SetupError('--args snapshot and --args-file do not go together')
    if options.jobs < 1:
        raise SetupError('-j wants 1 or more')
    return options


def plan(options):
    """[(snapshot, argument vector, case name)] for the whole run."""
    from_entry = options.args == 'snapshot'
    snapshots = list_snapshots(options.snapshots, options.function, from_entry)
    if options.args_file:
        vectors = read_cases(options.args_file)
        if options.args:
            vectors = [shlex.split(options.args) + v for v in vectors]
    elif options.args and not from_entry:
        vectors = [shlex.split(options.args)]
    else:
        vectors = [[]]
    return [(s, v, case_name(s, i, len(vectors))) for s in snapshots for i, v in enumerate(vectors)]


def main(argv, runner=run_ps2run, out=sys.stdout):
    try:
        options = parse_arguments(argv[1:])
        cases = plan(options)
        for side in ('recomp', 'decomp'):
            os.makedirs(os.path.join(options.out, side), exist_ok=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
            records = list(pool.map(lambda c: run_case(options, c[0], c[1], c[2], runner), cases))
    except SetupError as e:
        print(f'difftest: {e}', file=sys.stderr)
        return 2
    counts = {'PASS': 0, 'FAIL': 0, 'SKIP': 0}
    for r in records:
        counts[r['verdict']] += 1
        print(f'{r["case"]} {r["verdict"]} {r["text"]}', file=out)
    print(f'{options.hook}: {counts["PASS"]} pass, {counts["FAIL"]} fail, {counts["SKIP"]} skip', file=out)
    summary = {'function': f'0x{options.function:08X}', 'hook': options.hook, 'pass': counts['PASS'], 'fail': counts['FAIL'],
               'skip': counts['SKIP'], 'accepted': counts['PASS'] >= 1 and counts['FAIL'] == 0, 'cases': records}
    with open(os.path.join(options.out, 'summary.json'), 'w', encoding='utf-8') as f:
        json.dump(summary, f, indent=1)
    return 1 if counts['FAIL'] else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
