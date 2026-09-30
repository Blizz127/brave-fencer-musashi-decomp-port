"""Synthetic IRQ device driving the shared retail dispatcher and VBlank child."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
class Memory(C.Structure):
    _fields_ = [("bytes", C.c_ubyte * 0x200000)]
READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint16))
WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint16)
CALL = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Memory), C.c_uint32)
DIAG = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Memory), C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32)
RETURN = C.CFUNCTYPE(C.c_int, C.c_void_p)
class Device(C.Structure):
    _fields_ = [("userdata", C.c_void_p), ("read16", READ), ("write16", WRITE), ("execute", CALL), ("diagnostic", DIAG), ("return_from_exception", RETURN)]

class IrqTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tmp = tempfile.TemporaryDirectory(); cls.addClassCleanup(tmp.cleanup)
        out = Path(tmp.name) / 'irq.so'
        subprocess.run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
            '-DMUSASHI_NATIVE_IRQ_DISPATCH=1', '-DMUSASHI_NATIVE_CALLBACK_HANDLER=1',
            '-I', str(ROOT/'include'), '-I', str(ROOT/'pc_port/include'),
            str(ROOT/'pc_port/libetc_irq_dispatch.c'), str(ROOT/'pc_port/libetc_vsync_handler.c'),
            str(ROOT/'pc_port/boot_memory.c'), '-lcrypto', '-o', str(out)],
            check=True, capture_output=True, stdin=subprocess.DEVNULL, timeout=60)
        cls.lib = C.CDLL(str(out))
        cls.call = cls.lib.musashi_boot_call_800427f4
        cls.call.argtypes = [C.POINTER(Memory), C.POINTER(Device)]
        cls.child = cls.lib.musashi_boot_call_80042ce8
        cls.child.argtypes = [C.POINTER(Memory), CALL, C.c_void_p]

    def put(self, a, v, half=False):
        struct.pack_into('<H' if half else '<I', self.mem, a-0x80000000, v)
    def get(self, a, half=False):
        return struct.unpack_from('<H' if half else '<I', self.mem, a-0x80000000)[0]
    def setUp(self):
        self.mem = Memory(); self.trace = []; self.status = 1; self.mask = 1
        self.put(0x8006BAFC, 1, True); self.put(0x8006BB2C, 1, True)
        self.put(0x8006CB88, 0x1F801070); self.put(0x8006CB8C, 0x1F801074)
        self.put(0x8006BB00, 0x80042CE8)
        def read(_, a, value):
            self.trace.append(('read', a))
            if a not in (0x1F801070, 0x1F801074): return 0
            value[0] = self.status if a == 0x1F801070 else self.mask
            return 1
        def write(_, a, value):
            self.trace.append(('write', a, value))
            if a != 0x1F801070: return 0
            self.status &= value
            return 1
        def execute(_, memory, target):
            self.trace.append(('call', target, self.get(0x8006BAFE, True)))
            if target != 0x80042CE8: return 0
            return self.child(memory, CALL(), None)
        self.read, self.write, self.execute = READ(read), WRITE(write), CALL(execute)
        self.device = Device(None, self.read, self.write, self.execute, DIAG(), RETURN())

    def test_irq_acknowledges_then_runs_real_child_and_stops_at_bios_boundary(self):
        self.assertEqual(self.call(self.mem, self.device), 2)
        self.assertEqual(self.get(0x8006CBB8), 1)
        self.assertEqual(self.get(0x8006BAFE, True), 0)
        self.assertEqual(self.trace[:4], [('read',0x1F801070), ('read',0x1F801074),
            ('write',0x1F801070,0xFFFE), ('call',0x80042CE8,1)])
        self.assertEqual(len(self.trace), 8)  # initial sample, rescan, tail

    def test_ack_can_replace_live_callback_and_refusal_preserves_active_state(self):
        def write(_, a, v):
            self.status &= v; self.put(0x8006BB00, 0x80001000); return 1
        self.device.write16 = WRITE(write)
        self.assertEqual(self.call(self.mem, self.device), 0)
        self.assertEqual(self.trace[-1], ('call',0x80001000,1))
        self.assertEqual(self.get(0x8006BAFE, True), 1)
        self.assertEqual(self.get(0x8006CBB8), 0)

    def test_rescan_delivers_newly_raised_irq(self):
        def execute(_, memory, target):
            self.trace.append(('call',target))
            if len([x for x in self.trace if x[0]=='call']) == 1: self.status = 1
            return self.child(memory, CALL(), None)
        self.device.execute = CALL(execute)
        self.assertEqual(self.call(self.mem, self.device), 2)
        self.assertEqual(self.get(0x8006CBB8), 2)

    def test_unhandled_pending_threshold_requires_real_diagnostic(self):
        self.put(0x8006BB2C, 0, True); self.put(0x8006CB94, 0x801)
        self.assertEqual(self.call(self.mem, self.device), 0)
        self.assertEqual(self.get(0x8006CB94), 0x802)
        self.assertEqual(self.status, 1)
        self.assertEqual(self.get(0x8006BAFE, True), 1)

    def test_guard_zero_refuses_missing_diagnostic_before_active_store(self):
        self.put(0x8006BAFC, 0, True)
        self.assertEqual(self.call(self.mem, self.device), 0)
        self.assertEqual(self.get(0x8006BAFE, True), 0)
        self.assertEqual(self.call(None, self.device), 0)

    def test_guard_zero_delegates_b0_17_after_diagnostic(self):
        self.put(0x8006BAFC, 0, True)
        observed = []
        def diagnostic(_, memory, fmt, first, second, count):
            observed.append(('diagnostic', fmt, first, second, count))
            return 1
        def return_from_exception(_):
            observed.append(('return', self.get(0x8006BAFE, True)))
            return 1
        self.diagnostic = DIAG(diagnostic)
        self.return_from_exception = RETURN(return_from_exception)
        self.device.diagnostic = self.diagnostic
        self.device.return_from_exception = self.return_from_exception
        self.assertEqual(self.call(self.mem, self.device), 3)
        self.assertEqual(observed, [
            ('diagnostic', 0x80073558, 1, 0, 1),
            ('return', 0),
        ])

    def test_tail_keeps_pointer_snapshot_across_device_reads(self):
        self.put(0x8006BB2C, 0, True)
        self.put(0x8006CB94, 0x801)
        reads = []
        def read(_, a, v):
            reads.append(a)
            if a == 0x1F801070 and len(reads) == 3:
                self.put(0x8006CB8C, 0x1F801078)
            if a not in (0x1F801070, 0x1F801074): return 0
            v[0] = 1; return 1
        diagnostics = []
        def diagnostic(_, memory, fmt, first, second, count):
            diagnostics.append((fmt,first,second,count))
            self.put(0x8006CB88, 0x1F801072)
            return 1  # synthetic diagnostic only
        writes = []
        def write(_, a, v):
            writes.append((a,v,self.get(0x8006CB94))); return 1
        self.device.read16 = READ(read)
        self.device.write16 = WRITE(write)
        self.device.diagnostic = DIAG(diagnostic)
        self.assertEqual(self.call(self.mem, self.device), 2)
        self.assertEqual(reads, [0x1F801070,0x1F801074]*3)
        self.assertEqual(diagnostics, [(0x80073574,1,1,2)])
        self.assertEqual(writes, [(0x1F801072,0,0)])

    def test_signed_negative_counter_wrap_does_not_trigger_diagnostic(self):
        self.put(0x8006BB2C, 0, True); self.put(0x8006CB94, 0xFFFFFFFF)
        self.assertEqual(self.call(self.mem, self.device), 2)
        self.assertEqual(self.get(0x8006CB94), 0)
        self.assertEqual(self.status, 1)

    def test_refused_device_read_is_not_context_return(self):
        self.device.read16 = READ(lambda *_: 0)
        self.assertEqual(self.call(self.mem, self.device), 0)
        self.assertEqual(self.get(0x8006BAFE, True), 1)
        self.assertEqual(self.get(0x8006CBB8), 0)

    def test_platform_can_delegate_b0_17_only_after_dispatch_is_inactive(self):
        observed = []
        def return_from_exception(_):
            observed.append((self.get(0x8006BAFE, True), self.get(0x8006CBB8),
                             self.get(0x8006BB34)))
            return 1
        self.device.return_from_exception = RETURN(return_from_exception)
        self.put(0x8006BB34, 0x80042780)
        self.assertEqual(self.call(self.mem, self.device), 3)
        self.assertEqual(observed, [(0, 1, 0x80042780)])

    def test_refused_b0_17_keeps_its_preceding_dispatch_effects(self):
        self.device.return_from_exception = RETURN(lambda _: 0)
        self.assertEqual(self.call(self.mem, self.device), 0)
        self.assertEqual(self.get(0x8006BAFE, True), 0)
        self.assertEqual(self.get(0x8006CBB8), 1)
