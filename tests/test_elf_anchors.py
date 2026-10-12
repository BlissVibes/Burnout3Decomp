import contextlib, importlib.util, io, json, os, struct, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('elf_anchors', os.path.join(HERE, '..', 'tools', 'elf-anchors.py'))
elf_anchors = importlib.util.module_from_spec(spec)
spec.loader.exec_module(elf_anchors)

# A ten-line MIPS encoder: just the instructions the synthetic program needs.
AT, V0, V1, A0, A1, GP, SP, RA = 1, 2, 3, 4, 5, 28, 29, 31
def itype(op, rs, rt, imm): return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)
def lui(rt, imm): return itype(0x0F, 0, rt, imm)
def addiu(rt, rs, imm): return itype(0x09, rs, rt, imm)
def ori(rt, rs, imm): return itype(0x0D, rs, rt, imm)
def lw(rt, off, base): return itype(0x23, base, rt, off)
def sw(rt, off, base): return itype(0x2B, base, rt, off)
def lwc1(ft, off, base): return itype(0x31, base, ft, off)
def mtc1(rt, fs): return (0x11 << 26) | (4 << 21) | (rt << 16) | (fs << 11)
def hi(addr): return ((addr + 0x8000) >> 16) & 0xFFFF      # the sign adjust for a signed low half
JR_RA, NOP = 0x03E00008, 0

TEXT, RODATA, GP_VALUE = 0x00100000, 0x00200000, 0x00214000
STRING, FLOAT, WORD, SAVED = 0x0020FFF0, 0x00200010, 0x00200020, 0x00200030   # STRING has a negative low half

FUNC_A = [
    addiu(SP, SP, -32),                 # 0x100000
    lui(A0, hi(STRING)),
    addiu(A0, A0, STRING),              # 0x100008: string reference
    lui(V0, hi(FLOAT)),
    lwc1(0, FLOAT, V0),                 # 0x100010: float loaded from .rodata
    JR_RA,
    addiu(SP, SP, 32),
]
FUNC_B = [
    addiu(SP, SP, -16),                 # 0x10001C
    lui(V0, 0xFD46),
    ori(V0, V0, 0x2907),                # 0x100024: 32-bit immediate
    lui(AT, 0x4316),                    # 0x100028: 150.0 as lui + mtc1
    mtc1(AT, 1),
    lui(V1, hi(WORD)),
    NOP,
    lw(V1, WORD, V1),                   # 0x100038: table word, one instruction after the lui
    lw(A1, STRING - GP_VALUE, GP),      # 0x10003C: gp-relative string pointer
    lui(V0, hi(SAVED)),
    sw(A1, SAVED, V0),                  # 0x100044: store
    JR_RA,
    addiu(SP, SP, 16),
]
FUNC_C = [
    addiu(SP, SP, -8),                  # 0x100050
    lui(A0, hi(FLOAT)),
] + [NOP] * 8 + [
    addiu(A0, A0, FLOAT),               # nine instructions after the lui: too far to pair
    lui(A1, hi(WORD)),
    lui(A1, 0x0030),                    # the register is loaded again
    addiu(A1, A1, 0x1234),              # 0x100084: pairs with the second lui only -> 0x00301234
    JR_RA,
    addiu(SP, SP, 8),
]
FUNC_B_START, FUNC_C_START = TEXT + 4 * len(FUNC_A), TEXT + 4 * (len(FUNC_A) + len(FUNC_B))


def make_elf(machine=8):
    text = struct.pack('<%dI' % len(FUNC_A + FUNC_B + FUNC_C), *(FUNC_A + FUNC_B + FUNC_C))
    rodata = bytearray(0x10010)
    struct.pack_into('<f', rodata, FLOAT - RODATA, 2.2374146)
    struct.pack_into('<I', rodata, WORD - RODATA, 0x02B9D6F8)
    rodata[0x100:0x109] = b'Slide Max'                      # followed by NUL
    rodata[0x200:0x212] = b'Physics/Drift Bias'              # a longer string: not a hit for "Physics/Drift"
    rodata[STRING - RODATA:STRING - RODATA + 13] = b'Physics/Drift'
    text_off, rodata_off = 0x100, 0x100 + len(text)
    header = struct.pack('<4sBBBB8xHHIIIIIHHHHHH', b'\x7fELF', 1, 1, 1, 0, 2, machine, 1, TEXT, 52, 0, 0, 52, 32, 2, 40, 0, 0)
    phdrs = struct.pack('<8I', 1, text_off, TEXT, TEXT, len(text), len(text), 5, 16)
    phdrs += struct.pack('<8I', 1, rodata_off, RODATA, RODATA, len(rodata), len(rodata) + 0x1000, 4, 16)
    return (header + phdrs).ljust(text_off, b'\0') + text + bytes(rodata)


