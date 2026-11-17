"""Checks the Paramdex field layout code on synthetic definitions. Run: python3 -I research/test_paramlib.py"""
import os, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
import paramlib
def layout(fields_xml):
    d = tempfile.mkdtemp()
    open(os.path.join(d, "T.xml"), "w").write("<PARAMDEF><Fields>%s</Fields></PARAMDEF>" % "".join('<Field Def="%s"/>' % f for f in fields_xml))
    paramlib.DEFS = d + os.sep
    return paramlib.load_def("T")
# a value bit plus 7 padding bits share ONE byte; the next u8 starts at offset 1
f, size = layout(["u8 first:1", "dummy8 pad:7", "u8 next"])
assert size == 2 and [(x[0], x[2]) for x in f] == [("first", 0), ("next", 1)], (f, size)
# plain dummy8 arrays still take whole bytes
f, size = layout(["u8 a", "dummy8 pad[3]", "u8 b"])
assert size == 5 and f[1][2] == 4, (f, size)
# two bitfields share a byte
f, size = layout(["u8 a:3", "u8 b:5", "u8 c"])
assert size == 2 and f[1][2] == 0 and f[1][5] == (3, 5) and f[2][2] == 1, (f, size)
print("paramlib tests passed")
