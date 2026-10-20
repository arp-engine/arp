"""Admission failures must precede build/output mutation."""
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from webgl1_runtime_build import WebGl1RuntimeBuilder

class RuntimeProductAdmissionTests(unittest.TestCase):
    def test_protected_output_rejected_before_mutation(self):
        with TemporaryDirectory() as directory:
            root=Path(directory)
            program=root/'program'
            program.mkdir()
            (program/'webgl1_graphics.webgl-program.yaml').write_text('original input')
            tool=root/'tool'
            tool.write_text('explicit tool')
            for name in ('src','scripts','tests','cmake','docs','3rd','generated','shaders','.git'):
                output=root/name/'nested'
                builder=WebGl1RuntimeBuilder(root,output,program,tool,tool,tool)
                with self.assertRaisesRegex(ValueError,'OUTPUT-SOURCE'):
                    builder.build()
                self.assertFalse(output.exists())
            for output in (root,program,program/'nested'):
                with self.assertRaisesRegex(ValueError,'OUTPUT-ROOT'):
                    WebGl1RuntimeBuilder(root,output,program,tool,tool,tool).build()
            self.assertEqual((program/'webgl1_graphics.webgl-program.yaml').read_text(),'original input')

    def test_missing_tool_and_original_program_reject_before_output(self):
        with TemporaryDirectory() as directory:
            root=Path(directory); output=root/'build'
            missing=root/'missing'; tool=root/'tool';tool.write_text('tool')
            with self.assertRaisesRegex(ValueError,'TOOL-MISSING'):
                WebGl1RuntimeBuilder(root,output,root,missing,tool,tool).build()
            with self.assertRaisesRegex(ValueError,'ORIGINAL-PROGRAM-MISSING'):
                WebGl1RuntimeBuilder(root,output,root,tool,tool,tool).build()
            self.assertFalse(output.exists())

if __name__=='__main__': unittest.main()
