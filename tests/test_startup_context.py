"""Guard the byte layout used to recover startup's retail state accesses."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StartupContextTests(unittest.TestCase):
    def test_render_rotation_and_transform_are_distinct_layouts(self):
        source = '''
#include <stddef.h>
#include "config/context/render_transform.c"
typedef char rotation_size[(sizeof(MusashiRotationView) == 18) ? 1 : -1];
typedef char transform_size[(sizeof(MusashiTransformView) == 32) ? 1 : -1];
typedef char padding_offset[(offsetof(MusashiTransformView, unknown12) == 18) ? 1 : -1];
typedef char translation_offset[(offsetof(MusashiTransformView, translation) == 20) ? 1 : -1];
typedef char translation_width[(sizeof(((MusashiTransformView *)0)->translation) == 12) ? 1 : -1];
typedef char rotation_offset[(offsetof(MusashiTransformView, rotation) == 0) ? 1 : -1];
'''
        command = ['cc', '-std=c89', '-pedantic-errors', '-fsyntax-only', '-x', 'c',
                   '-I', str(ROOT), '-I', str(ROOT / 'include'), '-']
        result = subprocess.run(command, input=source, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        context = (ROOT / 'config/context/render_transform.c').read_text()
        narrowed = context.replace('s32 translation[3];', 's16 translation[3];')
        invalid = source.replace('#include "config/context/render_transform.c"', narrowed)
        rejected = subprocess.run(command, input=invalid, text=True, capture_output=True)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn('translation_width', rejected.stderr)

    def test_observed_offsets_and_widths_compile_on_host(self):
        fields = {
            "blocks0038": (0x38, 2 * 0x5C), "blocks014C": (0x14C, 2 * 0x14),
            "field0188": (0x188, 2), "field018A": (0x18A, 2),
            "fieldA3A8": (0xA3A8, 2), "fieldA3AC": (0xA3AC, 2),
            "fieldA3D2": (0xA3D2, 2), "fieldA3D4": (0xA3D4, 2),
            "fieldA3E3": (0xA3E3, 1), "fieldA3E8": (0xA3E8, 4),
        }
        source = '#include <stddef.h>\n#include "config/context/startup.c"\n'
        source += (
            'typedef char descriptor_stride[(sizeof(MusashiStartupDescriptor) == 12) ? 1 : -1];\n'
            'typedef char descriptor_address[(offsetof(MusashiStartupDescriptor, address08) == 8) ? 1 : -1];\n'
            'typedef char ps1_address_width[(sizeof(((MusashiStartupDescriptor *)0)->address08) == 4) ? 1 : -1];\n'
            'typedef char table_stride[(sizeof(MusashiStartupAddressStride20) == 20) ? 1 : -1];\n'
            'typedef char table_word_offset[(offsetof(MusashiStartupAddressStride20, address00) == 0) ? 1 : -1];\n'
            'typedef char table_word_width[(sizeof(((MusashiStartupAddressStride20 *)0)->address00) == 4) ? 1 : -1];\n'
        )
        for name, (offset, width) in fields.items():
            source += (
                f'typedef char offset_{name}[(offsetof(MusashiStartupStateView, {name})'
                f' == {offset}) ? 1 : -1];\n'
                f'typedef char width_{name}[(sizeof(((MusashiStartupStateView *)0)->{name})'
                f' == {width}) ? 1 : -1];\n'
            )
        result = subprocess.run(
            ['cc', '-std=c89', '-pedantic-errors', '-fsyntax-only', '-x', 'c',
             '-I', str(ROOT), '-I', str(ROOT / 'include'), '-'],
            input=source, text=True, capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        # Negative control: a narrowed address must fail the width assertion,
        # even if struct padding leaves the descriptor stride unchanged.
        narrowed = (ROOT / 'config/context/startup.c').read_text().replace(
            'u32 address08;', 'u16 address08;'
        )
        invalid = source.replace('#include "config/context/startup.c"', narrowed)
        rejected = subprocess.run(
            ['cc', '-std=c89', '-pedantic-errors', '-fsyntax-only', '-x', 'c',
             '-I', str(ROOT), '-I', str(ROOT / 'include'), '-'],
            input=invalid, text=True, capture_output=True,
        )
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn('ps1_address_width', rejected.stderr)