def run(queries, gp=None):
    image = elf_anchors.load(make_elf())
    return elf_anchors.search(image, elf_anchors.parse_query(queries), gp=gp)


def refs(result):
    return [(hit['address'], ref['pc'], ref['function']) for hit in result['hits'] for ref in hit['refs']]


class ElfAnchorsTest(unittest.TestCase):
    def test_a_string_is_found_with_its_lui_addiu_reference(self):
        result, = run(['str:Physics/Drift'])
        self.assertEqual([hit['address'] for hit in result['hits']], [STRING])
        self.assertEqual(refs(result), [(STRING, 0x100008, TEXT)])
        self.assertEqual(result['hits'][0]['refs'][0]['via'], 'addiu')
        self.assertEqual(result['functions'], 1)

    def test_gp_relative_references_need_the_gp_value(self):
        result, = run(['str:Physics/Drift'], gp=GP_VALUE)
        self.assertEqual(refs(result), [(STRING, 0x100008, TEXT), (STRING, 0x10003C, FUNC_B_START)])
        self.assertEqual(result['hits'][0]['refs'][1]['via'], 'gp lw')
        self.assertEqual(result['functions'], 2)

    def test_a_float_in_data_is_found_through_lwc1(self):
        result, = run(['f32:2.2374146'])
        self.assertEqual(refs(result), [(FLOAT, 0x100010, TEXT)])
        self.assertEqual(result['hits'][0]['refs'][0]['via'], 'lwc1')

    def test_a_float_with_a_zero_low_half_is_found_as_lui_mtc1(self):
        result, = run(['f32:150.0'])
        self.assertEqual(refs(result), [('imm', 0x100028, FUNC_B_START)])
        self.assertEqual(result['hits'][0]['refs'][0]['via'], 'lui+mtc1')

    def test_a_32_bit_immediate_is_found_as_lui_ori(self):
        result, = run(['u32:FD462907'])
        self.assertEqual(refs(result), [('imm', 0x100024, FUNC_B_START)])

    def test_a_table_word_is_found_with_a_reference_a_few_instructions_after_the_lui(self):
        result, = run(['u32:0x02B9D6F8'])
        self.assertEqual(refs(result), [(WORD, 0x100038, FUNC_B_START)])

    def test_a_store_counts_as_a_reference(self):
        index = elf_anchors.Index(elf_anchors.load(make_elf()))
        self.assertEqual(index.refs[SAVED], [(0x100044, 'sw')])

    def test_bytes_match_at_any_offset_and_strings_need_their_nul(self):
        result, = run(['bytes:6c696465204d6178'])              # "lide Max", unaligned
        self.assertEqual([hit['address'] for hit in result['hits']], [RODATA + 0x101])
        result, = run(['str:Slide Max'])
        self.assertEqual([hit['address'] for hit in result['hits']], [RODATA + 0x100])
        self.assertEqual(result['functions'], 0)

    def test_the_pairing_window_and_a_reloaded_register(self):
        index = elf_anchors.Index(elf_anchors.load(make_elf()))
        self.assertEqual([pc for pc, _ in index.refs[FLOAT]], [0x100010])       # not FUNC_C's far addiu
        self.assertEqual(index.refs[0x00301234], [(0x100084, 'addiu')])
        self.assertNotIn((hi(WORD) << 16) + 0x1234, index.refs)
        self.assertEqual(index.function_start(0x100084), FUNC_C_START)
        self.assertEqual(index.function_start(TEXT), TEXT)

    def test_a_missing_anchor_reports_zero_hits(self):
        result, = run(['str:No such string'])
        self.assertEqual(result['hits'], [])
        self.assertEqual(result['functions'], 0)

    def test_query_lines(self):
        anchors = elf_anchors.parse_query(['# comment', '', 'str:Boost Bar ', 'f32:27.5', 'u32:fd462907', 'bytes:01 02 0a'])
        self.assertEqual([a.kind for a in anchors], ['str', 'f32', 'u32', 'bytes'])
        self.assertEqual(anchors[0].pattern, b'Boost Bar \0')
        self.assertEqual(anchors[1].pattern, struct.pack('<f', 27.5))
        self.assertEqual(anchors[2].value, 0xFD462907)
        self.assertEqual(anchors[3].pattern, b'\x01\x02\x0a')
        for bad in ('Boost Bar', 'f32:fast', 'u32:123456789', 'bytes:0g', 'int:5', 'str:'):
            with self.assertRaises(ValueError):
                elf_anchors.parse_query([bad])

    def test_only_mips_elf32_little_endian_is_accepted(self):
        for data in (b'not an elf', make_elf(machine=3)):
            with self.assertRaises(ValueError):
                elf_anchors.load(data)


class MainTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.elf = os.path.join(self.dir, 'program.elf')
        with open(self.elf, 'wb') as f:
            f.write(make_elf())

    def tearDown(self):
        for name in os.listdir(self.dir):
            os.unlink(os.path.join(self.dir, name))
        os.rmdir(self.dir)

    def main(self, queries, *options):
        query = os.path.join(self.dir, 'query.txt')
        with open(query, 'w', newline='\n') as f:
            f.write('\n'.join(queries) + '\n')
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = elf_anchors.main(['elf-anchors.py', self.elf, query, *options])
        return code, out.getvalue(), err.getvalue()

    def test_text_table(self):
        code, out, _ = self.main(['str:Physics/Drift', 'f32:150.0', 'str:Nothing'], '--gp', '0x%X' % GP_VALUE)
        self.assertEqual(code, 0)
        lines = out.splitlines()
        self.assertEqual(lines[0].split(), ['anchor', 'hit', 'ref', 'pc', 'via', 'function', '(approx)'])
        self.assertIn('str:Physics/Drift  0x0020FFF0  0x00100008  addiu     0x00100000', lines)
        self.assertIn('str:Physics/Drift  0x0020FFF0  0x0010003C  gp lw     0x0010001C', lines)
        self.assertIn('f32:150.0          imm         0x00100028  lui+mtc1  0x0010001C', lines)
        self.assertIn('str:Physics/Drift: 1 hit, 2 references, 2 functions', lines)
        self.assertIn('str:Nothing: 0 hits, 0 references, 0 functions', lines)

    def test_json_and_max_refs(self):
        code, out, _ = self.main(['str:Physics/Drift'], '--json', '--gp', '0x%X' % GP_VALUE, '--max-refs', '1')
        self.assertEqual(code, 0)
        result, = json.loads(out)
        self.assertEqual(result['anchor'], 'str:Physics/Drift')
        self.assertEqual(result['references'], 2)                # the count is not capped
        self.assertEqual(result['functions'], 2)
        self.assertEqual(result['hits'], [{'address': STRING, 'refs': [{'pc': 0x100008, 'via': 'addiu', 'function': TEXT}]}])

    def test_a_missing_anchor_still_exits_0(self):
        code, out, _ = self.main(['str:No such string'])
        self.assertEqual(code, 0)
        self.assertIn('0 hits', out)

    def test_usage_and_bad_input_exit_2(self):
        self.assertEqual(elf_anchors.main(['elf-anchors.py']), 2)
        self.assertEqual(self.main(['str:x'], '--gp')[0], 2)
        self.assertEqual(self.main(['nonsense'])[0], 2)
        with open(self.elf, 'wb') as f:
            f.write(b'not an elf')
        code, _, err = self.main(['str:x'])
        self.assertEqual(code, 2)
        self.assertIn('not an ELF', err)


if __name__ == '__main__':
    unittest.main()
